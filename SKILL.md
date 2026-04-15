# WCH MCU - Guia de compilacion y flasheo

## Estructura del repositorio

```
WHC-MCU/
├── ch32v/                          Familia RISC-V CH32V
│   ├── ch32v003/                   Subfamilia V003
│   │   ├── common/                 Archivos compartidos (startup, .h, .ld, Makefile.common)
│   │   └── projects/
│   │       └── blink/              Blink LED bare-metal (PC3/PC4)
│   ├── ch32v305/                   Subfamilia V305
│   │   ├── common/                 Archivos para proyectos bare-metal simples
│   │   ├── sdk/                    WCH SDK (perifericos, startup, linker)
│   │   │   ├── Core/              core_riscv.c/h
│   │   │   ├── Debug/             debug.c/h (Delay_Init, USART debug)
│   │   │   ├── Peripheral/        inc/ + src/ (rcc, gpio, usart, usb, tim, dma...)
│   │   │   ├── Startup/           startup_ch32v30x_D8C.S
│   │   │   └── Ld/               Link.ld (128K Flash, 32K RAM)
│   │   └── projects/
│   │       ├── blink/              Blink LED bare-metal (PB6/PB7)
│   │       ├── usb_cdc/            USB CDC puente USB↔UART2 (con SDK)
│   │       └── usb_cdc_echo/       USB CDC eco directo por USB (con SDK)
│   └── ch32v307/                   Subfamilia V307 (pendiente)
├── ch5xx/                          Familia 8051 CH5xx (pendiente)
│   └── ch552/
└── SKILL.md                        Este archivo
```

## Toolchain instalado

| Herramienta             | Ruta                            | Version |
|-------------------------|---------------------------------|---------|
| RISC-V GCC (xPack)     | `/opt/riscv-toolchain/bin/`     | 15.2.0  |
| WCH OpenOCD             | `/opt/wch-openocd/bin/`         | 1.92    |
| Config OpenOCD (RISC-V) | `/opt/wch-openocd/bin/wch-riscv.cfg` | - |

PATH configurado en `~/.bashrc`:
```bash
export PATH="/opt/riscv-toolchain/bin:$PATH"
export PATH="/opt/wch-openocd/bin:$PATH"
```

## Programador

**WCH-LinkE** (USB VID:PID `1a86:8010` en modo RISC-V)

Pinout del conector:
```
SWCLK/TCK  (no usado en modo single-wire)
SWDIO/TMS  --> conectar al pin de debug del MCU
GND        --> GND de la placa
3V3        --> VCC/3V3 de la placa
5V         (no usado)
```

Reglas udev: `/etc/udev/rules.d/99-wch-link.rules`

---

## CH32V003 (QingKe V2A)

### Placa: CH32V003F4P6-R0-1v1

| Parametro    | Valor                    |
|--------------|--------------------------|
| Core         | QingKe V2A (RISC-V)     |
| Arquitectura | `rv32ec`                 |
| Clock        | HSI 24 MHz (default)     |
| Flash        | 16 KB @ 0x08000000       |
| RAM          | 2 KB @ 0x20000000        |
| Package      | TSSOP20                  |

### Conexion WCH-LinkE

| WCH-LinkE | Placa (header derecho P2) |
|------------|---------------------------|
| 3V3        | VCC (pin 1)               |
| GND        | GND (pin 2)               |
| SWDIO/TMS  | PD1 (pin 3) - single-wire debug |

### LEDs (header P4)

Los LEDs NO estan cableados a un GPIO fijo. Son **activos en bajo** (LOW = encendido).
Conectar con jumper desde P4 al GPIO deseado:

| P4   | Jumper a | Header |
|------|----------|--------|
| LED1 | PC3      | P2     |
| LED2 | PC4      | P2     |

### Compilacion y flasheo

```bash
cd ~/WHC-MCU/ch32v/ch32v003/projects/blink
make          # Compilar
make flash    # Flashear via WCH-LinkE
make clean    # Limpiar build
make erase    # Borrar toda la flash
```

### Flags del compilador

```makefile
ARCH = -march=rv32ec_zicsr -mabi=ilp32e
```

### Crear un nuevo proyecto

```bash
mkdir ~/WHC-MCU/ch32v/ch32v003/projects/mi_proyecto
```

Crear `main.c` con `#include "ch32v003.h"` y un `Makefile` minimo:

```makefile
TARGET  = mi_proyecto
SRCS_C  = main.c
include ../../common/Makefile.common
```

### Archivos common

| Archivo          | Funcion                                    |
|------------------|--------------------------------------------|
| `ch32v003.h`     | Registros RCC, GPIOA/C/D (CFGLR, BSHR...) |
| `ch32v003.ld`    | Linker script (16K Flash, 2K RAM)          |
| `startup.S`      | Vector table + init (.data, .bss) + main   |
| `Makefile.common`| Reglas de build, flash, erase, clean       |

### GPIO rapido

```c
// 1. Habilitar reloj del puerto
RCC_APB2PCENR |= RCC_IOPCEN;    // GPIOC
RCC_APB2PCENR |= RCC_IOPDEN;    // GPIOD

// 2. Configurar pin como salida push-pull 2MHz
GPIOC_CFGLR &= ~GPIO_CNF_MODE_MASK(3);       // Limpiar PC3
GPIOC_CFGLR |=  GPIO_MODE_OUT_PP_2MHZ(3);    // PC3 salida

// 3. Escribir pin
GPIOC_BSHR = (1 << 3);          // Set PC3 (HIGH)
GPIOC_BSHR = (1 << (3 + 16));   // Reset PC3 (LOW)
```

---

## CH32V305 (QingKe V4F)

### Placa: CH32V305F-R0-1v0

| Parametro    | Valor                           |
|--------------|---------------------------------|
| Core         | QingKe V4F (RISC-V + FPU)      |
| Arquitectura | `rv32imafcxw`                   |
| Clock        | HSI 8 MHz (default), PLL hasta 144 MHz |
| Flash        | 128 KB @ 0x08000000             |
| RAM          | 32 KB @ 0x20000000              |
| Package      | LQFP48                          |
| Extras       | USB OTG FS/HS, Ethernet 10M, CAN |

### Pinout de headers (verificado con fotos de la placa)

**Header P1 (izquierdo, doble fila):**

| Col izq | Col der |
|---------|---------|
| 3V3     | 3V3     |
| PA1     | PA5     |
| NRST    | PB10    |
| PB11    | PB12    |
| PB13    | PB14    |
| PB15    | PC6     |
| PC7     | GND     |

**Header P2 (derecho, doble fila):**

| Col izq | Col der |
|---------|---------|
| 3V3     | 3V3     |
| PA9     | GND     |
| PB7     | PA13    |
| PB6     | PA14    |
| PC9     | PA8     |
| 3V3     | PC8     |
| GND     | GND     |

**Header P4:** LED1, LED2 (activos en bajo, no cableados a GPIO fijo)

**Conectores USB:** P7 (USB-C, device), P6 (USB-A, host)

**Resumen de GPIOs accesibles en headers:**

| Puerto | Pines en headers                          |
|--------|------------------------------------------|
| PA     | PA1, PA5, PA8, PA9, PA13*, PA14*         |
| PB     | PB6**, PB7**, PB10-PB15                  |
| PC     | PC6, PC7, PC8, PC9                       |

`*` PA13/PA14 = SWDIO/SWCLK (debug). `**` PB6/PB7 = USB D-/D+ (compartidos con LEDs).

> **Pines NO accesibles en headers**: PA0, PA2, PA3, PA4, PA6, PA7,
> PB0, PB1, PB2-PB9 (excepto PB6/PB7), PC0-PC5. Estan en el chip
> LQFP48 pero la placa no los saca a ningun header.

### Conexion WCH-LinkE

| WCH-LinkE | Placa (header P2)                   |
|------------|-------------------------------------|
| 3V3        | 3V3 (pin superior)                  |
| GND        | GND                                 |
| SWDIO/TMS  | PA13 (SWDIO) - single-wire debug    |

### LEDs (header P4)

Mismo esquema que V003: LEDs **activos en bajo**, no cableados a GPIO fijo.

| P4   | Jumper a | Header |
|------|----------|--------|
| LED1 | PB6      | P2     |
| LED2 | PB7      | P2     |

### Compilacion y flasheo

```bash
cd ~/WHC-MCU/ch32v/ch32v305/projects/blink
make          # Compilar
make flash    # Flashear via WCH-LinkE
make clean    # Limpiar build
make erase    # Borrar toda la flash
```

### Flags del compilador

Proyectos bare-metal simples (usando `common/`):
```makefile
ARCH = -march=rv32imac_zicsr -mabi=ilp32
```

Proyectos con SDK (USB, perifericos complejos):
```makefile
ARCH = -march=rv32imac_zicsr_zifencei -mabi=ilp32
```

> `_zifencei` es requerido por el SDK (usa instruccion `fence.i`).
> Para habilitar FPU: `-march=rv32imafc_zicsr_zifencei -mabi=ilp32f`

### Tipos de proyecto

**Bare-metal simple** (blink, GPIO, etc.) — usa `common/`:
```bash
mkdir ~/WHC-MCU/ch32v/ch32v305/projects/mi_proyecto
```
```makefile
TARGET  = mi_proyecto
SRCS_C  = main.c
include ../../common/Makefile.common
```

**Proyecto con SDK** (USB, DMA, timers, etc.) — Makefile standalone:
- Usa el SDK en `../../sdk/` (perifericos, startup, linker script)
- Makefile propio con rutas a SDK, flags `-DCH32V30x_D8C`, `-nostartfiles`
- Ver `usb_cdc_echo/Makefile` como referencia

### Archivos common (bare-metal)

| Archivo          | Funcion                                         |
|------------------|------------------------------------------------|
| `ch32v305.h`     | Registros RCC, GPIOA/B/C/D (CFGLR, CFGHR, BSHR) |
| `ch32v305.ld`    | Linker script (128K Flash, 32K RAM)            |
| `startup.S`      | Vector table + init (.data, .bss) + main       |
| `Makefile.common`| Reglas de build, flash, erase, clean           |

### SDK (perifericos complejos)

| Directorio       | Contenido                                       |
|------------------|-------------------------------------------------|
| `sdk/Core/`      | `core_riscv.c/h` — funciones base del core      |
| `sdk/Debug/`     | `debug.c/h` — Delay_Init, Delay_Ms, Delay_Us    |
| `sdk/Peripheral/`| Drivers: RCC, GPIO, USART, TIM, DMA, USB, etc. |
| `sdk/Startup/`   | `startup_ch32v30x_D8C.S` — vector table + init  |
| `sdk/Ld/`        | `Link.ld` — linker script con todas las secciones |

Flags adicionales requeridos para proyectos con SDK:
```makefile
DEFS    = -DCH32V30x_D8C           # Variante D8C (V305/V307 con USBHS+ETH)
CFLAGS += -Wno-attributes           # Silenciar warnings de atributos WCH
CFLAGS += -nostdlib                 # Evitar conflicto con crt0
LDFLAGS += -nostartfiles            # Usar startup del SDK, no el del toolchain
LDFLAGS += --specs=nano.specs --specs=nosys.specs  # libc minima
# IMPORTANTE: -lc -lm -lnosys deben ir DESPUES de los objetos en el link
```

### GPIO rapido

```c
// 1. Habilitar reloj del puerto
RCC_APB2PCENR |= RCC_IOPBEN;    // GPIOB

// 2. Configurar pin como salida (CFGLR para pines 0-7, CFGHR para 8-15)
GPIOB_CFGLR &= ~GPIO_CNF_MODE_MASK(6);       // Limpiar PB6
GPIOB_CFGLR |=  GPIO_MODE_OUT_PP_2MHZ(6);    // PB6 salida push-pull

// Para pines 8-15 usar CFGHR con offset (pin - 8):
// GPIOA_CFGHR &= ~GPIO_CNF_MODE_MASK(9 - 8);
// GPIOA_CFGHR |=  GPIO_MODE_OUT_PP_2MHZ(9 - 8);

// 3. Escribir pin
GPIOB_BSHR = (1 << 6);          // Set PB6 (HIGH)
GPIOB_BSHR = (1 << (6 + 16));   // Reset PB6 (LOW)
```

### USB (USBHS — High Speed 480 Mbps)

El CH32V305 tiene un periférico USBHS con PHY integrado en **PB6 (D-)** y **PB7 (D+)**.

| Conector en placa | Tipo   | Funcion       | Pines    |
|-------------------|--------|---------------|----------|
| P7 (USB-C)        | Device | Conectar a PC | PB6/PB7  |
| P6 (USB-A)        | Host   | Conectar perifericos | PB6/PB7 |

> **IMPORTANTE**: PB6/PB7 son compartidos entre LEDs y USB.
> No conectar LEDs a PB6/PB7 mientras se usa USB.

#### Proyecto `usb_cdc` (puente USB↔UART)

Puerto serie virtual que puentea USB CDC ↔ UART2 (PA2 TX, PA3 RX):
```bash
cd ~/WHC-MCU/ch32v/ch32v305/projects/usb_cdc
make && make flash
```
- Host ve: `ID 1a86:fe0c` → `/dev/ttyACMx`
- Datos USB se envian por UART2 TX (PA2) y viceversa
- Para eco con este proyecto: conectar PA2 ↔ PA3 con jumper

#### Proyecto `usb_cdc_echo` (eco directo USB)

Eco puro por USB, sin UART — todo lo que se envia se devuelve:
```bash
cd ~/WHC-MCU/ch32v/ch32v305/projects/usb_cdc_echo
make && make flash
```
- Host ve: `ID 1a86:fe0c` → `/dev/ttyACMx`
- Tamano: ~3.8 KB (minimalista, sin UART/DMA/TIM)
- Probar eco:
```bash
# Terminal 1: escuchar
cat /dev/ttyACM1
# Terminal 2: enviar
echo "hola mundo" > /dev/ttyACM1
```

#### Probar eco con Python
```python
import serial
s = serial.Serial('/dev/ttyACM1', 115200, timeout=1)
s.write(b'Hola!\n')
print(s.read(100))   # b'Hola!\n'
s.close()
```

### ADC (12 bits, 2 unidades)

El CH32V305 tiene **ADC1** y **ADC2** (12 bits, 18 canales cada uno).

| Parametro | Valor |
|-----------|-------|
| Resolucion | 12 bits (0–4095) |
| Unidades | ADC1, ADC2 (modo dual disponible) |
| Canales externos | 16 (IN0–IN15, mapeados a GPIO) |
| Canales internos | IN16 = sensor temp, IN17 = Vrefint (~1.2V) |
| Clock ADC | PCLK2 / 2, 4, 6, 8 (max ~14 MHz) |
| Tiempos muestreo | 1.5, 7.5, 13.5, 28.5, 41.5, 55.5, 71.5, 239.5 ciclos |
| PGA integrado | x1, x4, x16, x64 |
| DMA | Si |
| Watchdog analogico | Si |
| Canales inyectados | 4 por ADC |
| Cristal requerido | No (funciona con HSI) |

**Canales ADC disponibles en los headers de la placa:**

| Canal | Pin | Header | Notas |
|-------|-----|--------|-------|
| IN1   | PA1 | P1     | Libre |
| IN5   | PA5 | P1     | Libre |
| IN16  | —   | interno | Sensor de temperatura |
| IN17  | —   | interno | Vrefint ~1.2V |

> Los demas canales ADC (IN0, IN2–IN4, IN6–IN15) usan pines que **no
> estan en los headers** de esta placa (PA0, PA2–PA4, PA6–PA7, PB0–PB1,
> PC0–PC5). Para usarlos habria que soldar directo al LQFP48.

---

## Diferencias clave entre ICs

| Caracteristica | CH32V003          | CH32V305                 |
|----------------|-------------------|--------------------------|
| `-march`       | `rv32ec_zicsr`    | `rv32imac_zicsr[_zifencei]` |
| `-mabi`        | `ilp32e`          | `ilp32`                  |
| Flash          | 16 KB             | 128 KB                   |
| RAM            | 2 KB              | 32 KB                    |
| GPIO config    | Solo CFGLR (0-7)  | CFGLR (0-7) + CFGHR (8-15) |
| Pin debug      | PD1               | PA13                     |
| HSI default    | 24 MHz            | 8 MHz                    |
| USB            | No                | USBHS 480Mbps (PB6/PB7) |
| ADC            | 1x 10-bit         | 2x 12-bit + PGA x1/x4/x16/x64 |
| Tipo proyecto  | Solo bare-metal   | Bare-metal o SDK         |
| Header file    | `ch32v003.h`      | `ch32v305.h` o SDK headers |
| Linker script  | `ch32v003.ld`     | `ch32v305.ld` o `sdk/Ld/Link.ld` |

## Troubleshooting

**OpenOCD no conecta:**
- Verificar que el switch de la placa este en ON
- Verificar cable SWDIO conectado al pin correcto (PD1 en V003, PA13 en V305)
- Verificar que 3V3 este conectado desde el WCH-LinkE
- `lsusb | grep 1a86` debe mostrar `8010` (modo RISC-V)

**Error "libhidapi" o "libjaylink":**
```bash
sudo apt install libhidapi-hidraw0 libjaylink0
```

**Permiso denegado al flashear:**
```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```
Verificar que el usuario este en el grupo `plugdev`:
```bash
groups | grep plugdev
```

**GCC error "extension zicsr required":**
Agregar `_zicsr` al flag `-march` (ej: `rv32ec_zicsr`).

**GCC error "fence.i" / "extension zifencei required":**
Agregar `_zifencei` al `-march`. Solo necesario con SDK (ej: `rv32imac_zicsr_zifencei`).

**USB no enumera / "device descriptor read, error -110":**
> **CRITICO con GCC 15**: El atributo `__attribute__((interrupt("WCH-Interrupt-fast")))` del
> SDK de WCH es ignorado silenciosamente por GCC 15. Los interrupt handlers se compilan como
> funciones normales con `ret` en vez de `mret`, lo que estrella el CPU en la primera interrupcion.
>
> **Solucion**: Cambiar `"WCH-Interrupt-fast"` por `"machine"` en todos los ISR:
> ```c
> // ANTES (no funciona con GCC 15):
> void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
>
> // DESPUES (correcto):
> void TIM2_IRQHandler(void) __attribute__((interrupt("machine")));
> ```
> Esto aplica a TODOS los handlers: TIM2_IRQHandler, USBHS_IRQHandler, NMI_Handler, etc.

**Linker: "undefined reference to memcpy/printf":**
Asegurar que `-lc -lm -lnosys` estan DESPUES de los objetos en la linea de link:
```makefile
$(CC) $(LDFLAGS) $^ -lc -lm -lnosys -o $@   # Correcto
```

**Linker: "multiple definition of _start":**
Agregar `-nostartfiles` a LDFLAGS cuando se usa el startup del SDK.
