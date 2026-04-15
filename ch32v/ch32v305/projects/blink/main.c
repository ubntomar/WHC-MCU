/*
 * Blink LED - Hola Mundo para CH32V305FBP6 (CH32V305F-R0-1v0)
 *
 * Los LEDs van al header P4 (no cableados a GPIO fijo).
 * Conectar con jumper:
 *   P4 pin 1 (LED1) → PB6  (header derecho P2)
 *   P4 pin 2 (LED2) → PB7  (header derecho P2)
 *
 * LEDs son activos en bajo (LOW = encendido, HIGH = apagado).
 * Reloj: HSI interno a 8 MHz (default, sin PLL)
 */

#include "ch32v305.h"

#define LED1_PIN  6   /* PB6 */
#define LED2_PIN  7   /* PB7 */

static void delay(volatile uint32_t count)
{
    while (count--)
        ;
}

int main(void)
{
    /* 1. Habilitar reloj de GPIOB */
    RCC_APB2PCENR |= RCC_IOPBEN;

    /* 2. Configurar PB6 y PB7 como salida push-pull, 2 MHz */
    GPIOB_CFGLR &= ~(GPIO_CNF_MODE_MASK(LED1_PIN) | GPIO_CNF_MODE_MASK(LED2_PIN));
    GPIOB_CFGLR |= GPIO_MODE_OUT_PP_2MHZ(LED1_PIN) | GPIO_MODE_OUT_PP_2MHZ(LED2_PIN);

    /* Apagar ambos LEDs al inicio (HIGH = apagado, activo bajo) */
    GPIOB_BSHR = (1 << LED1_PIN) | (1 << LED2_PIN);

    while (1) {
        /* Encender LED1 (LOW), apagar LED2 (HIGH) */
        GPIOB_BSHR = (1 << (LED1_PIN + 16));   /* Reset PB6 = LED1 ON  */
        GPIOB_BSHR = (1 << LED2_PIN);           /* Set PB7   = LED2 OFF */
        delay(400000);

        /* Apagar LED1 (HIGH), encender LED2 (LOW) */
        GPIOB_BSHR = (1 << LED1_PIN);           /* Set PB6   = LED1 OFF */
        GPIOB_BSHR = (1 << (LED2_PIN + 16));    /* Reset PB7 = LED2 ON  */
        delay(400000);
    }
}
