#include "RF_Uart.h"

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"
#include "misc.h"

#define RF_UART_BAUDRATE 9600u
#define RF_UART_PACKET_MAX_LEN (2u + 2u + RF_BUFFER_SIZE * 2u + 1u)
#define RF_UART_TX_FIFO_SIZE 2304u

static volatile uint8_t g_uart_rx_data = 0u;
static volatile uint8_t g_uart_rx_flag = 0u;
static uint8_t g_uart_tx_packet[RF_UART_PACKET_MAX_LEN];
static uint8_t g_uart_tx_fifo[RF_UART_TX_FIFO_SIZE];
static volatile uint16_t g_uart_tx_head = 0u;
static volatile uint16_t g_uart_tx_tail = 0u;
static volatile uint32_t g_uart_tx_drop_frames = 0u;

static uint16_t RF_Uart_TxUsed(void) {
    const uint16_t head = g_uart_tx_head;
    const uint16_t tail = g_uart_tx_tail;
    if (head >= tail) {
        return (uint16_t)(head - tail);
    }
    return (uint16_t)(RF_UART_TX_FIFO_SIZE - (tail - head));
}

static uint16_t RF_Uart_TxFree(void) {
    return (uint16_t)(RF_UART_TX_FIFO_SIZE - RF_Uart_TxUsed() - 1u);
}

static uint8_t RF_Uart_TxEnqueue(const uint8_t *Array, uint16_t Length) {
    uint16_t i = 0u;
    uint16_t head = 0u;

    if (Array == NULL || Length == 0u) {
        return 0u;
    }
    if (Length >= RF_UART_TX_FIFO_SIZE) {
        g_uart_tx_drop_frames++;
        return 0u;
    }
    if (RF_Uart_TxFree() < Length) {
        g_uart_tx_drop_frames++;
        return 0u;
    }

    head = g_uart_tx_head;
    for (i = 0u; i < Length; ++i) {
        g_uart_tx_fifo[head] = Array[i];
        head++;
        if (head >= RF_UART_TX_FIFO_SIZE) {
            head = 0u;
        }
    }

    g_uart_tx_head = head;
    USART_ITConfig(USART1, USART_IT_TXE, ENABLE);
    return 1u;
}

static uint8_t RF_Uart_TxDequeueByte(uint8_t *Byte) {
    uint16_t tail = 0u;
    if (Byte == NULL) {
        return 0u;
    }
    if (g_uart_tx_tail == g_uart_tx_head) {
        return 0u;
    }

    tail = g_uart_tx_tail;
    *Byte = g_uart_tx_fifo[tail];
    tail++;
    if (tail >= RF_UART_TX_FIFO_SIZE) {
        tail = 0u;
    }
    g_uart_tx_tail = tail;
    return 1u;
}

void RF_Uart_Init(void) {
    GPIO_InitTypeDef gpio = {0};
    USART_InitTypeDef usart = {0};
    NVIC_InitTypeDef nvic = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    gpio.GPIO_Pin = GPIO_Pin_9;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate = RF_UART_BAUDRATE;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART1, &usart);

    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    nvic.NVIC_IRQChannel = USART1_IRQn;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    nvic.NVIC_IRQChannelPreemptionPriority = 1u;
    nvic.NVIC_IRQChannelSubPriority = 1u;
    NVIC_Init(&nvic);

    USART_Cmd(USART1, ENABLE);
}

void RF_Uart_SendByte(uint8_t Byte) {
    USART_SendData(USART1, Byte);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {
    }
}

uint8_t RF_Uart_SendArray(const uint8_t *Array, uint16_t Length) {
    return RF_Uart_TxEnqueue(Array, Length);
}

uint8_t RF_Uart_SendFrame(const rf_frame_t *Frame) {
    size_t n = 0u;
    if (Frame == NULL) {
        return 0u;
    }
    n = rf_proto_encode(Frame, g_uart_tx_packet, sizeof(g_uart_tx_packet));
    if (n > 0u && n <= 65535u) {
        return RF_Uart_SendArray(g_uart_tx_packet, (uint16_t)n);
    }
    return 0u;
}

uint8_t RF_Uart_GetRxData(void) {
    return g_uart_rx_data;
}

uint8_t RF_Uart_GetRxFlag(void) {
    if (g_uart_rx_flag != 0u) {
        g_uart_rx_flag = 0u;
        return 1u;
    }
    return 0u;
}

void USART1_IRQHandler(void) {
    if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET) {
        g_uart_rx_data = (uint8_t)USART_ReceiveData(USART1);
        g_uart_rx_flag = 1u;
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }

    if (USART_GetITStatus(USART1, USART_IT_TXE) == SET) {
        uint8_t tx_byte = 0u;
        if (RF_Uart_TxDequeueByte(&tx_byte) != 0u) {
            USART_SendData(USART1, tx_byte);
        } else {
            USART_ITConfig(USART1, USART_IT_TXE, DISABLE);
        }
    }
}
