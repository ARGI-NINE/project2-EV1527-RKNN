#ifndef __RF_CAPTURE_H
#define __RF_CAPTURE_H

#include "stm32f10x.h"

void RF_Capture_Init(void);
void RF_Capture_SetFilter(
    uint16_t MinPulseUs,
    uint16_t MaxPulseUs,
    uint16_t SyncUs,
    uint16_t MinFramePulses
);
void RF_Capture_ProcessLoop(void);
void RF_Capture_Tick1msHandler(void);
void RF_Capture_TIM2_IRQHandler(void);
uint16_t RF_Capture_GetLastIntervalUs(void);

#endif
