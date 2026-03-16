#ifndef RF_TX_H
#define RF_TX_H

#include <stdint.h>

#include "stm32f10x_gpio.h"

#include "../common/rf_protocol.h"

/* Configure TX GPIO as push-pull output and drive low. */
int rf_tx_init(GPIO_TypeDef *port, uint16_t pin);

/* Blocking replay helpers for bare-metal standard library projects. */
void rf_tx_set_level(uint8_t level);
void rf_tx_delay_us(uint16_t us);
void rf_tx_replay(const uint16_t *pulse, uint16_t len);
void rf_tx_replay_frame(const rf_frame_t *frame);

#endif
