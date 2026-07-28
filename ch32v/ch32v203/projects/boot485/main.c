/*
 * boot485 — Bootloader RS-485 para buck_adc (CH32V203C8T6)
 *
 * Vive en las primeras 2 páginas de flash (0x0000-0x1FFF, 8K) y es
 * INMUTABLE: jamás se borra a sí mismo. Mapa de flash v2:
 *   0x2000-0x9FFF  app (32K, trailer de validez en 0x9FF0) — zona OTA
 *   0xA000-0xEFFF  datalogger (20K, 5 páginas) — SOBREVIVE OTA
 *   0xF000         configuración — SOBREVIVE OTA
 * Solo la zona de app se borra al actualizar.
 *
 * Arranque:
 *   1. ¿Bandera "BOOT" en RAM (0x20004FF0, puesta por la app)? → quedarse
 *   2. Ventana de rescate: ~300 ms escuchando PING → si llega, quedarse
 *   3. ¿App válida (trailer magic+CRC32 en 0xEFF0)? → saltar a 0x2000
 *   4. App inválida → quedarse en modo actualización
 *
 * Protocolo (frames Modbus con FC usuario 0x42, siempre broadcast,
 * direccionados por UID de fábrica — funciona sin dirección asignada):
 *   sub 0x00 PING:   [00][42][00][uid12]                → [F8][42][00][uid12][ver2][estado1]
 *   sub 0x01 ERASE:  [00][42][01][uid12]                → ok (borra zona app, ~1 s)
 *   sub 0x02 WRITE:  [00][42][02][uid12][off4][len1][d] → ok (len par, ≤64)
 *   sub 0x03 COMMIT: [00][42][03][uid12][size4][crc4]   → verifica CRC32 y escribe trailer
 *   sub 0x04 REBOOT: [00][42][04][uid12]                → responde y resetea
 *   Respuestas de error: [F8][C2][sub][código][crc]
 *
 * LED D4: parpadeo muy rápido (~12 Hz) = en modo bootloader.
 */

#include "ch32v20x.h"
#include <string.h>

#define BL_VERSION   0x0102
#define FC_BOOT      0x42
#define BL_ID        0xF8u                /* byte de dirección en respuestas */

#define APP_BASE     0x08002000u          /* para el controlador de flash */
#define APP_EXEC     0x00002000u          /* alias de ejecución: el PC debe
                                             coincidir con las direcciones de
                                             enlace de la app (la/auipc son
                                             relativas al PC) */
#define APP_MAX      0x7FF0u              /* 32K - 16 de trailer */
#define TRAILER_ADDR 0x08009FF0u
#define TRAILER_MAGIC 0xA5B007A5u

#define BOOT_FLAG_ADDR  0x20004FF0u
#define BOOT_FLAG_MAGIC 0x424F4F54u       /* "BOOT" */

#define UID_BASE     0x1FFFF7E8u

/* marcadores de diagnóstico legibles por SWD (RAM libre, lejos de stack) */
#define DBG ((volatile uint32_t *)0x20002000u)  /* [0]=etapa [1]=bytes rx
                                                   [2]=frames [3]=aceptados
                                                   [4]=app_valid */

#define DE_PORT GPIOB
#define DE_PIN  GPIO_Pin_12

typedef struct {
    uint32_t magic;
    uint32_t size;
    uint32_t crc;
    uint32_t pad;
} trailer_t;

/* ---------- utilidades mínimas (sin printf, sin debug UART) ---------- */

static void delay_ms(uint32_t n)
{
    /* HSI 8 MHz, ~4 ciclos por vuelta */
    for (volatile uint32_t i = 0; i < n * 2000u; i++)
        ;
}

static uint16_t crc16(const uint8_t *buf, uint32_t len)
{
    uint16_t crc = 0xFFFF;

    for (uint32_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

static uint32_t crc32_calc(const uint8_t *p, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return ~crc;
}

/* ---------- hardware ---------- */

static void hw_init(void)
{
    GPIO_InitTypeDef  gpio  = {0};
    USART_InitTypeDef usart = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB,
                           ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_1;         /* LED */
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = DE_PIN;               /* DE/~RE en escucha */
    GPIO_Init(DE_PORT, &gpio);
    GPIO_ResetBits(DE_PORT, DE_PIN);

    gpio.GPIO_Pin   = GPIO_Pin_10;        /* USART3 TX */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    gpio.GPIO_Pin  = GPIO_Pin_11;         /* USART3 RX */
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &gpio);

    usart.USART_BaudRate            = 115200;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART3, &usart);
    USART_Cmd(USART3, ENABLE);
}

static void rs485_send(const uint8_t *buf, uint32_t len)
{
    GPIO_SetBits(DE_PORT, DE_PIN);
    for (volatile int i = 0; i < 40; i++)
        ;
    for (uint32_t i = 0; i < len; i++) {
        while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET)
            ;
        USART_SendData(USART3, buf[i]);
    }
    while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET)
        ;
    GPIO_ResetBits(DE_PORT, DE_PIN);
}

/* ---------- validación y salto a la app ---------- */

static int app_valid(void)
{
    const trailer_t *t = (const trailer_t *)TRAILER_ADDR;

    if (t->magic != TRAILER_MAGIC || t->size == 0 || t->size > APP_MAX)
        return 0;
    return crc32_calc((const uint8_t *)APP_BASE, t->size) == t->crc;
}

static void jump_app(void)
{
    /* dejar los periféricos usados en estado de reset */
    USART_Cmd(USART3, DISABLE);
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_USART3, ENABLE);
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_USART3, DISABLE);

    /* en APP_EXEC vive el "j handle_reset" del startup de la app */
    ((void (*)(void))APP_EXEC)();
}

/* ---------- protocolo ---------- */

static void bl_error(uint8_t sub, uint8_t code)
{
    uint8_t  r[6] = { BL_ID, FC_BOOT | 0x80, sub, code };
    uint16_t crc  = crc16(r, 4);

    r[4] = crc & 0xFF;
    r[5] = crc >> 8;
    rs485_send(r, 6);
}

static void bl_ok_hdr(uint8_t *r, uint8_t sub)
{
    r[0] = BL_ID;
    r[1] = FC_BOOT;
    r[2] = sub;
    memcpy(&r[3], (const void *)UID_BASE, 12);
}

/* devuelve 1 si el frame era un comando válido para esta tarjeta */
static int bl_handle(const uint8_t *f, uint32_t len)
{
    uint8_t  resp[24];
    uint16_t crc;

    if (len < 17 || crc16(f, len) != 0)
        return 0;
    if (f[0] != 0x00 || f[1] != FC_BOOT)
        return 0;
    if (memcmp(&f[3], (const void *)UID_BASE, 12))
        return 0;                         /* es para otra tarjeta */

    uint8_t sub = f[2];

    if (sub == 0x00 && len == 17) {       /* PING */
        bl_ok_hdr(resp, sub);
        resp[15] = BL_VERSION >> 8;
        resp[16] = BL_VERSION & 0xFF;
        resp[17] = app_valid() ? 1 : 0;
        crc = crc16(resp, 18);
        resp[18] = crc & 0xFF;
        resp[19] = crc >> 8;
        rs485_send(resp, 20);
        return 1;

    } else if (sub == 0x01 && len == 17) { /* ERASE zona app */
        FLASH_Unlock();
        for (uint32_t a = APP_BASE; a < 0x0800A000u; a += 4096) {
            if (FLASH_ErasePage(a) != FLASH_COMPLETE) {
                FLASH_Lock();
                bl_error(sub, 4);
                return 1;
            }
        }
        FLASH_Lock();
        bl_ok_hdr(resp, sub);
        crc = crc16(resp, 15);
        resp[15] = crc & 0xFF;
        resp[16] = crc >> 8;
        rs485_send(resp, 17);
        return 1;

    } else if (sub == 0x02 && len >= 24) { /* WRITE off4 len1 data */
        uint32_t off = ((uint32_t)f[15] << 24) | ((uint32_t)f[16] << 16) |
                       ((uint32_t)f[17] << 8) | f[18];
        uint8_t  n   = f[19];

        /* se permite escribir hasta APP_MAX+16: el trailer lo manda el
         * maestro como un WRITE normal (programar justo después de la
         * lectura larga del CRC corrompe el dato en este chip) */
        if (n == 0 || n > 64 || (n & 1) || (off & 1) ||
            off + n > APP_MAX + 16u || len != 22u + n) {
            bl_error(sub, 3);
            return 1;
        }
        FLASH_Unlock();
        for (uint8_t i = 0; i < n; i += 2) {
            uint16_t hw = f[20 + i] | ((uint16_t)f[21 + i] << 8);
            if (FLASH_ProgramHalfWord(APP_BASE + off + i, hw)
                    != FLASH_COMPLETE) {
                FLASH_Lock();
                bl_error(sub, 4);
                return 1;
            }
        }
        FLASH_Lock();
        bl_ok_hdr(resp, sub);
        memcpy(&resp[15], &f[15], 4);     /* eco del offset */
        crc = crc16(resp, 19);
        resp[19] = crc & 0xFF;
        resp[20] = crc >> 8;
        rs485_send(resp, 21);
        return 1;

    } else if (sub == 0x03 && len == 25) { /* COMMIT size4 crc4 */
        uint32_t size = ((uint32_t)f[15] << 24) | ((uint32_t)f[16] << 16) |
                        ((uint32_t)f[17] << 8) | f[18];
        uint32_t want = ((uint32_t)f[19] << 24) | ((uint32_t)f[20] << 16) |
                        ((uint32_t)f[21] << 8) | f[22];

        /* COMMIT = solo verificación: el trailer ya lo escribió el
         * maestro vía WRITE; aquí se lee todo y se confirma */
        const trailer_t *tr = (const trailer_t *)TRAILER_ADDR;

        if (size == 0 || size > APP_MAX) {
            bl_error(sub, 3);
            return 1;
        }
        if (crc32_calc((const uint8_t *)APP_BASE, size) != want ||
            tr->magic != TRAILER_MAGIC || tr->size != size ||
            tr->crc != want) {
            bl_error(sub, 5);             /* no cuadra: no se activa */
            return 1;
        }
        bl_ok_hdr(resp, sub);
        crc = crc16(resp, 15);
        resp[15] = crc & 0xFF;
        resp[16] = crc >> 8;
        rs485_send(resp, 17);
        return 1;

    } else if (sub == 0x04 && len == 17) { /* REBOOT */
        bl_ok_hdr(resp, sub);
        crc = crc16(resp, 15);
        resp[15] = crc & 0xFF;
        resp[16] = crc >> 8;
        rs485_send(resp, 17);
        delay_ms(5);
        NVIC_SystemReset();
    }
    return 0;
}

/* ---------- main ---------- */

int main(void)
{
    uint8_t  frame[96];
    uint32_t flen = 0, gap = 0, t = 0;
    uint32_t stay;

    hw_init();
    DBG[0] = 1; DBG[1] = 0; DBG[2] = 0; DBG[3] = 0;
    DBG[4] = app_valid();

    stay = (*(volatile uint32_t *)BOOT_FLAG_ADDR == BOOT_FLAG_MAGIC);
    *(volatile uint32_t *)BOOT_FLAG_ADDR = 0;

    /* lazo único: ventana de rescate de ~300 ms; si hay que quedarse,
     * el mismo lazo se vuelve el modo actualización (sin límite) */
    while (1) {
        if (USART_GetFlagStatus(USART3, USART_FLAG_RXNE) != RESET) {
            uint8_t c = (uint8_t)USART_ReceiveData(USART3);
            DBG[1]++;
            if (flen < sizeof(frame))
                frame[flen++] = c;
            gap = 0;
        } else {
            /* vuelta corta (~30 µs): el lazo DEBE ser más rápido que
             * un byte a 115200 (87 µs) o se pierden bytes en RX */
            for (volatile int i = 0; i < 20; i++)
                ;
            /* alimentar el IWDG heredado de la app: una vez iniciado no
             * se puede apagar NI con reset del sistema (solo con corte
             * de energía) — si no se alimenta, resetea la tarjeta en
             * plena actualización y corrompe la escritura */
            IWDG->CTLR = 0xAAAA;
            t++;
            if (flen && ++gap >= 220) {   /* fin de frame: ~7 ms */
                DBG[2]++;
                if (bl_handle(frame, flen)) {
                    DBG[3]++;
                    stay = 1;             /* nos están hablando */
                }
                flen = 0;
                gap  = 0;
            }
            if (!stay && t > 10000) {     /* ~300 ms de ventana */
                if (app_valid()) {
                    DBG[0] = 2;
                    jump_app();
                }
                DBG[0] = 3;
                stay = 1;                 /* app inválida: quedarse */
            }
            /* LED: parpadeo muy rápido = bootloader activo */
            GPIO_WriteBit(GPIOA, GPIO_Pin_1,
                          ((t / 1500) & 1) ? Bit_SET : Bit_RESET);
        }
    }
}
