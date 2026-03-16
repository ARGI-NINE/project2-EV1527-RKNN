#ifndef __RF_TX_H
#define __RF_TX_H

#include <stdint.h>

#include "stm32f10x_gpio.h"

#include "RF_Protocol.h"

void RF_Tx_Init(GPIO_TypeDef *Port, uint16_t Pin);
void RF_Tx_SetLevel(uint8_t Level);
void RF_Tx_DelayUs(uint16_t Us);
void RF_Tx_Replay(const uint16_t *Pulse, uint16_t Length);
void RF_Tx_ReplayFrame(const rf_frame_t *Frame);

#endif
