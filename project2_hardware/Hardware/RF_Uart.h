#ifndef __RF_UART_H
#define __RF_UART_H

#include "stm32f10x.h"

#include "RF_Protocol.h"

void RF_Uart_Init(void);
void RF_Uart_SendByte(uint8_t Byte);
uint8_t RF_Uart_SendArray(const uint8_t *Array, uint16_t Length);
uint8_t RF_Uart_SendFrame(const rf_frame_t *Frame);
uint8_t RF_Uart_GetRxData(void);
uint8_t RF_Uart_GetRxFlag(void);

#endif
