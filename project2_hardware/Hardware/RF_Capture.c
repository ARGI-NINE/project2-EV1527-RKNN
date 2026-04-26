#include <string.h>

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"
#include "misc.h"

#include "RF_Capture.h"
#include "RF_Protocol.h"
#include "RF_Uart.h"

#define RF_CAPTURE_READY_QUEUE_SIZE 4u
#define RF_CAPTURE_DEFAULT_MIN_PULSE_US 80u
#define RF_CAPTURE_DEFAULT_MAX_PULSE_US 65535u
#define RF_CAPTURE_DEFAULT_SYNC_US 8000u
#define RF_CAPTURE_DEFAULT_MIN_FRAME_PULSES 50u

#define RF_CAPTURE_TIM_GPIO_PORT GPIOA
#define RF_CAPTURE_TIM_GPIO_PIN GPIO_Pin_0
#define RF_CAPTURE_TIM_GPIO_MODE GPIO_Mode_IPU
#define RF_CAPTURE_TIM_IC_FILTER 0xFu

typedef struct {
    rf_frame_t CurrentFrame;
    rf_frame_t ReadyQueue[RF_CAPTURE_READY_QUEUE_SIZE];
    uint32_t TimerHz;
    uint16_t TimerPeriod;
    uint64_t LastCaptureStampTicks;
    uint8_t HasLastCaptureStamp;
    uint16_t MinPulseUs;
    uint16_t MaxPulseUs;
    uint16_t SyncUs;
    uint16_t MinFramePulses;
    uint16_t IdleFlushMs;
    volatile uint8_t ReadyHead;
    volatile uint8_t ReadyTail;
    volatile uint32_t DroppedFrames;
    volatile uint32_t MsTick;
    volatile uint32_t LastEdgeMs;
    volatile uint16_t LastIntervalUs;
    volatile uint32_t Tim2OverflowCount;
} RF_CaptureState_t;

static RF_CaptureState_t g_rf_capture;

static uint16_t RF_Capture_ComputeIdleFlushMs(uint16_t sync_us) {
    uint32_t idle_ms = (uint32_t)sync_us / 1000u + 8u;
    if (idle_ms < 12u) {
        idle_ms = 12u;
    } else if (idle_ms > 100u) {
        idle_ms = 100u;
    }
    return (uint16_t)idle_ms;
}

static uint16_t RF_Capture_TicksToUs(uint32_t ticks) {
    uint64_t us = ((uint64_t)ticks * 1000000ull) / (uint64_t)g_rf_capture.TimerHz;
    if (us > 65535ull) {
        us = 65535ull;
    }
    return (uint16_t)us;
}

static uint64_t RF_Capture_BuildStampTicks(uint32_t overflow_count, uint16_t ccr) {
    return ((uint64_t)overflow_count * ((uint64_t)g_rf_capture.TimerPeriod + 1ull)) + (uint64_t)ccr;
}

static uint8_t RF_Capture_FirstLowLongestForPhase(const rf_frame_t *Frame, uint16_t LowStartIndex) {
    uint16_t first_low = 0u;
    uint16_t i = 0u;

    if (Frame == NULL || LowStartIndex >= Frame->len) {
        return 0u;
    }

    first_low = Frame->pulse[LowStartIndex];
    if (first_low == 0u) {
        return 0u;
    }

    for (i = (uint16_t)(LowStartIndex + 2u); i < Frame->len; i = (uint16_t)(i + 2u)) {
        if (Frame->pulse[i] > first_low) {
            return 0u;
        }
    }
    return 1u;
}

static uint8_t RF_Capture_FrameFirstLowIsLongest(const rf_frame_t *Frame) {
    if (Frame == NULL || Frame->len < 4u) {
        return 0u;
    }

    if (RF_Capture_FirstLowLongestForPhase(Frame, 1u) != 0u) {
        return 1u;
    }
    if (RF_Capture_FirstLowLongestForPhase(Frame, 0u) != 0u) {
        return 1u;
    }
    return 0u;
}

static void RF_Capture_EnqueueFrame(const rf_frame_t *Frame) {
    uint8_t next = 0u;
    rf_frame_t *dst = NULL;

    if (Frame == NULL) {
        return;
    }
    if (Frame->len < g_rf_capture.MinFramePulses || Frame->len > RF_BUFFER_SIZE) {
        return;
    }
    if (RF_Capture_FrameFirstLowIsLongest(Frame) == 0u) {
        return;
    }

    next = (uint8_t)((g_rf_capture.ReadyHead + 1u) % RF_CAPTURE_READY_QUEUE_SIZE);
    if (next == g_rf_capture.ReadyTail) {
        g_rf_capture.DroppedFrames++;
        return;
    }

    dst = &g_rf_capture.ReadyQueue[g_rf_capture.ReadyHead];
    dst->len = Frame->len;
    memcpy(dst->pulse, Frame->pulse, (size_t)Frame->len * sizeof(uint16_t));
    g_rf_capture.ReadyHead = next;
}

static uint8_t RF_Capture_DequeueFrame(rf_frame_t *OutFrame) {
    const rf_frame_t *src = NULL;
    /*
     * Queue model: TIM2 ISR is the single producer (updates ReadyHead),
     * main loop is the single consumer (updates ReadyTail).
     * Producer publishes a slot only after payload copy + head advance,
     * so consumer can copy tail slot without long global IRQ masking.
     */
    if (OutFrame == NULL) {
        return 0u;
    }
    if (g_rf_capture.ReadyTail == g_rf_capture.ReadyHead) {
        return 0u;
    }

    src = &g_rf_capture.ReadyQueue[g_rf_capture.ReadyTail];
    OutFrame->len = src->len;
    memcpy(OutFrame->pulse, src->pulse, (size_t)src->len * sizeof(uint16_t));
    g_rf_capture.ReadyTail = (uint8_t)((g_rf_capture.ReadyTail + 1u) % RF_CAPTURE_READY_QUEUE_SIZE);
    return 1u;
}

static void RF_Capture_FlushCurrentFrame(void) {
    if (g_rf_capture.CurrentFrame.len >= g_rf_capture.MinFramePulses) {
        RF_Capture_EnqueueFrame(&g_rf_capture.CurrentFrame);
    }
    g_rf_capture.CurrentFrame.len = 0u;
}

static void RF_Capture_DetectPulse(uint16_t PulseUs) {
    if (PulseUs == 0u) {
        return;
    }
    if (PulseUs < g_rf_capture.MinPulseUs || PulseUs > g_rf_capture.MaxPulseUs) {
        return;
    }

    if (PulseUs > g_rf_capture.SyncUs && g_rf_capture.CurrentFrame.len >= g_rf_capture.MinFramePulses) {
        uint16_t carry_pulse = 0u;
        uint8_t has_carry = 0u;
        if (g_rf_capture.CurrentFrame.len > g_rf_capture.MinFramePulses) {
            carry_pulse = g_rf_capture.CurrentFrame.pulse[g_rf_capture.CurrentFrame.len - 1u];
            if (carry_pulse <= g_rf_capture.SyncUs) {
                g_rf_capture.CurrentFrame.len--;
                has_carry = 1u;
            }
        }
        RF_Capture_FlushCurrentFrame();
        if (has_carry != 0u && g_rf_capture.CurrentFrame.len < RF_BUFFER_SIZE) {
            g_rf_capture.CurrentFrame.pulse[g_rf_capture.CurrentFrame.len++] = carry_pulse;
        }
    }

    if (g_rf_capture.CurrentFrame.len < RF_BUFFER_SIZE) {
        g_rf_capture.CurrentFrame.pulse[g_rf_capture.CurrentFrame.len++] = PulseUs;
    } else {
        RF_Capture_FlushCurrentFrame();
        g_rf_capture.CurrentFrame.pulse[g_rf_capture.CurrentFrame.len++] = PulseUs;
    }
}

void RF_Capture_SetFilter(
    uint16_t MinPulseUs,
    uint16_t MaxPulseUs,
    uint16_t SyncUs,
    uint16_t MinFramePulses
) {
    uint16_t clamped_min_frame_pulses = MinFramePulses;

    if (MinPulseUs > 0u) {
        g_rf_capture.MinPulseUs = MinPulseUs;
    }
    if (MaxPulseUs > g_rf_capture.MinPulseUs) {
        g_rf_capture.MaxPulseUs = MaxPulseUs;
    }
    if (SyncUs > g_rf_capture.MinPulseUs) {
        g_rf_capture.SyncUs = SyncUs;
        g_rf_capture.IdleFlushMs = RF_Capture_ComputeIdleFlushMs(SyncUs);
    }
    if (clamped_min_frame_pulses > RF_BUFFER_SIZE) {
        clamped_min_frame_pulses = RF_BUFFER_SIZE;
    }
    if (clamped_min_frame_pulses > 0u) {
        g_rf_capture.MinFramePulses = clamped_min_frame_pulses;
    }
}

void RF_Capture_Init(void) {
    GPIO_InitTypeDef GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    TIM_ICInitTypeDef TIM_ICInitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    memset(&g_rf_capture, 0, sizeof(g_rf_capture));
    g_rf_capture.TimerHz = 1000000u;
    g_rf_capture.TimerPeriod = 0xFFFFu;
    g_rf_capture.MinPulseUs = RF_CAPTURE_DEFAULT_MIN_PULSE_US;
    g_rf_capture.MaxPulseUs = RF_CAPTURE_DEFAULT_MAX_PULSE_US;
    g_rf_capture.SyncUs = RF_CAPTURE_DEFAULT_SYNC_US;
    g_rf_capture.MinFramePulses = RF_CAPTURE_DEFAULT_MIN_FRAME_PULSES;
    g_rf_capture.IdleFlushMs = RF_Capture_ComputeIdleFlushMs(g_rf_capture.SyncUs);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    /* TIM2_CH1 = PA0, use pull-up input to improve idle-level stability. */
    GPIO_InitStructure.GPIO_Mode = RF_CAPTURE_TIM_GPIO_MODE;
    GPIO_InitStructure.GPIO_Pin = RF_CAPTURE_TIM_GPIO_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(RF_CAPTURE_TIM_GPIO_PORT, &GPIO_InitStructure);

    TIM_TimeBaseStructInit(&TIM_TimeBaseInitStructure);
    TIM_TimeBaseInitStructure.TIM_Prescaler = 71u;
    TIM_TimeBaseInitStructure.TIM_Period = g_rf_capture.TimerPeriod;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);

    TIM_ICStructInit(&TIM_ICInitStructure);
    TIM_ICInitStructure.TIM_Channel = TIM_Channel_1;
    TIM_ICInitStructure.TIM_ICPolarity = TIM_ICPolarity_Rising;
    TIM_ICInitStructure.TIM_ICSelection = TIM_ICSelection_DirectTI;
    TIM_ICInitStructure.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    /* Max digital filter for RF edge debounce/noise suppression. */
    TIM_ICInitStructure.TIM_ICFilter = RF_CAPTURE_TIM_IC_FILTER;
    TIM_ICInit(TIM2, &TIM_ICInitStructure);

    TIM_ClearITPendingBit(TIM2, TIM_IT_CC1);
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    TIM_ClearFlag(TIM2, TIM_FLAG_Update);
    TIM_ITConfig(TIM2, TIM_IT_CC1 | TIM_IT_Update, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1u;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0u;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(TIM2, ENABLE);
}

void RF_Capture_Tick1msHandler(void) {
    g_rf_capture.MsTick++;
}

uint16_t RF_Capture_GetLastIntervalUs(void) {
    return g_rf_capture.LastIntervalUs;
}

void RF_Capture_TIM2_IRQHandler(void) {
    uint8_t had_cc1 = (TIM_GetITStatus(TIM2, TIM_IT_CC1) != RESET) ? 1u : 0u;

    if (had_cc1 != 0u) {
        uint16_t ccr = (uint16_t)TIM_GetCapture1(TIM2);
        uint16_t cnt_now = (uint16_t)TIM_GetCounter(TIM2);
        uint8_t update_pending = (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) ? 1u : 0u;
        uint32_t overflows_for_capture = g_rf_capture.Tim2OverflowCount;
        uint64_t stamp_ticks = 0ull;

        /*
         * Reliable extended timestamp:
         * - TIM2 update IRQ maintains Tim2OverflowCount for every 0xFFFF wrap.
         * - If UIF is pending and CCR <= current CNT, overflow happened before capture,
         *   so this capture belongs to the next timer cycle and needs +1 overflow.
         * - This avoids mis-decoding long gaps (>1 wrap) as short pulses.
         */
        if (update_pending != 0u && ccr <= cnt_now) {
            overflows_for_capture++;
        }

        stamp_ticks = RF_Capture_BuildStampTicks(overflows_for_capture, ccr);
        if (g_rf_capture.HasLastCaptureStamp == 0u) {
            g_rf_capture.LastCaptureStampTicks = stamp_ticks;
            g_rf_capture.HasLastCaptureStamp = 1u;
        } else {
            uint64_t delta_ticks = stamp_ticks - g_rf_capture.LastCaptureStampTicks;
            uint16_t pulse_us = 0u;
            if (delta_ticks > 0xFFFFFFFFull) {
                delta_ticks = 0xFFFFFFFFull;
            }
            g_rf_capture.LastCaptureStampTicks = stamp_ticks;
            pulse_us = RF_Capture_TicksToUs((uint32_t)delta_ticks);
            g_rf_capture.LastIntervalUs = pulse_us;
            RF_Capture_DetectPulse(pulse_us);
            g_rf_capture.LastEdgeMs = g_rf_capture.MsTick;
        }

        TIM_ClearITPendingBit(TIM2, TIM_IT_CC1);
        TIM2->CCER ^= TIM_CCER_CC1P;
    }

    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) {
        g_rf_capture.Tim2OverflowCount++;
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        TIM_ClearFlag(TIM2, TIM_FLAG_Update);
    }
}

#if !defined(RF_CAPTURE_DISABLE_TIM2_IRQ_BRIDGE)
/* Default behavior: export TIM2_IRQHandler and forward to RF capture handler. */
void TIM2_IRQHandler(void) {
    RF_Capture_TIM2_IRQHandler();
}
#endif

void RF_Capture_ProcessLoop(void) {
    static rf_frame_t frame_work;
    static uint8_t has_pending_tx = 0u;
    uint32_t now_ms = g_rf_capture.MsTick;
    uint32_t edge_ms = g_rf_capture.LastEdgeMs;
    uint16_t len_snapshot = g_rf_capture.CurrentFrame.len;
    uint8_t has_snapshot = 0u;

    if (has_pending_tx != 0u) {
        if (RF_Uart_SendFrame(&frame_work) != 0u) {
            has_pending_tx = 0u;
        } else {
            return;
        }
    }

    if (
        len_snapshot >= g_rf_capture.MinFramePulses &&
        (now_ms - edge_ms) > (uint32_t)g_rf_capture.IdleFlushMs
    ) {
        frame_work.len = len_snapshot;
        memcpy(frame_work.pulse, g_rf_capture.CurrentFrame.pulse, (size_t)len_snapshot * sizeof(uint16_t));

        __disable_irq();
        if (g_rf_capture.CurrentFrame.len == len_snapshot && g_rf_capture.LastEdgeMs == edge_ms) {
            g_rf_capture.CurrentFrame.len = 0u;
            g_rf_capture.LastEdgeMs = g_rf_capture.MsTick;
            has_snapshot = 1u;
        }
        __enable_irq();

        if (has_snapshot != 0u) {
            if (RF_Uart_SendFrame(&frame_work) == 0u) {
                has_pending_tx = 1u;
                return;
            }
        }
    }

    while (1) {
        uint8_t has_frame = 0u;
        has_frame = RF_Capture_DequeueFrame(&frame_work);
        if (has_frame == 0u) {
            break;
        }
        if (RF_Uart_SendFrame(&frame_work) == 0u) {
            has_pending_tx = 1u;
            break;
        }
    }
}
