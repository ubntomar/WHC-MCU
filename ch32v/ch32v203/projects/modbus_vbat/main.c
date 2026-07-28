/*
 * Monitor de batería Modbus RTU — buck_adc (CH32V203C8T6)
 *
 * FIRMWARE DE PRODUCTO: binario idéntico para todas las tarjetas.
 * La identidad (dirección Modbus) y la calibración se aprovisionan por
 * el propio bus y persisten en la última página de flash (0x0800F000).
 * Sin configurar arranca en la dirección 247 con calibración neutra.
 *
 * Modbus RTU esclavo @ 115200 8N1 sobre RS-485 (USART3, DE=PB12):
 *
 *   Input registers (FC 0x04):
 *     0  VBAT en mV (calibrado)
 *     1  ADC crudo (promedio de 16)
 *     2  voltaje en el pin ADC en mV
 *     3  versión de firmware (0x0100 = v1.0)
 *     4..9  UID de fábrica de 96 bits (ESIG), 6 palabras
 *     10 causa del último reset (bit0=PIN bit1=POR bit2=SOFT
 *        bit3=IWDG bit4=WWDG bit5=LOWPWR)
 *
 *   Holding registers (FC 0x03 / 0x06 / 0x10):
 *     0  dirección esclavo (1-247)
 *     1  factor de calibración ×10000 (10000 = neutro)
 *     2  clave de guardado: escribir 0xA55A persiste addr+cal en flash
 *     3  prueba de watchdog: escribir 0xDEAD cuelga el firmware a
 *        propósito → el IWDG debe resetear la tarjeta en ~6.5 s
 *
 * WATCHDOG: IWDG por LSI (~40 kHz /64, recarga 4095) ≈ 6.5 s. Se
 * alimenta en el lazo principal; cualquier cuelgue → reset automático.
 * La causa del reset queda en el input reg 10 (y en el banner de debug).
 *
 * El esclavo jamás transmite sin ser interrogado → bus sin colisiones
 * por diseño (el maestro sondea cada dirección).
 *
 * AUTO-DESCUBRIMIENTO (FC usuario 0x41, siempre a broadcast addr 0):
 *   sub 0x01 QUERY:  [00][41][01][nbits][prefijo 12B][crc]
 *     Las tarjetas SIN CONFIGURAR (addr 247) cuyo UID empiece con ese
 *     prefijo de nbits responden [F7][41][01][uid 12B][fw 2B][crc].
 *     Varias a la vez → colisión (CRC roto en el maestro) → el maestro
 *     parte la rama del árbol de bits y desciende hasta aislar cada una.
 *   sub 0x02 ASSIGN: [00][41][02][uid 12B][addr][crc]
 *     La tarjeta con UID exacto (configurada o no) adopta la dirección,
 *     la persiste y responde [addr][41][02][uid 12B][crc].
 *
 * Hardware: ADC PA0 (divisor 47K/10K), RS-485 USART3 PB10/PB11/PB12,
 * debug USART2 PA2/PA3, LED D4 PA1 (destello por transacción válida).
 */

#include "debug.h"
#include <stdio.h>
#include <string.h>

#define FW_VERSION   0x0104
#define HANG_KEY     0xDEADu
#define FC_DISCOVERY 0x41

/*
 * LED D4 = indicador de estado/fase:
 *   Sin configurar (addr 247): parpadeo rápido continuo 5 Hz
 *   Configurada en espera:     latido corto (40 ms) cada 2 s
 *   Transacción atendida:      flash de 40 ms (sondeo activo = ritmo)
 */
#define DE_PORT      GPIOB
#define DE_PIN       GPIO_Pin_12

#define VREF_MV      3300u
#define DIV_NUM      57u
#define DIV_DEN      10u
#define ADC_SAMPLES  16u

#define CFG_ADDR     0x0800F000u          /* última página de 4K */
#define CFG_MAGIC    0xB007u
#define DEFAULT_ADDR 247u
#define DEFAULT_CAL  10000u
#define SAVE_KEY     0xA55Au

#define UID_BASE     0x1FFFF7E8u          /* ESIG: UID 96 bits */

/* Fin de frame RTU: silencio de 4 ms (3.5 chars @115200 son 0.3 ms,
 * pero los maestros USB-RS485 meten huecos de ~1-2 ms entre bytes) */
#define GAP_TICKS    80                   /* 80 × 50 µs = 4 ms */

typedef struct {
    uint16_t magic;
    uint16_t addr;
    uint16_t cal;
    uint16_t check;                       /* magic ^ addr ^ cal */
} cfg_t;

static uint16_t g_addr = DEFAULT_ADDR;
static uint16_t g_cal  = DEFAULT_CAL;
static uint16_t g_reset_cause;            /* bitfield, ver input reg 10 */
static uint8_t  g_hang_request;           /* prueba de watchdog pedida */

/* ---------- watchdog ---------- */

static void iwdg_init(void)
{
    /* LSI ~40 kHz / 64 = 625 Hz; recarga 4095 → timeout ≈ 6.5 s */
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    IWDG_SetReload(0xFFF);
    IWDG_ReloadCounter();
    IWDG_Enable();
}

static void reset_cause_init(void)
{
    g_reset_cause =
        (RCC_GetFlagStatus(RCC_FLAG_PINRST)  == SET ? 1u << 0 : 0) |
        (RCC_GetFlagStatus(RCC_FLAG_PORRST)  == SET ? 1u << 1 : 0) |
        (RCC_GetFlagStatus(RCC_FLAG_SFTRST)  == SET ? 1u << 2 : 0) |
        (RCC_GetFlagStatus(RCC_FLAG_IWDGRST) == SET ? 1u << 3 : 0) |
        (RCC_GetFlagStatus(RCC_FLAG_WWDGRST) == SET ? 1u << 4 : 0) |
        (RCC_GetFlagStatus(RCC_FLAG_LPWRRST) == SET ? 1u << 5 : 0);
    RCC_ClearFlag();                      /* que el próximo boot sea limpio */
}

/* ---------- config en flash ---------- */

static void cfg_load(void)
{
    const cfg_t *c = (const cfg_t *)CFG_ADDR;

    if (c->magic == CFG_MAGIC &&
        c->check == (uint16_t)(c->magic ^ c->addr ^ c->cal) &&
        c->addr >= 1 && c->addr <= 247 && c->cal >= 5000 && c->cal <= 20000) {
        g_addr = c->addr;
        g_cal  = c->cal;
    }
}

static int cfg_save(void)
{
    cfg_t c = {
        .magic = CFG_MAGIC,
        .addr  = g_addr,
        .cal   = g_cal,
        .check = (uint16_t)(CFG_MAGIC ^ g_addr ^ g_cal),
    };
    const uint16_t *src = (const uint16_t *)&c;

    FLASH_Unlock();
    if (FLASH_ErasePage(CFG_ADDR) != FLASH_COMPLETE) {
        FLASH_Lock();
        return -1;
    }
    for (uint32_t i = 0; i < sizeof(c) / 2; i++) {
        if (FLASH_ProgramHalfWord(CFG_ADDR + i * 2, src[i]) != FLASH_COMPLETE) {
            FLASH_Lock();
            return -1;
        }
    }
    FLASH_Lock();
    return memcmp((const void *)CFG_ADDR, &c, sizeof(c)) ? -1 : 0;
}

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

static void rs485_send(const uint8_t *buf, uint32_t len)
{
    GPIO_SetBits(DE_PORT, DE_PIN);
    Delay_Us(10);

    for (uint32_t i = 0; i < len; i++) {
        while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET)
            ;
        USART_SendData(USART3, buf[i]);
    }
    while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET)
        ;

    GPIO_ResetBits(DE_PORT, DE_PIN);
}

/* ---------- ADC ---------- */

static void adc_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    ADC_InitTypeDef  adc  = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div2);

    gpio.GPIO_Pin  = GPIO_Pin_0;
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

static uint16_t adc_read_avg(void)
{
    uint32_t acc = 0;

    for (uint32_t i = 0; i < ADC_SAMPLES; i++) {
        ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1,
                                 ADC_SampleTime_239Cycles5);
        ADC_SoftwareStartConvCmd(ADC1, ENABLE);
        while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET)
            ;
        acc += ADC_GetConversionValue(ADC1);
    }
    return (uint16_t)(acc / ADC_SAMPLES);
}

/* ---------- Modbus RTU ---------- */

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

static uint16_t input_reg(uint16_t idx)
{
    const uint16_t *uid = (const uint16_t *)UID_BASE;

    switch (idx) {
    case 0: {
        uint16_t raw     = adc_read_avg();
        uint32_t adc_mv  = (uint32_t)raw * VREF_MV / 4095u;
        uint32_t vbat_mv = adc_mv * DIV_NUM / DIV_DEN * g_cal / 10000u;
        return (uint16_t)vbat_mv;
    }
    case 1: return adc_read_avg();
    case 2: return (uint16_t)((uint32_t)adc_read_avg() * VREF_MV / 4095u);
    case 3: return FW_VERSION;
    case 4: case 5: case 6: case 7: case 8: case 9:
        return uid[idx - 4];
    case 10: return g_reset_cause;
    default: return 0;
    }
}

static uint16_t holding_read(uint16_t idx)
{
    switch (idx) {
    case 0: return g_addr;
    case 1: return g_cal;
    default: return 0;                    /* save_key se lee como 0 */
    }
}

/* devuelve 0 ok, o código de excepción Modbus */
static uint8_t holding_write(uint16_t idx, uint16_t val)
{
    switch (idx) {
    case 0:
        if (val < 1 || val > 247)
            return 3;                     /* illegal data value */
        g_addr = val;
        return 0;
    case 1:
        if (val < 5000 || val > 20000)
            return 3;
        g_cal = val;
        return 0;
    case 2:
        if (val != SAVE_KEY)
            return 3;
        return cfg_save() ? 4 : 0;        /* 4 = slave device failure */
    case 3:
        if (val != HANG_KEY)
            return 3;
        g_hang_request = 1;               /* se cuelga tras responder */
        return 0;
    default:
        return 2;                         /* illegal data address */
    }
}

static int uid_prefix_match(uint8_t nbits, const uint8_t *prefix)
{
    const uint8_t *uid  = (const uint8_t *)UID_BASE;
    uint8_t        full = nbits / 8, rem = nbits % 8;

    if (nbits > 96)
        return 0;
    if (memcmp(uid, prefix, full))
        return 0;
    if (rem) {
        uint8_t mask = (uint8_t)(0xFF << (8 - rem));
        if ((uid[full] ^ prefix[full]) & mask)
            return 0;
    }
    return 1;
}

/* devuelve 1 si esta tarjeta respondió (para el LED) */
static int mb_discovery(const uint8_t *f, uint32_t len)
{
    const uint8_t *uid = (const uint8_t *)UID_BASE;
    uint8_t        resp[20];
    uint16_t       crc;

    if (len != 18)
        return 0;

    if (f[2] == 0x01) {                   /* QUERY: solo sin configurar */
        if (g_addr != DEFAULT_ADDR || !uid_prefix_match(f[3], &f[4]))
            return 0;
        resp[0] = DEFAULT_ADDR;
        resp[1] = FC_DISCOVERY;
        resp[2] = 0x01;
        memcpy(&resp[3], uid, 12);
        resp[15] = FW_VERSION >> 8;
        resp[16] = FW_VERSION & 0xFF;
        crc = crc16(resp, 17);
        resp[17] = crc & 0xFF;
        resp[18] = crc >> 8;
        rs485_send(resp, 19);
        return 1;

    } else if (f[2] == 0x02) {            /* ASSIGN por UID exacto */
        uint8_t na = f[15];

        if (memcmp(&f[3], uid, 12) || na < 1 || na > 246)
            return 0;
        g_addr = na;
        if (cfg_save())
            return 0;                     /* falló flash: silencio */
        resp[0] = na;
        resp[1] = FC_DISCOVERY;
        resp[2] = 0x02;
        memcpy(&resp[3], uid, 12);
        crc = crc16(resp, 15);
        resp[15] = crc & 0xFF;
        resp[16] = crc >> 8;
        rs485_send(resp, 17);
        return 1;
    }
    return 0;
}

static void mb_exception(uint8_t addr, uint8_t fc, uint8_t code)
{
    uint8_t  r[5] = { addr, (uint8_t)(fc | 0x80), code };
    uint16_t crc  = crc16(r, 3);

    r[3] = crc & 0xFF;
    r[4] = crc >> 8;
    rs485_send(r, 5);
}

/* devuelve 1 si el frame era para esta tarjeta (para el LED) */
static int mb_handle(const uint8_t *f, uint32_t len)
{
    uint8_t resp[8 + 2 * 32];

    if (len < 4 || crc16(f, len) != 0)    /* CRC sobre todo = 0 si ok */
        return 0;
    if (f[0] != g_addr && f[0] != 0)
        return 0;                          /* no es para mí */

    uint8_t  addr = f[0], fc = f[1];
    uint8_t  bcast = (addr == 0);

    /* En el bus también se oyen las RESPUESTAS de otros esclavos. Con
     * direcciones duplicadas (varias tarjetas en 247) parecerían
     * peticiones y generarían un ping-pong infinito de excepciones.
     * Reglas anti-eco: */
    if (fc & 0x80)
        return 0;                     /* frame de excepción = respuesta ajena */
    if (fc == FC_DISCOVERY)
        return bcast ? mb_discovery(f, len) : 0;  /* unicast 0x41 = respuesta */
    if (!bcast && g_addr == DEFAULT_ADDR)
        return 0;                     /* sin configurar: solo discovery/bcast */

    if ((fc == 0x03 || fc == 0x04) && len == 8 && !bcast) {
        uint16_t start = (f[2] << 8) | f[3];
        uint16_t count = (f[4] << 8) | f[5];

        if (count < 1 || count > 32 || start + count > 11) {
            mb_exception(addr, fc, 2);
            return 1;
        }
        resp[0] = addr;
        resp[1] = fc;
        resp[2] = (uint8_t)(count * 2);
        for (uint16_t i = 0; i < count; i++) {
            uint16_t v = (fc == 0x04) ? input_reg(start + i)
                                      : holding_read(start + i);
            resp[3 + 2 * i] = v >> 8;
            resp[4 + 2 * i] = v & 0xFF;
        }
        uint16_t crc = crc16(resp, 3 + count * 2);
        resp[3 + count * 2] = crc & 0xFF;
        resp[4 + count * 2] = crc >> 8;
        rs485_send(resp, 5 + count * 2);

    } else if (fc == 0x06 && len == 8) {
        uint16_t reg = (f[2] << 8) | f[3];
        uint16_t val = (f[4] << 8) | f[5];
        uint8_t  err = holding_write(reg, val);

        if (bcast)
            return 1;                       /* broadcast: sin respuesta */
        if (err) {
            mb_exception(addr, fc, err);
            return 1;
        }
        memcpy(resp, f, 6);               /* eco de la petición */
        uint16_t crc = crc16(resp, 6);
        resp[6] = crc & 0xFF;
        resp[7] = crc >> 8;
        rs485_send(resp, 8);

    } else if (fc == 0x10 && len >= 11) {
        uint16_t start = (f[2] << 8) | f[3];
        uint16_t count = (f[4] << 8) | f[5];
        uint8_t  bytes = f[6];

        if (count < 1 || count > 3 || bytes != count * 2 ||
            len != 9u + bytes) {
            if (!bcast) mb_exception(addr, fc, 2);
            return 1;
        }
        for (uint16_t i = 0; i < count; i++) {
            uint16_t val = (f[7 + 2 * i] << 8) | f[8 + 2 * i];
            uint8_t  err = holding_write(start + i, val);
            if (err) {
                if (!bcast) mb_exception(addr, fc, err);
                return 1;
            }
        }
        if (bcast)
            return 1;
        resp[0] = addr; resp[1] = fc;
        resp[2] = f[2]; resp[3] = f[3];
        resp[4] = f[4]; resp[5] = f[5];
        uint16_t crc = crc16(resp, 6);
        resp[6] = crc & 0xFF;
        resp[7] = crc >> 8;
        rs485_send(resp, 8);

    } else if (!bcast) {
        mb_exception(addr, fc, 1);        /* illegal function */
    }
    return 1;
}

/* ---------- main ---------- */

int main(void)
{
    uint8_t  frame[64];
    uint32_t flen = 0, gap = 0;
    uint32_t ms = 0, tick = 0, flash_until = 0;
    char     line[80];

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    led_init();
    dbg_init(115200);
    rs485_init(115200);
    adc_init();
    cfg_load();
    reset_cause_init();
    iwdg_init();

    {
        const uint16_t *uid = (const uint16_t *)UID_BASE;
        snprintf(line, sizeof(line),
                 "\r\n== modbus_vbat v%u.%u  addr=%u cal=%u"
                 "  uid=%04X%04X%04X%04X%04X%04X  rst=0x%02X%s ==\r\n",
                 FW_VERSION >> 8, FW_VERSION & 0xFF,
                 g_addr, g_cal, uid[5], uid[4], uid[3],
                 uid[2], uid[1], uid[0], g_reset_cause,
                 (g_reset_cause & (1u << 3)) ? " (WATCHDOG!)" : "");
        dbg_puts(line);
    }

    while (1) {
        if (USART_GetFlagStatus(USART3, USART_FLAG_RXNE) != RESET) {
            uint8_t c = (uint8_t)USART_ReceiveData(USART3);
            if (flen < sizeof(frame))
                frame[flen++] = c;
            gap = 0;
        } else {
            Delay_Us(50);
            if (++tick >= 20) {           /* ~1 ms de reloj para el LED */
                tick = 0;
                ms++;
            }
            IWDG_ReloadCounter();         /* alimentar el perro */

            if (flen && ++gap >= GAP_TICKS) {
                uint16_t old_addr = g_addr, old_cal = g_cal;

                if (mb_handle(frame, flen))
                    flash_until = ms + 40;

                if (g_hang_request) {      /* prueba de watchdog */
                    dbg_puts("[test] cuelgue intencional — "
                             "esperando reset del IWDG...\r\n");
                    GPIO_SetBits(GPIOA, GPIO_Pin_1);
                    while (1)
                        ;                  /* sin alimentar el perro */
                }

                if (g_addr != old_addr || g_cal != old_cal) {
                    snprintf(line, sizeof(line),
                             "[cfg] addr=%u cal=%u\r\n", g_addr, g_cal);
                    dbg_puts(line);
                }
                flen = 0;
                gap  = 0;
            }

            /* LED según estado/fase */
            uint8_t on;
            if ((int32_t)(flash_until - ms) > 0)
                on = 1;                            /* transacción */
            else if (g_addr == DEFAULT_ADDR)
                on = (ms % 200) < 100;             /* sin configurar: 5 Hz */
            else
                on = (ms % 2000) < 40;             /* latido cada 2 s */
            GPIO_WriteBit(GPIOA, GPIO_Pin_1, on ? Bit_SET : Bit_RESET);
        }
    }
}
