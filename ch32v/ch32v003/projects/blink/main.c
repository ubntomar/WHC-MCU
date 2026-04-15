/*
 * Blink LED - Hola Mundo para CH32V003F4P6-R0-1v1
 *
 * Los LEDs de esta placa van al header P4 (NO están cableados a un GPIO fijo).
 * Conectar con jumper:
 *   P4 pin 1 (LED1) → PC3  (header P2)
 *   P4 pin 2 (LED2) → PC4  (header P2)
 *
 * LEDs son activos en bajo (LOW = encendido, HIGH = apagado).
 * Reloj: HSI interno a 24 MHz (default, sin configuración necesaria)
 */

#include "ch32v003.h"

#define LED1_PIN  3   /* PC3 */
#define LED2_PIN  4   /* PC4 */

static void delay(volatile uint32_t count)
{
    while (count--)
        ;
}

int main(void)
{
    /* 1. Habilitar reloj de GPIOC */
    RCC_APB2PCENR |= RCC_IOPCEN;

    /* 2. Configurar PC3 y PC4 como salida push-pull, 2 MHz */
    GPIOC_CFGLR &= ~(GPIO_CNF_MODE_MASK(LED1_PIN) | GPIO_CNF_MODE_MASK(LED2_PIN));
    GPIOC_CFGLR |= GPIO_MODE_OUT_PP_2MHZ(LED1_PIN) | GPIO_MODE_OUT_PP_2MHZ(LED2_PIN);

    /* Apagar ambos LEDs al inicio (HIGH = apagado, activo bajo) */
    GPIOC_BSHR = (1 << LED1_PIN) | (1 << LED2_PIN);

    while (1) {
        /* Encender LED1 (LOW), apagar LED2 (HIGH) */
        GPIOC_BSHR = (1 << (LED1_PIN + 16));   /* Reset PC3 = LED1 ON  */
        GPIOC_BSHR = (1 << LED2_PIN);           /* Set PC4   = LED2 OFF */
        delay(800000);

        /* Apagar LED1 (HIGH), encender LED2 (LOW) */
        GPIOC_BSHR = (1 << LED1_PIN);           /* Set PC3   = LED1 OFF */
        GPIOC_BSHR = (1 << (LED2_PIN + 16));    /* Reset PC4 = LED2 ON  */
        delay(800000);
    }
}
