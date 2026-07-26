/*
 * ADC batería + reporte RS-485 — buck_adc (CH32V203C8T6)
 *
 * Propósito del sistema completo: leer el voltaje de la batería y
 * publicarlo por el bus RS-485 aislado. Debug en paralelo por USART2.
 *
 * Hardware:
 *   ADC_CH0: +VIN → R10 (47K) → R11 (10K) → GND, tap → R12 (1K) → PA0
 *            + C18 100nF de filtro. Divisor ÷5.7, Vref = VDDA = 3.3V
 *   USART3 → CA-IS3092W (RS-485): PB10 TX, PB11 RX, PB12 = DE/~RE
 *   USART2 → debug: PA2 TX, PA3 RX
 *   LED D4 → PA1 (heartbeat)
 *
 * Cada 1 s: promedia 16 muestras del ADC, calcula milivoltios de
 * batería y manda "VBAT=12.345V (raw=NNNN)" por RS-485 y por debug.
 */

#include "debug.h"
#include <stdio.h>

#define DE_PORT GPIOB
#define DE_PIN  GPIO_Pin_12

/* Divisor R10/R11: Vbat = Vadc * (47K+10K)/10K = Vadc * 5.7 */
#define VREF_MV      3300u
#define DIV_NUM      57u
#define DIV_DEN      10u
#define ADC_SAMPLES  16u

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

    gpio.GPIO_Pin   = DE_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(DE_PORT, &gpio);
    GPIO_ResetBits(DE_PORT, DE_PIN);

    gpio.GPIO_Pin   = GPIO_Pin_10;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    gpio.GPIO_Pin  = GPIO_Pin_11;
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
    GPIO_SetBits(DE_PORT, DE_PIN);
    Delay_Us(10);

    while (*s) {
        while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET)
            ;
        USART_SendData(USART3, (uint8_t)*s++);
    }
    while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET)
        ;

    GPIO_ResetBits(DE_PORT, DE_PIN);
}

/* ---------- ADC1 canal 0 (PA0) ---------- */

static void adc_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    ADC_InitTypeDef  adc  = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div2);        /* 8 MHz / 2 = 4 MHz ADCCLK */

    gpio.GPIO_Pin  = GPIO_Pin_0;             /* PA0 = ADC_CH0 */
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &gpio);

    adc.ADC_Mode               = ADC_Mode_Independent;
    adc.ADC_ScanConvMode       = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign          = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel       = 1;
    ADC_Init(ADC1, &adc);

    ADC_Cmd(ADC1, ENABLE);

    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1))
        ;
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1))
        ;
}

static uint16_t adc_read_raw(void)
{
    /* 239.5 ciclos de muestreo: ~60 µs @ 4 MHz, tolera la impedancia
       del divisor (≈9K) sin descargar C18 */
    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_239Cycles5);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET)
        ;
    return ADC_GetConversionValue(ADC1);
}

static uint16_t adc_read_avg(void)
{
    uint32_t acc = 0;

    for (uint32_t i = 0; i < ADC_SAMPLES; i++)
        acc += adc_read_raw();
    return (uint16_t)(acc / ADC_SAMPLES);
}

/* ---------- main ---------- */

int main(void)
{
    char line[64];

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    led_init();
    dbg_init(115200);
    rs485_init(115200);
    adc_init();

    dbg_puts("\r\n== buck_adc VBAT monitor (ADC1 ch0 en PA0, div 47K/10K) ==\r\n");

    while (1) {
        GPIO_SetBits(GPIOA, GPIO_Pin_1);

        uint16_t raw     = adc_read_avg();
        uint32_t adc_mv  = (uint32_t)raw * VREF_MV / 4095u;
        uint32_t vbat_mv = adc_mv * DIV_NUM / DIV_DEN;

        snprintf(line, sizeof(line), "VBAT=%lu.%03luV (raw=%u adc=%lumV)\r\n",
                 (unsigned long)(vbat_mv / 1000u),
                 (unsigned long)(vbat_mv % 1000u),
                 raw, (unsigned long)adc_mv);

        rs485_send(line);
        dbg_puts(line);

        GPIO_ResetBits(GPIOA, GPIO_Pin_1);
        Delay_Ms(900);
    }
}
