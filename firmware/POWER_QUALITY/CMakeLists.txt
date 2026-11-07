cmake_minimum_required(VERSION 3.15)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)

project(POWER_QUALITY C ASM)

set(TARGET_NAME power_quality)

add_definitions(
    -DSTM32F10X_MD
    -DUSE_STDPERIPH_DRIVER
)

include_directories(
    .
    Core/Inc
    Drivers/CMSIS/Core
    Drivers/CMSIS/Device
    Drivers/STM32F10x_StdPeriph_Driver/inc
    Middlewares/Third_Party/FreeRTOS/Source/include
    Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3
    Hardware
    Hardware/led
    Hardware/delay
    Hardware/pzem
    middle
    middle/uart_debug
    third_party
    third_party/ringbuff
    third_party/jsmn
)

file(GLOB_RECURSE SRC_CORE "Core/Src/*.c")
file(GLOB_RECURSE SRC_HARDWARE "Hardware/*.c")
file(GLOB_RECURSE SRC_MIDDLE "middle/*.c")
file(GLOB_RECURSE SRC_THIRD_PARTY "third_party/*.c")
file(GLOB_RECURSE SRC_STDPERIPH "Drivers/STM32F10x_StdPeriph_Driver/src/*.c")
file(GLOB SRC_FREERTOS "Middlewares/Third_Party/FreeRTOS/Source/*.c")

set(SOURCES
    ${SRC_CORE}
    ${SRC_HARDWARE}
    ${SRC_MIDDLE}
    ${SRC_THIRD_PARTY}
    ${SRC_STDPERIPH}
    ${SRC_FREERTOS}
    Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3/port.c
    Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c
    Drivers/CMSIS/Core/core_cm3.c
    Drivers/CMSIS/Device/Startup/startup_stm32f10x_md.s
)

set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/STM32F103C8Tx_FLASH.ld)

set(MCU_FLAGS "-mcpu=cortex-m3 -mthumb")

set(CMAKE_C_FLAGS "${MCU_FLAGS} -Wall -fdata-sections -ffunction-sections -Og -g")
set(CMAKE_ASM_FLAGS "${MCU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS "${MCU_FLAGS} -specs=nano.specs -T${LINKER_SCRIPT} -Wl,-Map=${TARGET_NAME}.map,--cref -Wl,--gc-sections -lc -lm -lnosys")

add_executable(${TARGET_NAME}.elf ${SOURCES})

add_custom_command(TARGET ${TARGET_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary ${TARGET_NAME}.elf ${TARGET_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${TARGET_NAME}.elf ${TARGET_NAME}.hex
    COMMAND ${CMAKE_SIZE} ${TARGET_NAME}.elf
    COMMENT "Building binary and hex outputs..."
)
