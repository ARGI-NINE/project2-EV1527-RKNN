#include "stm32f10x.h"

#include "Delay.h"

void Delay_us(uint32_t xus) {
    uint32_t cycles = (SystemCoreClock / 8000000u) * xus;
    if (cycles == 0u) {
        cycles = 1u;
    }
    while (cycles--) {
        __NOP();
    }
}

void Delay_ms(uint32_t xms) {
    while (xms--) {
        Delay_us(1000u);
    }
}

void Delay_s(uint32_t xs) {
    while (xs--) {
        Delay_ms(1000u);
    }
}
