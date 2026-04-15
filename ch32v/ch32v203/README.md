# CH32V203 — Guía específica (WHC-MCU)

Soporte para la familia **CH32V203** (QingKe V4B, RISC-V `rv32imac` con
`zicsr` + `zifencei`, hasta 144 MHz). Usa el **HAL oficial de WCH** en lugar
de bare-metal porque los periféricos del V20x (USB, ADC, TIM, etc.) son
suficientemente complejos como para que no valga la pena reescribirlos.

Este árbol fue creado para la placa [`buck_adc`](../../../pcbs/buck_adc) —
una PCB con CH32V203C8T6 + medición ADC de voltaje de baterías — pero sirve
para cualquier board con un CH32V203 en paquete C8/F6/F8/G6/G8/K8 (variante
**D6** del SDK).

---

## Tabla de contenidos

1. [Bootstrap inicial](#bootstrap-inicial)
2. [Layout del árbol](#layout-del-árbol)
3. [Compilar y flashear el primer proyecto](#compilar-y-flashear-el-primer-proyecto)
4. [Gotchas específicos del V203](#gotchas-específicos-del-v203)
5. [Hardware de referencia: `buck_adc`](#hardware-de-referencia-buck_adc)
6. [Crear un proyecto nuevo](#crear-un-proyecto-nuevo)
7. [Añadir módulos del HAL](#añadir-módulos-del-hal)

---

## Bootstrap inicial

El SDK de WCH **no se versiona** en este repo (son ~180 MB). Se regenera con
un script a partir del upstream oficial:

```bash
cd WHC-MCU
./scripts/bootstrap_ch32v203_sdk.sh
```

Qué hace:

1. Clona `https://github.com/openwch/ch32v20x` (depth 1) en
   `ch32v/ch32v203/_upstream/` (gitignored).
2. Copia a `ch32v/ch32v203/sdk/` únicamente los archivos que necesitamos:
   - `Core/{core_riscv.c,core_riscv.h,ch32v20x_conf.h,ch32v20x_it.c/h,system_ch32v20x.c/h}`
   - `Debug/{debug.c,debug.h}`
   - `Peripheral/inc/*` y `Peripheral/src/*`
   - `Startup/startup_ch32v20x_D6.S`
   - `Ld/Link.ld`
3. Parcha `system_ch32v20x.c` para usar **HSI interno** como fuente de reloj
   (ver [gotchas](#gotchas-específicos-del-v203)).

Tras ejecutar el script, `sdk/` queda listo para compilar cualquier proyecto
en `projects/`.

---

## Layout del árbol

```
ch32v/ch32v203/
├── README.md                  # este archivo
├── common/
│   └── Makefile.common        # incluye el HAL + startup + linker
├── sdk/                       # regenerable con scripts/bootstrap_ch32v203_sdk.sh
│   ├── Core/
│   ├── Debug/
│   ├── Peripheral/{inc,src}/
│   ├── Startup/startup_ch32v20x_D6.S
│   └── Ld/Link.ld
├── projects/
│   └── blink_pa1/             # primer test: parpadea PA1 del buck_adc
│       ├── Makefile           # 3 líneas
│       └── main.c
└── _upstream/                 # clon del repo openwch (gitignored)
```

---

## Compilar y flashear el primer proyecto

`blink_pa1` parpadea el LED D4 (PA1) del board `buck_adc` a ~2 Hz.

```bash
cd ch32v/ch32v203/projects/blink_pa1
make          # → build/blink_pa1.elf / .hex / .bin  (~1.1 KB text)
make flash    # → programa vía WCH-LinkE + OpenOCD
```

Salida esperada de `make flash`:

```
Info : WCH-LinkE  mode:RV version 2.11
Info : [wch_riscv.cpu.0] XLEN=32, misa=0x40901105
** Programming Started **
Info : device id = 0xd3a9abcd
Info : flash size = 64kbytes
** Programming Finished **
** Verified OK **
```

Después del reset el LED D4 debería parpadear. Si no parpadea, revisa la
sección de [hardware de referencia](#hardware-de-referencia-buck_adc).

---

## Gotchas específicos del V203

Errores con los que te vas a topar si no los sabes de antemano — están en
orden de "te va a pasar la primera vez":

### 1. `-march` debe incluir `zifencei`

El HAL usa `fence.i` en `core_riscv.h`. Sin `_zifencei` el assembler falla:

```
Error: unrecognized opcode `fence.i', extension `zifencei' required
```

Flag correcto (ya aplicado en `common/Makefile.common`):

```makefile
ARCH = -march=rv32imac_zicsr_zifencei -mabi=ilp32
```

### 2. Define `-DCH32V20x_D6`

El header `ch32v20x.h` necesita saber qué variante es. Para el C8T6 (y toda
la serie F6/F8/G6/G8/K8/C6/C8) el define es `CH32V20x_D6`. Las variantes
`D8` (CH32V203RBT) y `D8W` (CH32V208 con BLE) usan otros startups.

### 3. Startup correcto = `startup_ch32v20x_D6.S`

El upstream trae tres startups:

| Archivo                         | Variante            | MCUs                       |
|---------------------------------|---------------------|----------------------------|
| `startup_ch32v20x_D6.S`         | D6 (sin BLE, ≤64K)  | CH32V203F6/F8/G6/G8/K8/C6/C8 |
| `startup_ch32v20x_D8.S`         | D8 (128K)           | CH32V203RBT                |
| `startup_ch32v20x_D8W.S`        | D8W (con BLE)       | CH32V208                   |

El bootstrap script copia solo el D6. Si necesitas otra variante,
adáptalo.

### 4. Clock: el SDK asume HSE de 32 MHz

`system_ch32v20x.c` viene con `#define SYSCLK_FREQ_96MHz_HSE` activado y un
PLL configurado asumiendo `HSE_VALUE = 32000000`. La mayoría de los boards
reales (incluyendo `buck_adc`, que lleva un cristal de 8 MHz) no cumplen
esto → el PLL no se bloquea, el chip queda en HSI por el watchdog de
arranque, o peor.

**Solución aplicada automáticamente por el bootstrap**: forzar
`SYSCLK_FREQ_HSI` (8 MHz HSI directo, sin PLL). Suficiente para blink,
UART, ADC de baja velocidad.

Si necesitas más velocidad:
- Cambia a `SYSCLK_FREQ_*_HSI` (48/72/96/144 MHz desde HSI con PLL).
- O ajusta `HSE_VALUE` en `ch32v20x.h` al valor real de tu cristal y usa
  `SYSCLK_FREQ_*_HSE`.

### 5. GCC 15 ignora `interrupt("WCH-Interrupt-fast")`

Si usas interrupts (USB, TIM), GCC 15 silenciosamente compila los handlers
como funciones normales (terminadas en `ret` en vez de `mret`) y el CPU
crashea en la primera interrupción. Reemplaza el atributo:

```c
// antes
void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

// después
void TIM2_IRQHandler(void) __attribute__((interrupt("machine")));
```

Esto aplica a **todos** los handlers. Ver detalle en
[`SKILL.md`](../../SKILL.md#troubleshooting).

### 6. LEDs activos alto vs bajo

A diferencia de los boards de desarrollo oficiales de WCH (donde los LEDs
suelen ser activos en bajo, cátodo al GPIO), en `buck_adc` el LED **D4 está
en PA1 con ánodo al GPIO** (activo alto):

```c
GPIO_SetBits(GPIOA, GPIO_Pin_1);    // enciende
GPIO_ResetBits(GPIOA, GPIO_Pin_1);  // apaga
```

Cuando portees un ejemplo del upstream de WCH, invierte la lógica.

---

## Hardware de referencia: `buck_adc`

Proyecto KiCad en `../../../pcbs/buck_adc/` (fuera de este repo). Resumen:

| Elemento     | Pin/Net       | Nota                                  |
|--------------|---------------|---------------------------------------|
| MCU          | CH32V203C8T6  | LQFP48, 64K flash, 20K RAM           |
| HSE          | Y1 = 8 MHz    | cristal SMD 3.2 × 2.5 mm             |
| LED D4       | PA1, activo alto | verde, con R5 en serie a GND      |
| ADC_CH0      | red label en schematic | entrada de medición de batería |
| UART         | TX/RX globales | USART para debug print               |
| RS485_DE     | global label  | control de driver RS485              |
| SWD          | PA13 (SWDIO), PA14 (SWCLK) | conexión al WCH-LinkE  |

---

## Crear un proyecto nuevo

```bash
cd ch32v/ch32v203/projects
cp -r blink_pa1 mi_test
cd mi_test
# editar Makefile: cambiar TARGET = mi_test
$EDITOR main.c
make && make flash
```

El `Makefile` solo necesita tres líneas:

```makefile
TARGET = mi_test
SRCS_C = main.c other.c
include ../../common/Makefile.common
```

---

## Añadir módulos del HAL

Por defecto `common/Makefile.common` compila solo un subset del HAL para
minimizar tiempo de build:

```
core_riscv, system_ch32v20x, ch32v20x_it, debug,
ch32v20x_rcc, ch32v20x_gpio, ch32v20x_misc, ch32v20x_usart
```

Si tu proyecto necesita ADC, TIM, SPI, I2C, etc., añade el `.c`
correspondiente a la variable `SDK_SRCS_C` en
`common/Makefile.common`:

```makefile
SDK_SRCS_C = \
    $(SDK)/Core/core_riscv.c \
    ...
    $(SDK)/Peripheral/src/ch32v20x_adc.c \
    $(SDK)/Peripheral/src/ch32v20x_tim.c
```

Los headers ya están todos en `sdk/Peripheral/inc/` — solo hay que
`#include "ch32v20x_adc.h"` en tu `main.c`.
