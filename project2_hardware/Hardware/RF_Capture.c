#include <string.h>

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"
#include "misc.h"

#include "RF_Capture.h"
#include "RF_Protocol.h"
#include "RF_Uart.h"

#define RF_CAPTURE_READY_QUEUE_SIZE 3u

typedef struct {
    rf_frame_t CurrentFrame;
    rf_frame_t ReadyQueue[RF_CAPTURE_READY_QUEUE_SIZE];
    uint32_t TimerHz;
    uint16_t TimerPeriod;
    uint16_t LastCCR;
    uint8_t HasLastCCR;
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

static void RF_Capture_EnqueueFrame(const rf_frame_t *Frame) {
    uint8_t next = 0u;
    rf_frame_t *dst = NULL;

    if (Frame == NULL) {
        return;
    }
    if (Frame->len < g_rf_capture.MinFramePulses || Frame->len > RF_BUFFER_SIZE) {
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
    if (MinFramePulses > 0u) {
        g_rf_capture.MinFramePulses = MinFramePulses;
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
    g_rf_capture.MinPulseUs = 80u;
    g_rf_capture.MaxPulseUs = 60000u;
    g_rf_capture.SyncUs = 8000u;
    g_rf_capture.MinFramePulses = 48u;
    g_rf_capture.IdleFlushMs = RF_Capture_ComputeIdleFlushMs(g_rf_capture.SyncUs);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

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
    TIM_ICInitStructure.TIM_ICFilter = 0u;
    TIM_ICInit(TIM2, &TIM_ICInitStructure);

    TIM_ClearITPendingBit(TIM2, TIM_IT_CC1);
    TIM_ITConfig(TIM2, TIM_IT_CC1, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1u;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0u;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(TIM2, ENABLE);
}

void RF_Capture_SysTickHandler(void) {
    g_rf_capture.MsTick++;
}

void RF_Capture_TIM2_IRQHandler(void) {
    uint16_t ccr = 0u;
    uint32_t ticks = 0u;
    uint16_t pulse_us = 0u;

    if (TIM_GetITStatus(TIM2, TIM_IT_CC1) == RESET) {
        return;
    }

    ccr = (uint16_t)TIM_GetCapture1(TIM2);
    if (!g_rf_capture.HasLastCCR) {
        g_rf_capture.LastCCR = ccr;
        g_rf_capture.HasLastCCR = 1u;
    } else {
        if (ccr >= g_rf_capture.LastCCR) {
            ticks = (uint32_t)(ccr - g_rf_capture.LastCCR);
        } else {
            ticks = (uint32_t)g_rf_capture.TimerPeriod + 1u + (uint32_t)ccr - (uint32_t)g_rf_capture.LastCCR;
        }
        g_rf_capture.LastCCR = ccr;
        pulse_us = RF_Capture_TicksToUs(ticks);
        RF_Capture_DetectPulse(pulse_us);
        g_rf_capture.LastEdgeMs = g_rf_capture.MsTick;
    }

    TIM_ClearITPendingBit(TIM2, TIM_IT_CC1);
    TIM2->CCER ^= TIM_CCER_CC1P;
}

void RF_Capture_ProcessLoop(void) {
    static rf_frame_t frame_work;
    uint32_t now_ms = g_rf_capture.MsTick;
    uint32_t edge_ms = g_rf_capture.LastEdgeMs;
    uint16_t len_snapshot = g_rf_capture.CurrentFrame.len;
    uint8_t has_snapshot = 0u;

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
            __disable_irq();
            RF_Capture_EnqueueFrame(&frame_work);
            __enable_irq();
        }
    }

    while (1) {
        uint8_t has_frame = 0u;
        __disable_irq();
        has_frame = RF_Capture_DequeueFrame(&frame_work);
        __enable_irq();
        if (has_frame == 0u) {
            break;
        }
        RF_Uart_SendFrame(&frame_work);
    }
}
