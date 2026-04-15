#ifndef CH32V003_H
#define CH32V003_H

#include <stdint.h>

/* ---- RCC ---- */
#define RCC_BASE            0x40021000
#define RCC_APB2PCENR       (*(volatile uint32_t *)(RCC_BASE + 0x18))

#define RCC_IOPAEN          (1 << 2)
#define RCC_IOPCEN          (1 << 4)
#define RCC_IOPDEN          (1 << 5)

/* ---- GPIO Port A ---- */
#define GPIOA_BASE          0x40010800
#define GPIOA_CFGLR         (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_OUTDR         (*(volatile uint32_t *)(GPIOA_BASE + 0x0C))
#define GPIOA_BSHR          (*(volatile uint32_t *)(GPIOA_BASE + 0x10))

/* ---- GPIO Port C ---- */
#define GPIOC_BASE          0x40011000
#define GPIOC_CFGLR         (*(volatile uint32_t *)(GPIOC_BASE + 0x00))
#define GPIOC_OUTDR         (*(volatile uint32_t *)(GPIOC_BASE + 0x0C))
#define GPIOC_BSHR          (*(volatile uint32_t *)(GPIOC_BASE + 0x10))

/* ---- GPIO Port D ---- */
#define GPIOD_BASE          0x40011400
#define GPIOD_CFGLR         (*(volatile uint32_t *)(GPIOD_BASE + 0x00))
#define GPIOD_OUTDR         (*(volatile uint32_t *)(GPIOD_BASE + 0x0C))
#define GPIOD_BSHR          (*(volatile uint32_t *)(GPIOD_BASE + 0x10))

/* GPIO CFGLR mode/cnf fields (4 bits per pin in low register, pins 0-7) */
#define GPIO_CNF_MODE_MASK(pin)  (0xF << ((pin) * 4))
#define GPIO_MODE_OUT_PP_2MHZ(pin) (0x2 << ((pin) * 4))  /* Push-pull, 2 MHz */
#define GPIO_MODE_OUT_PP_10MHZ(pin) (0x1 << ((pin) * 4)) /* Push-pull, 10 MHz */

#endif /* CH32V003_H */
