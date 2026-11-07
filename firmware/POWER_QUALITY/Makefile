################################################################################
# Makefile for STM32F103C8T6 (StdPeriph Library v3.5.0)
# Project: POWER_QUALITY (DATN_NAM)
################################################################################

TARGET = power_quality
BUILD_DIR = build

# Toolchain definitions
PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

# MCU Flags
CPU = -mcpu=cortex-m3
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

# Macros / Defines
C_DEFS =  \
-DSTM32F10X_MD \
-DUSE_STDPERIPH_DRIVER

# Include paths
C_INCLUDES =  \
-I. \
-ICore/Inc \
-IDrivers/CMSIS/Core \
-IDrivers/CMSIS/Device \
-IDrivers/STM32F10x_StdPeriph_Driver/inc \
-IMiddlewares/Third_Party/FreeRTOS/Source/include \
-IMiddlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3 \
-IHardware \
-IHardware/led \
-IHardware/delay \
-IHardware/pzem \
-Imiddle \
-Imiddle/uart_debug \
-Ithird_party \
-Ithird_party/ringbuff \
-Ithird_party/jsmn

# C Sources
C_SOURCES =  \
Core/Src/main.c \
Core/Src/stm32f10x_it.c \
Core/Src/system_stm32f10x.c \
Drivers/CMSIS/Core/core_cm3.c \
$(wildcard Drivers/STM32F10x_StdPeriph_Driver/src/*.c) \
Hardware/led/led.c \
Hardware/delay/delay.c \
middle/uart_debug/uart_debug.c \
third_party/ringbuff/ringbuff.c \
third_party/jsmn/jsmn.c

# ASM Sources
ASM_SOURCES =  \
Drivers/CMSIS/Device/Startup/startup_stm32f10x_md.s

# Linker script
LDSCRIPT = STM32F103C8Tx_FLASH.ld

# Compiler flags
OPT ?= -Og
CFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections -g -gdwarf-2

# Generate dependency information
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

# Linker flags
LIBS = -lc -lm -lnosys
LIBDIR =
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBDIR) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

# Default rule: build all
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

# Object files
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))

OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	@echo "  CC    $<"
	@$(CC) -c $(CFLAGS) -Wa,-adhlns=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	@echo "  AS    $<"
	@$(AS) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	@echo "  LD    $@"
	@$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	@$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	@echo "  HEX   $@"
	@$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	@echo "  BIN   $@"
	@$(BIN) $< $@

$(BUILD_DIR):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD_DIR)
	@echo "Build directory cleaned."

# Flash via ST-Link (st-flash)
flash-stlink: $(BUILD_DIR)/$(TARGET).bin
	st-flash --reset write $(BUILD_DIR)/$(TARGET).bin 0x08000000

# Flash via OpenOCD
flash-openocd: $(BUILD_DIR)/$(TARGET).bin
	openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "program $(BUILD_DIR)/$(TARGET).bin 0x08000000 verify reset exit"

# Default flash: prefer st-flash if available, fallback to openocd
flash: $(BUILD_DIR)/$(TARGET).bin
	@if which st-flash > /dev/null 2>&1; then \
		$(MAKE) flash-stlink; \
	else \
		$(MAKE) flash-openocd; \
	fi

# Erase chip
erase-stlink:
	st-flash erase

erase-openocd:
	openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "init; reset halt; stm32x mass_erase 0; reset exit"

erase:
	@if which st-flash > /dev/null 2>&1; then \
		$(MAKE) erase-stlink; \
	else \
		$(MAKE) erase-openocd; \
	fi

# Reset chip
reset-stlink:
	st-flash reset

reset-openocd:
	openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "init; reset run; exit"

reset:
	@if which st-flash > /dev/null 2>&1; then \
		$(MAKE) reset-stlink; \
	else \
		$(MAKE) reset-openocd; \
	fi

.PHONY: all clean flash flash-stlink flash-openocd erase erase-stlink erase-openocd reset reset-stlink reset-openocd
-include $(wildcard $(BUILD_DIR)/*.d)
