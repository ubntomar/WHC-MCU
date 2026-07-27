TARGET = adc_vbat
SRCS_C = main.c
BOARD_ID  ?= B?
PERIOD_MS ?= 900
CFLAGS += -DBOARD_ID=\"$(BOARD_ID)\" -DPERIOD_MS=$(PERIOD_MS)
SDK_EXTRA_C = $(SDK)/Peripheral/src/ch32v20x_adc.c
include ../../common/Makefile.common
