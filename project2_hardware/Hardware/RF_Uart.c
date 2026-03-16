#include "RF_Uart.h"

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"

static USART_TypeDef *g_uart = USART1;

void RF_Uart_Init(void) {
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_9;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    USART_StructInit(&usart);
    usart.USART_BaudRate = 115200u;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_Mode = USART_Mode_Tx;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART1, &usart);
    USART_Cmd(USART1, ENABLE);
    g_uart = USART1;
}

void RF_Uart_SendByte(uint8_t Byte) {
    if (g_uart == NULL) {
        return;
    }
    USART_SendData(g_uart, Byte);
    while (USART_GetFlagStatus(g_uart, USART_FLAG_TXE) == RESET) {
    }
}

void RF_Uart_SendArray(uint8_t *Array, uint16_t Length) {
    uint16_t i = 0u;
    if (Array == NULL || Length == 0u) {
        return;
    }
    for (i = 0u; i < Length; ++i) {
        RF_Uart_SendByte(Array[i]);
    }
    while (USART_GetFlagStatus(g_uart, USART_FLAG_TC) == RESET) {
    }
}

void RF_Uart_SendFrame(const rf_frame_t *Frame) {
    uint8_t packet[2u + 2u + RF_BUFFER_SIZE * 2u + 1u];
    size_t n = 0u;
    if (Frame == NULL) {
        return;
    }
    n = rf_proto_encode(Frame, packet, sizeof(packet));
    if (n > 0u && n <= 65535u) {
        RF_Uart_SendArray(packet, (uint16_t)n);
    }
}
