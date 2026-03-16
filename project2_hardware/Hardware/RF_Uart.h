#ifndef __RF_UART_H
#define __RF_UART_H

#include "stm32f10x.h"

#include "RF_Protocol.h"

void RF_Uart_Init(void);
void RF_Uart_SendByte(uint8_t Byte);
void RF_Uart_SendArray(uint8_t *Array, uint16_t Length);
void RF_Uart_SendFrame(const rf_frame_t *Frame);

#endif
