#ifndef CH32V305_H
#define CH32V305_H

#include <stdint.h>

/* ---- RCC ---- */
#define RCC_BASE            0x40021000
#define RCC_APB2PCENR       (*(volatile uint32_t *)(RCC_BASE + 0x18))

#define RCC_IOPAEN          (1 << 2)
#define RCC_IOPBEN          (1 << 3)
#define RCC_IOPCEN          (1 << 4)
#define RCC_IOPDEN          (1 << 5)
#define RCC_IOPEEN          (1 << 6)

/* ---- GPIO Port A ---- */
#define GPIOA_BASE          0x40010800
#define GPIOA_CFGLR         (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_CFGHR         (*(volatile uint32_t *)(GPIOA_BASE + 0x04))
#define GPIOA_OUTDR         (*(volatile uint32_t *)(GPIOA_BASE + 0x0C))
#define GPIOA_BSHR          (*(volatile uint32_t *)(GPIOA_BASE + 0x10))

/* ---- GPIO Port B ---- */
#define GPIOB_BASE          0x40010C00
#define GPIOB_CFGLR         (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_CFGHR         (*(volatile uint32_t *)(GPIOB_BASE + 0x04))
#define GPIOB_OUTDR         (*(volatile uint32_t *)(GPIOB_BASE + 0x0C))
#define GPIOB_BSHR          (*(volatile uint32_t *)(GPIOB_BASE + 0x10))

/* ---- GPIO Port C ---- */
#define GPIOC_BASE          0x40011000
#define GPIOC_CFGLR         (*(volatile uint32_t *)(GPIOC_BASE + 0x00))
#define GPIOC_CFGHR         (*(volatile uint32_t *)(GPIOC_BASE + 0x04))
#define GPIOC_OUTDR         (*(volatile uint32_t *)(GPIOC_BASE + 0x0C))
#define GPIOC_BSHR          (*(volatile uint32_t *)(GPIOC_BASE + 0x10))

/* ---- GPIO Port D ---- */
#define GPIOD_BASE          0x40011400
#define GPIOD_CFGLR         (*(volatile uint32_t *)(GPIOD_BASE + 0x00))
#define GPIOD_CFGHR         (*(volatile uint32_t *)(GPIOD_BASE + 0x04))
#define GPIOD_OUTDR         (*(volatile uint32_t *)(GPIOD_BASE + 0x0C))
#define GPIOD_BSHR          (*(volatile uint32_t *)(GPIOD_BASE + 0x10))

/*
 * GPIO CFGLR: 4 bits per pin (pins 0-7)
 * GPIO CFGHR: 4 bits per pin (pins 8-15), use (pin - 8) for bit offset
 */
#define GPIO_CNF_MODE_MASK(pin)      (0xFU << ((pin) * 4))
#define GPIO_MODE_OUT_PP_2MHZ(pin)   (0x2U << ((pin) * 4))
#define GPIO_MODE_OUT_PP_10MHZ(pin)  (0x1U << ((pin) * 4))
#define GPIO_MODE_OUT_PP_50MHZ(pin)  (0x3U << ((pin) * 4))

#endif /* CH32V305_H */
