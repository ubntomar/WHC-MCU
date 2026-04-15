# USB CDC Echo directo — CH32V305FBP6

TARGET   = usb_cdc_echo
BUILD    = build
SDK      = ../../sdk

# Toolchain
PREFIX   = riscv-none-elf-
CC       = $(PREFIX)gcc
OBJCOPY  = $(PREFIX)objcopy
OBJDUMP  = $(PREFIX)objdump
SIZE     = $(PREFIX)size

# WCH OpenOCD
OPENOCD  = /opt/wch-openocd/bin/openocd
OCD_CFG  = /opt/wch-openocd/bin/wch-riscv.cfg

# CPU flags
ARCH     = -march=rv32imac_zicsr_zifencei -mabi=ilp32

# Include paths
INCLUDES = -I. \
           -IUSB_Device \
           -I$(SDK)/Core \
           -I$(SDK)/Debug \
           -I$(SDK)/Peripheral/inc

# Compiler defines
DEFS     = -DCH32V30x_D8C

CFLAGS   = $(ARCH) -Os -Wall -Wno-attributes -g \
           $(INCLUDES) $(DEFS) \
           -ffunction-sections -fdata-sections \
           -fno-common -nostdlib

ASFLAGS  = $(ARCH) -g $(INCLUDES) $(DEFS) -x assembler-with-cpp

LDFLAGS  = $(ARCH) -T $(SDK)/Ld/Link.ld -nostartfiles -Wl,--gc-sections \
           -Wl,-Map=$(BUILD)/$(TARGET).map \
           --specs=nano.specs --specs=nosys.specs

# === Sources ===
SRCS_C   = main.c \
           system_ch32v30x.c \
           ch32v30x_it.c \
           USB_Device/ch32v30x_usbhs_device.c \
           USB_Device/usb_desc.c

# SDK
SRCS_C  += $(SDK)/Core/core_riscv.c \
           $(SDK)/Debug/debug.c \
           $(SDK)/Peripheral/src/ch32v30x_rcc.c \
           $(SDK)/Peripheral/src/ch32v30x_gpio.c \
           $(SDK)/Peripheral/src/ch32v30x_misc.c

# Startup
SRCS_S   = $(SDK)/Startup/startup_ch32v30x_D8C.S

# Objects
OBJS     = $(addprefix $(BUILD)/, $(notdir $(SRCS_C:.c=.o))) \
           $(addprefix $(BUILD)/, $(notdir $(SRCS_S:.S=.o)))

VPATH    = . USB_Device $(SDK)/Core $(SDK)/Debug $(SDK)/Peripheral/src $(SDK)/Startup

.PHONY: all clean flash erase

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin
	@$(SIZE) $(BUILD)/$(TARGET).elf

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ -lc -lm -lnosys -o $@
	$(OBJDUMP) -d $@ > $(BUILD)/$(TARGET).lst

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD):
	mkdir -p $(BUILD)

flash: $(BUILD)/$(TARGET).elf
	$(OPENOCD) -f $(OCD_CFG) \
	  -c "init" \
	  -c "halt" \
	  -c "program $(BUILD)/$(TARGET).elf verify" \
	  -c "reset" \
	  -c "exit"

erase:
	$(OPENOCD) -f $(OCD_CFG) \
	  -c "init" \
	  -c "halt" \
	  -c "flash erase_sector wch_riscv.flash 0 last" \
	  -c "exit"

clean:
	rm -rf $(BUILD)
