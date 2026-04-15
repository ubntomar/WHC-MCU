/*
 * USB CDC + ADC — CH32V305FBP6 (CH32V305F-R0-1v0)
 *
 * Lee ADC1 canal 1 (PA1) cada 250 ms y envia la lectura como texto
 * por el puerto serie virtual USB CDC.
 *
 * Formato de salida:  "ADC:2048 1.650V\r\n"
 *
 * Conectar PA1 (header P1) a una senal DC de 0–3.3 V.
 * Conectar USB-C (P7) al PC. Aparece como /dev/ttyACMx.
 */

#include "ch32v30x_usbhs_device.h"
#include "debug.h"

/*********************************************************************
 * @fn      ADC1_Init
 * @brief   Configura ADC1 canal 1 (PA1) como entrada analogica.
 */
static void ADC1_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    ADC_InitTypeDef   ADC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    /* PA1 como entrada analogica */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* ADC1: independiente, canal unico, conversion por software */
    ADC_InitStructure.ADC_Mode               = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode       = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign          = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel       = 1;
    ADC_InitStructure.ADC_OutputBuffer       = ADC_OutputBuffer_Disable;
    ADC_InitStructure.ADC_Pga                = ADC_Pga_1;
    ADC_Init(ADC1, &ADC_InitStructure);

    ADC_Cmd(ADC1, ENABLE);

    /* Calibracion */
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));
}

/*********************************************************************
 * @fn      ADC1_Read
 * @brief   Dispara una conversion en canal 1 y devuelve el valor (0–4095).
 */
static uint16_t ADC1_Read(void)
{
    ADC_RegularChannelConfig(ADC1, ADC_Channel_1, 1, ADC_SampleTime_239Cycles5);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));
    return ADC_GetConversionValue(ADC1);
}

/*********************************************************************
 * @fn      itoa_pad
 * @brief   Convierte entero a string con padding de ceros a la izquierda.
 * @return  Numero de caracteres escritos.
 */
static int itoa_pad(uint32_t val, char *buf, int min_digits)
{
    char tmp[10];
    int i = 0;

    if (val == 0)
        tmp[i++] = '0';
    while (val > 0) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i < min_digits)
        tmp[i++] = '0';

    int len = i;
    while (i > 0)
        *buf++ = tmp[--i];
    return len;
}

int main(void)
{
    SystemCoreClockUpdate();
    Delay_Init();

    ADC1_Init();

    USBHS_RCC_Init();
    USBHS_Device_Init(ENABLE);

    char buf[48];

    while (1) {
        if (USBHS_CDC_Ready()) {
            uint16_t raw = ADC1_Read();
            uint32_t mV  = (uint32_t)raw * 3300 / 4095;
            uint32_t adj = mV + 100; /* +0.1V */

            /* Formato: "ADC:2048 1.650V 1.7V\r\n" */
            char *p = buf;
            *p++ = 'A'; *p++ = 'D'; *p++ = 'C'; *p++ = ':';
            p += itoa_pad(raw, p, 1);
            *p++ = ' ';
            p += itoa_pad(mV / 1000, p, 1);
            *p++ = '.';
            p += itoa_pad(mV % 1000, p, 3);
            *p++ = 'V';
            *p++ = ' ';
            p += itoa_pad(adj / 1000, p, 1);
            *p++ = '.';
            *p++ = '0' + (adj % 1000) / 100;
            *p++ = 'V';
            *p++ = '\r';
            *p++ = '\n';

            USBHS_CDC_Send((uint8_t *)buf, p - buf);
        }
        Delay_Ms(250);
    }
}
