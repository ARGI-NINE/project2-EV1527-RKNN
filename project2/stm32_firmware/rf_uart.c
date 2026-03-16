#include "rf_uart.h"

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

static USART_TypeDef *g_rf_uart = USART1;

void rf_uart_bind(USART_TypeDef *uart) {
    if (uart == NULL) {
        return;
    }
    g_rf_uart = uart;
}

void rf_uart_hw_init_usart1_tx(uint32_t baudrate) {
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;

    if (baudrate == 0u) {
        baudrate = 115200u;
    }

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_9;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    USART_StructInit(&usart);
    usart.USART_BaudRate = baudrate;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_Mode = USART_Mode_Tx;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART1, &usart);
    USART_Cmd(USART1, ENABLE);
    rf_uart_bind(USART1);
}

void rf_uart_send_bytes(const uint8_t *data, uint16_t len) {
    uint16_t i = 0u;
    if (g_rf_uart == NULL || data == NULL || len == 0u) {
        return;
    }
    for (i = 0u; i < len; ++i) {
        while (USART_GetFlagStatus(g_rf_uart, USART_FLAG_TXE) == RESET) {
        }
        USART_SendData(g_rf_uart, data[i]);
    }
    while (USART_GetFlagStatus(g_rf_uart, USART_FLAG_TC) == RESET) {
    }
}

void rf_uart_send_frame(const rf_frame_t *frame) {
    uint8_t packet[2u + 2u + RF_BUFFER_SIZE * 2u + 1u];
    size_t n = 0u;
    if (frame == NULL) {
        return;
    }
    n = rf_proto_encode(frame, packet, sizeof(packet));
    if (n > 0u && n <= 65535u) {
        rf_uart_send_bytes(packet, (uint16_t)n);
    }
}
