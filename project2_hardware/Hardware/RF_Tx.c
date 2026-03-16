#include "RF_Tx.h"

#include "stm32f10x.h"
#include "stm32f10x_rcc.h"

static GPIO_TypeDef *g_tx_port = NULL;
static uint16_t g_tx_pin = 0u;

static void RF_Tx_EnablePortClock(GPIO_TypeDef *Port) {
    if (Port == GPIOA) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    } else if (Port == GPIOB) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    } else if (Port == GPIOC) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    } else if (Port == GPIOD) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
    } else if (Port == GPIOE) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);
    }
}

void RF_Tx_Init(GPIO_TypeDef *Port, uint16_t Pin) {
    GPIO_InitTypeDef gpio;
    if (Port == NULL || Pin == 0u) {
        return;
    }

    RF_Tx_EnablePortClock(Port);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = Pin;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(Port, &gpio);
    GPIO_ResetBits(Port, Pin);

    g_tx_port = Port;
    g_tx_pin = Pin;
}

void RF_Tx_SetLevel(uint8_t Level) {
    if (g_tx_port == NULL || g_tx_pin == 0u) {
        return;
    }
    if (Level != 0u) {
        GPIO_SetBits(g_tx_port, g_tx_pin);
    } else {
        GPIO_ResetBits(g_tx_port, g_tx_pin);
    }
}

void RF_Tx_DelayUs(uint16_t Us) {
    uint32_t i = 0u;
    uint32_t loops_per_us = SystemCoreClock / 8000000u;
    if (loops_per_us == 0u) {
        loops_per_us = 1u;
    }
    for (i = 0u; i < (uint32_t)Us * loops_per_us; ++i) {
        __NOP();
    }
}

void RF_Tx_Replay(const uint16_t *Pulse, uint16_t Length) {
    uint16_t i = 0u;
    uint8_t level = 1u;
    if (Pulse == NULL || Length == 0u || g_tx_port == NULL || g_tx_pin == 0u) {
        return;
    }
    for (i = 0u; i < Length; ++i) {
        RF_Tx_SetLevel(level);
        RF_Tx_DelayUs(Pulse[i]);
        level = (uint8_t)(1u - level);
    }
    RF_Tx_SetLevel(0u);
}

void RF_Tx_ReplayFrame(const rf_frame_t *Frame) {
    if (Frame == NULL) {
        return;
    }
    RF_Tx_Replay(Frame->pulse, Frame->len);
}
