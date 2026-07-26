/*
 * UART test — buck_adc (CH32V203C8T6)
 *
 * Hardware:
 *   USART2: PA2 = TX (test point TX1), PA3 = RX (test point RX1)
 *   LED D4 (verde) → PA1 (activo alto), parpadea como heartbeat
 *
 * Comportamiento:
 *   - Cada 1 s envía "buck_adc uart test #N" por USART2 @ 115200 8N1
 *   - Eco: todo byte recibido se devuelve como "echo: 'X'"
 *
 * Nota: USART1 (PA9/PA10) queda libre — en esta placa es el RS-485.
 */

#include "debug.h"
#include <stdio.h>

static void led_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_1;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &gpio);
}

static void uart2_init(uint32_t baud)
{
    GPIO_InitTypeDef  gpio  = {0};
    USART_InitTypeDef usart = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_2;            /* PA2 = TX */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin  = GPIO_Pin_3;             /* PA3 = RX */
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate            = baud;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART2, &usart);

    USART_Cmd(USART2, ENABLE);
}

static void uart2_putc(char c)
{
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET)
        ;
    USART_SendData(USART2, (uint8_t)c);
}

static void uart2_puts(const char *s)
{
    while (*s)
        uart2_putc(*s++);
}

int main(void)
{
    char     line[48];
    uint32_t n = 0;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    led_init();
    uart2_init(115200);

    uart2_puts("\r\nbuck_adc CH32V203 UART2 listo @115200\r\n");

    while (1) {
        GPIO_SetBits(GPIOA, GPIO_Pin_1);

        snprintf(line, sizeof(line), "buck_adc uart test #%lu\r\n",
                 (unsigned long)n++);
        uart2_puts(line);

        /* ~1 s de espera atendiendo el eco en trozos de 10 ms */
        for (int i = 0; i < 100; i++) {
            if (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET) {
                char c = (char)USART_ReceiveData(USART2);
                snprintf(line, sizeof(line), "echo: '%c' (0x%02X)\r\n",
                         (c >= 32 && c < 127) ? c : '.', (unsigned)c);
                uart2_puts(line);
            }
            if (i == 10)
                GPIO_ResetBits(GPIOA, GPIO_Pin_1);
            Delay_Ms(10);
        }
    }
}
