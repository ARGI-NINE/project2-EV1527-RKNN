#ifndef RF_UART_H
#define RF_UART_H

#include <stdint.h>

#include "stm32f10x_usart.h"

#include "../common/rf_protocol.h"

/* Initialize USART1 TX (PA9) with SPL and bind it as RF UART output. */
void rf_uart_hw_init_usart1_tx(uint32_t baudrate);

/* Bind any initialized USART instance as RF UART output. */
void rf_uart_bind(USART_TypeDef *uart);

/* Blocking TX helpers (bare-metal, no DMA/RTOS). */
void rf_uart_send_bytes(const uint8_t *data, uint16_t len);
void rf_uart_send_frame(const rf_frame_t *frame);

#endif
