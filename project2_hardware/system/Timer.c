#include <stdint.h>

#include "stm32f10x.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"
#include "misc.h"

#include "Timer.h"
#include "RF_Capture.h"

#define TIMER_TICK_HZ 1000u
#define TIMER_PERIOD_US (1000000u / TIMER_TICK_HZ)

static volatile uint32_t g_time_base_us = 0u;

void Timer_Init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    TIM_InternalClockConfig(TIM3);

    TIM_TimeBaseInitTypeDef tim_base = {0};
    tim_base.TIM_ClockDivision = TIM_CKD_DIV1;
    tim_base.TIM_CounterMode = TIM_CounterMode_Up;
    tim_base.TIM_Prescaler = (uint16_t)(72u - 1u); /* 72MHz -> 1MHz */
    tim_base.TIM_Period = (uint16_t)(TIMER_PERIOD_US - 1u);
    tim_base.TIM_RepetitionCounter = 0u;
    TIM_TimeBaseInit(TIM3, &tim_base);

    TIM_ClearFlag(TIM3, TIM_FLAG_Update);
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    NVIC_InitTypeDef nvic = {0};
    nvic.NVIC_IRQChannel = TIM3_IRQn;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    nvic.NVIC_IRQChannelPreemptionPriority = 1u;
    nvic.NVIC_IRQChannelSubPriority = 1u;
    NVIC_Init(&nvic);

    g_time_base_us = 0u;
    TIM_Cmd(TIM3, ENABLE);
}

uint32_t Timer_NowUs(void)
{
    uint32_t base_before = 0u;
    uint32_t base_after = 0u;
    uint16_t cnt = 0u;

    do {
        base_before = g_time_base_us;
        cnt = (uint16_t)TIM_GetCounter(TIM3);
        base_after = g_time_base_us;
    } while (base_before != base_after);

    if (TIM_GetFlagStatus(TIM3, TIM_FLAG_Update) != RESET) {
        base_before += TIMER_PERIOD_US;
    }

    return base_before + (uint32_t)cnt;
}

void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) == SET) {
        g_time_base_us += TIMER_PERIOD_US;
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
        RF_Capture_Tick1msHandler();
    }
}