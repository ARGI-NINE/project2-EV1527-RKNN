#include "rf_tx.h"

#include "stm32f10x.h"
#include "stm32f10x_rcc.h"

static GPIO_TypeDef *g_tx_port = NULL;
static uint16_t g_tx_pin = 0u;

static void rf_tx_enable_port_clock(GPIO_TypeDef *port) {
    if (port == GPIOA) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    } else if (port == GPIOB) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    } else if (port == GPIOC) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    } else if (port == GPIOD) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
    } else if (port == GPIOE) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);
    }
}

int rf_tx_init(GPIO_TypeDef *port, uint16_t pin) {
    GPIO_InitTypeDef gpio;
    if (port == NULL || pin == 0u) {
        return -1;
    }

    rf_tx_enable_port_clock(port);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = pin;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(port, &gpio);
    GPIO_ResetBits(port, pin);

    g_tx_port = port;
    g_tx_pin = pin;
    return 0;
}

void rf_tx_set_level(uint8_t level) {
    if (g_tx_port == NULL || g_tx_pin == 0u) {
        return;
    }
    if (level != 0u) {
        GPIO_SetBits(g_tx_port, g_tx_pin);
    } else {
        GPIO_ResetBits(g_tx_port, g_tx_pin);
    }
}

void rf_tx_delay_us(uint16_t us) {
    uint32_t i = 0u;
    uint32_t loops_per_us = SystemCoreClock / 8000000u;
    if (loops_per_us == 0u) {
        loops_per_us = 1u;
    }
    for (i = 0u; i < (uint32_t)us * loops_per_us; ++i) {
        __NOP();
    }
}

void rf_tx_replay(const uint16_t *pulse, uint16_t len) {
    uint16_t i = 0u;
    uint8_t level = 1u;
    if (pulse == NULL || len == 0u || g_tx_port == NULL || g_tx_pin == 0u) {
        return;
    }
    for (i = 0u; i < len; ++i) {
        rf_tx_set_level(level);
        rf_tx_delay_us(pulse[i]);
        level = (uint8_t)(1u - level);
    }
    rf_tx_set_level(0u);
}

void rf_tx_replay_frame(const rf_frame_t *frame) {
    if (frame == NULL) {
        return;
    }
    rf_tx_replay(frame->pulse, frame->len);
}
