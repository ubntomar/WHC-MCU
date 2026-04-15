# WHC-MCU — Compilar y flashear microcontroladores WCH desde Ubuntu

WHC-MCU es una colección de proyectos bare-metal y basados en el HAL oficial de
WCH para microcontroladores **CH32V** (RISC-V) y **CH5xx** (8051), con Makefiles
uniformes para compilar y flashear desde Linux usando un programador **WCH-LinkE**.

El objetivo del repositorio es **didáctico y reproducible**: cualquier persona
(o agente de IA) con una Ubuntu limpia debe poder clonar, instalar la toolchain
y tener el primer LED parpadeando siguiendo estos pasos.

> Si eres un agente de IA leyendo este README para aprender: el documento está
> organizado en secciones autocontenidas. Cada familia de MCU tiene su propio
> subdirectorio con un `README.md` o referencia a [`SKILL.md`](SKILL.md). El
> flujo siempre es el mismo: **instalar toolchain → `cd` al proyecto → `make`
> → `make flash`**.

---

## Tabla de contenidos

1. [Hardware soportado](#hardware-soportado)
2. [Estructura del repositorio](#estructura-del-repositorio)
3. [Primer arranque en Ubuntu](#primer-arranque-en-ubuntu)
4. [Flujo de compilación y flasheo](#flujo-de-compilación-y-flasheo)
5. [Por familia de MCU](#por-familia-de-mcu)
6. [Programador WCH-LinkE](#programador-wch-linke)
7. [Troubleshooting](#troubleshooting)
8. [Cómo añadir un proyecto nuevo](#cómo-añadir-un-proyecto-nuevo)
9. [Referencias](#referencias)

---

## Hardware soportado

| Familia     | Core         | ARCH (`-march`)                   | Flash / RAM | Placas probadas               | Estado |
|-------------|--------------|-----------------------------------|-------------|-------------------------------|--------|
| CH32V003    | QingKe V2A   | `rv32ec_zicsr` + `ilp32e`         | 16K / 2K    | CH32V003F4P6-R0-1v1           | ok     |
| CH32V203    | QingKe V4B   | `rv32imac_zicsr_zifencei` + `ilp32` | 64K / 20K | `buck_adc` (custom, C8T6)     | ok     |
| CH32V305    | QingKe V4F   | `rv32imac_zicsr[_zifencei]` + `ilp32` | 128K / 32K | CH32V305F-R0-1v0            | ok     |
| CH32V307    | QingKe V4F   | igual que V305                    | 256K / 64K  | —                             | plan   |
| CH552 (8051)| E8051        | sdcc                              | 16K / 1K    | —                             | plan   |

Cada MCU tiene una entrada detallada en [`SKILL.md`](SKILL.md). La familia
**CH32V203** está documentada además en
[`ch32v/ch32v203/README.md`](ch32v/ch32v203/README.md).

---

## Estructura del repositorio

```
WHC-MCU/
├── README.md                  # este archivo — punto de entrada
├── SKILL.md                   # referencia profunda por familia (V003, V305, V203...)
├── .gitignore
├── scripts/
│   └── bootstrap_ch32v203_sdk.sh   # reproduce ch32v203/sdk/ desde openwch/ch32v20x
├── tools/
│   └── openocd/wch-riscv.cfg   # config OpenOCD para WCH-LinkE en modo RISC-V
├── ch32v/
│   ├── ch32v003/
│   │   ├── common/             # Makefile.common, startup.S, linker, headers
│   │   └── projects/blink/
│   ├── ch32v203/
│   │   ├── README.md           # guía específica del V203 + buck_adc
│   │   ├── common/Makefile.common
│   │   ├── sdk/                # HAL oficial de WCH (regenerable con scripts/bootstrap_ch32v203_sdk.sh)
│   │   └── projects/blink_pa1/
│   ├── ch32v305/
│   │   ├── common/
│   │   ├── sdk/                # HAL WCH para V30x (ya versionado)
│   │   └── projects/{blink,usb_cdc,usb_cdc_echo,usb_cdc_adc}/
│   └── ch32v307/ (pendiente)
└── ch5xx/ch552/ (pendiente)
```

**Convención importante**: cada proyecto vive en
`ch32v/<familia>/projects/<nombre>/` y contiene como mínimo `main.c` y un
`Makefile` de 3 líneas que incluye `../../common/Makefile.common`. Toda la
lógica de build, flash y limpieza está centralizada en ese Makefile común.

---

## Primer arranque en Ubuntu

Probado en Ubuntu 22.04 y 24.04. Requiere sudo solo para instalar paquetes del
sistema y las reglas udev.

### 1. Paquetes del sistema

```bash
sudo apt update
sudo apt install -y \
    build-essential git make \
    libhidapi-hidraw0 libjaylink0 libusb-1.0-0 \
    python3 python3-pip
```

### 2. Toolchain RISC-V (xPack)

El repo asume `riscv-none-elf-gcc` en `/opt/riscv-toolchain/bin`. Instalar la
toolchain de xPack:

```bash
cd /tmp
VER=14.2.0-3   # o la última release de xpack-dev-tools/riscv-none-elf-gcc-xpack
wget "https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v${VER}/xpack-riscv-none-elf-gcc-${VER}-linux-x64.tar.gz"
sudo mkdir -p /opt/riscv-toolchain
sudo tar -C /opt/riscv-toolchain --strip-components=1 -xzf xpack-riscv-none-elf-gcc-${VER}-linux-x64.tar.gz
```

Añadir al `~/.bashrc`:

```bash
export PATH="/opt/riscv-toolchain/bin:$PATH"
```

Verificar:

```bash
riscv-none-elf-gcc --version
```

### 3. OpenOCD de WCH

El OpenOCD estándar de Ubuntu **no soporta** el WCH-LinkE. Hay que usar el
fork de WCH:

```bash
# Opción A: descargar binario precompilado desde MounRiver Studio
# (incluye el binario en MounRiver_Studio_Setup.tar.xz → toolchain/OpenOCD/)
# → copiar a /opt/wch-openocd/bin/

# Opción B: build manual
# https://github.com/karlp/openocd-hacks   (fork con driver wlinke)
```

Añadir al PATH:

```bash
export PATH="/opt/wch-openocd/bin:$PATH"
```

Verificar:

```bash
openocd --version   # debe decir "(wlinke)" o contener wch
```

> El archivo de configuración `tools/openocd/wch-riscv.cfg` en este repo
> funciona tanto con el OpenOCD de `/opt/wch-openocd` como con el que viene en
> MounRiver Studio.

### 4. Reglas udev para el WCH-LinkE

Crear `/etc/udev/rules.d/99-wch-link.rules` con:

```
# WCH-LinkE (modo RISC-V: VID 1a86, PID 8010)
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="8010", MODE="0666", GROUP="plugdev"
# WCH-LinkE (modo ARM/DAP: PID 8012)
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="8012", MODE="0666", GROUP="plugdev"
```

Recargar:

```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
sudo usermod -aG plugdev $USER
# cerrar sesión y volver a entrar
```

### 5. Clonar este repo

```bash
git clone https://github.com/<tu-usuario>/WHC-MCU.git
cd WHC-MCU
```

Si vas a trabajar con **CH32V203**, regenera su SDK una vez:

```bash
./scripts/bootstrap_ch32v203_sdk.sh
```

---

## Flujo de compilación y flasheo

Idéntico para todas las familias:

```bash
cd ch32v/<familia>/projects/<proyecto>
make          # compila → build/*.elf .hex .bin .lst .map
make flash    # programa el MCU vía WCH-LinkE + OpenOCD
make clean    # borra build/
make erase    # borra toda la flash del MCU
```

Conecta el WCH-LinkE al header SWD de la placa **antes** de `make flash`. El
comando `make flash` invoca OpenOCD con `tools/openocd/wch-riscv.cfg`, hace
`init`, `halt`, `program … verify`, `reset`, `exit`.

---

## Por familia de MCU

### CH32V003
Ver sección correspondiente en [`SKILL.md`](SKILL.md#ch32v003-qingke-v2a).
Proyecto ejemplo: `ch32v/ch32v003/projects/blink`.

### CH32V203
**Guía dedicada**: [`ch32v/ch32v203/README.md`](ch32v/ch32v203/README.md).
Usa el HAL oficial de WCH (no bare-metal). El directorio `sdk/` se regenera
con `scripts/bootstrap_ch32v203_sdk.sh` (no se versiona el clon completo del
upstream — solo los archivos realmente usados).

Proyecto ejemplo: `ch32v/ch32v203/projects/blink_pa1` (parpadea PA1 en el
board `buck_adc`).

### CH32V305
Ver [`SKILL.md`](SKILL.md#ch32v305-qingke-v4f). Dos estilos de proyecto:
- **Bare-metal simple** (blink) usando `common/`.
- **Con SDK** (USB CDC, ADC) — Makefile propio.

---

## Programador WCH-LinkE

| Señal     | WCH-LinkE | Placa                                    |
|-----------|-----------|------------------------------------------|
| 3V3       | 3V3       | VCC / 3V3                                |
| GND       | GND       | GND                                      |
| SWDIO/TMS | SWDIO     | pin debug del MCU (PD1 V003, PA13 V305, PA13 V203) |
| SWCLK/TCK | (no usado en single-wire) |                            |

Verificación:

```bash
lsusb | grep 1a86
# Bus 001 Device XXX: ID 1a86:8010 QinHeng Electronics WCH-Link    ← modo RISC-V OK
```

Si aparece `1a86:8012` en lugar de `8010`, el programador está en modo ARM.
Cambiar con el botón del WCH-LinkE o con WCH-LinkUtility.

---

## Troubleshooting

La [sección de troubleshooting de SKILL.md](SKILL.md#troubleshooting) cubre:

- OpenOCD no conecta
- Errores `libhidapi`/`libjaylink`
- Permiso denegado (udev)
- GCC: `extension zicsr required` / `extension zifencei required`
- USB no enumera con GCC 15 (hay que parchear los handlers `interrupt("machine")`)
- Linker: `undefined reference to memcpy/printf`
- Linker: `multiple definition of _start`

---

## Cómo añadir un proyecto nuevo

Ejemplo para CH32V203:

```bash
mkdir -p ch32v/ch32v203/projects/mi_test
cd ch32v/ch32v203/projects/mi_test
cat > Makefile <<'EOF'
TARGET = mi_test
SRCS_C = main.c
include ../../common/Makefile.common
EOF
$EDITOR main.c   # tu código
make && make flash
```

El `Makefile.common` ya linkea el HAL, el startup D6 y el linker 64K/20K. Si
necesitas añadir módulos del HAL (USART, ADC, TIM, SPI), edítalo y agrega
entradas a `SDK_SRCS_C`.

---

## Referencias

- [openwch/ch32v20x](https://github.com/openwch/ch32v20x) — HAL y ejemplos oficiales del CH32V203/208
- [openwch/ch32v307](https://github.com/openwch/ch32v307) — HAL CH32V30x
- [xpack-dev-tools/riscv-none-elf-gcc-xpack](https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack) — toolchain RISC-V
- [ch32-rs/wlink](https://github.com/ch32-rs/wlink) — flasher alternativo en Rust
- Datasheet CH32V203: [wch-ic.com/products/CH32V203.html](https://www.wch-ic.com/products/CH32V203.html)

---

## Licencia

Ver [`LICENSE`](LICENSE). El código del HAL bajo `ch32v/*/sdk/` proviene del
repo oficial de WCH (openwch/*) y está bajo su licencia original (Apache-2.0).
