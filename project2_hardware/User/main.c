#include "stm32f10x.h"
#include "RF_Capture.h"
#include "RF_Uart.h"

int main(void) {
    RF_Uart_Init();
    RF_Capture_Init();
    SysTick_Config(SystemCoreClock / 1000u);

    while (1) {
        RF_Capture_ProcessLoop();
    }
}
