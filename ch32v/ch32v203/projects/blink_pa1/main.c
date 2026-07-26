/*
 * Blink LED — buck_adc (CH32V203C8T6)
 *
 * Hardware:
 *   LED D4 (verde) → PA1 → R5 → GND  (activo alto)
 *
 * Reloj: HSI 8 MHz (configurado en system_ch32v20x.c con SYSCLK_FREQ_HSI)
 */

#include "debug.h"

static void led_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_1;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &gpio);

    GPIO_ResetBits(GPIOA, GPIO_Pin_1);
}

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    led_init();

    while (1) {
        GPIO_SetBits(GPIOA, GPIO_Pin_1);
        Delay_Ms(100);
        GPIO_ResetBits(GPIOA, GPIO_Pin_1);
        Delay_Ms(100);
    }
}
