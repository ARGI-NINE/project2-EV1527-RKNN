#include "stm32f10x.h"
#include "RF_Capture.h"
#include "RF_Uart.h"
#include "Timer.h"

int main(void) {
    RF_Capture_Init();
    RF_Uart_Init();
    Timer_Init();

    while (1) {
        RF_Capture_ProcessLoop();
    }
}
