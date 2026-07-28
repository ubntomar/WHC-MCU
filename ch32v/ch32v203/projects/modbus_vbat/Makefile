TARGET = modbus_vbat
SRCS_C = main.c
LDSCRIPT = app.ld
SDK_EXTRA_C = $(SDK)/Peripheral/src/ch32v20x_adc.c \
              $(SDK)/Peripheral/src/ch32v20x_flash.c \
              $(SDK)/Peripheral/src/ch32v20x_iwdg.c
include ../../common/Makefile.common

# Imagen combinada bootloader (0x0000) + app (0x2000) para tarjetas
# vírgenes vía WCH-Link. Las actualizaciones posteriores van por RS-485.
BOOT = ../boot485

$(BUILD)/combined.bin: all
	$(MAKE) -C $(BOOT)
	python3 -c "import zlib,struct; \
b=open('$(BOOT)/build/boot485.bin','rb').read(); \
a=open('$(BUILD)/$(TARGET).bin','rb').read(); \
a+= b'\xff'*(len(a)%2); \
assert len(b)<=8192 and len(a)<=0x7FF0; \
crc=zlib.crc32(a)&0xFFFFFFFF; \
tr=struct.pack('<III',0xA5B007A5,len(a),crc)+b'\xff'*4; \
img=b+b'\xff'*(8192-len(b))+a+b'\xff'*(0x7FF0-len(a))+tr; \
assert len(img)==0xA000; \
open('$(BUILD)/combined.bin','wb').write(img); \
print(f'combined: boot={len(b)} app={len(a)} crc={crc:08X} (con trailer)')"

combined: $(BUILD)/combined.bin

flash-full: $(BUILD)/combined.bin
	$(OPENOCD) -f $(OCD_CFG) \
	  -c "init" -c "halt" \
	  -c "program $(BUILD)/combined.bin 0x00000000 verify" \
	  -c "reset" -c "exit"

.PHONY: combined flash-full
