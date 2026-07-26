/*
 * RS-485 test — buck_adc (CH32V203C8T6)
 *
 * Hardware:
 *   USART3 → CA-IS3092W aislado: PB10 = TX → DI, PB11 = RX ← RO
 *   PB12   → DE + ~RE (juntos): alto = transmitir, bajo = recibir
 *   USART2 → debug: PA2 = TX (TX1), PA3 = RX (RX1) @ 115200
 *   LED D4 → PA1 (activo alto), heartbeat
 *
 * Comportamiento:
 *   - Cada 1 s transmite "buck_adc rs485 #N" por el bus @ 115200
 *   - En reposo escucha el bus; cada byte recibido se reporta por
 *     debug y se devuelve como "echo: 'X'" por el bus
 *   - Todo lo que pasa se narra por el UART de debug
 *
 * Gotcha RS-485: antes de bajar DE hay que esperar el flag TC
 * (transmisión completa en el cable), no TXE (registro vacío) —
 * si no, el último byte se corta.
 */

#include "debug.h"
#include <stdio.h>

#define DE_PORT GPIOB
#define DE_PIN  GPIO_Pin_12

/* ---------- LED ---------- */

static void led_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_1;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &gpio);
}

/* ---------- USART2: debug ---------- */

static void dbg_init(uint32_t baud)
{
    GPIO_InitTypeDef  gpio  = {0};
    USART_InitTypeDef usart = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_2;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin  = GPIO_Pin_3;
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

static void dbg_puts(const char *s)
{
    while (*s) {
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET)
            ;
        USART_SendData(USART2, (uint8_t)*s++);
    }
}

/* ---------- USART3: RS-485 ---------- */

static void rs485_init(uint32_t baud)
{
    GPIO_InitTypeDef  gpio  = {0};
    USART_InitTypeDef usart = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    /* DE/~RE en bajo desde el arranque: modo escucha */
    gpio.GPIO_Pin   = DE_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(DE_PORT, &gpio);
    GPIO_ResetBits(DE_PORT, DE_PIN);

    gpio.GPIO_Pin   = GPIO_Pin_10;           /* PB10 = TX → DI */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    gpio.GPIO_Pin  = GPIO_Pin_11;            /* PB11 = RX ← RO */
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &gpio);

    usart.USART_BaudRate            = baud;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART3, &usart);
    USART_Cmd(USART3, ENABLE);
}

static void rs485_send(const char *s)
{
    GPIO_SetBits(DE_PORT, DE_PIN);           /* driver ON */
    Delay_Us(10);                            /* margen de habilitación */

    while (*s) {
        while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET)
            ;
        USART_SendData(USART3, (uint8_t)*s++);
    }
    while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET)
        ;                                    /* último bit en el cable */

    GPIO_ResetBits(DE_PORT, DE_PIN);         /* volver a escucha */
}

/* ---------- main ---------- */

int main(void)
{
    char     line[64];
    uint32_t n = 0;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    led_init();
    dbg_init(115200);
    rs485_init(115200);

    dbg_puts("\r\n== buck_adc RS-485 test (USART3 PB10/PB11, DE=PB12) ==\r\n");

    while (1) {
        GPIO_SetBits(GPIOA, GPIO_Pin_1);

        snprintf(line, sizeof(line), "buck_adc rs485 #%lu\r\n",
                 (unsigned long)n);
        rs485_send(line);

        snprintf(line, sizeof(line), "[dbg] tx bus #%lu\r\n",
                 (unsigned long)n++);
        dbg_puts(line);

        /* ~1 s escuchando el bus en trozos de 10 ms */
        for (int i = 0; i < 100; i++) {
            if (USART_GetFlagStatus(USART3, USART_FLAG_RXNE) != RESET) {
                char c = (char)USART_ReceiveData(USART3);

                snprintf(line, sizeof(line), "[dbg] rx bus: '%c' (0x%02X)\r\n",
                         (c >= 32 && c < 127) ? c : '.', (unsigned)c);
                dbg_puts(line);

                snprintf(line, sizeof(line), "echo: '%c'\r\n",
                         (c >= 32 && c < 127) ? c : '.');
                rs485_send(line);
            }
            if (i == 10)
                GPIO_ResetBits(GPIOA, GPIO_Pin_1);
            Delay_Ms(10);
        }
    }
}
