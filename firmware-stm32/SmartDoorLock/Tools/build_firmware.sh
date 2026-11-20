#!/usr/bin/env bash
# Збирає прошивку без STM32CubeIDE, тими ж прапорцями, що й Release у CubeIDE.
# Потрібен arm-none-eabi-gcc у PATH. Запуск з папки SmartDoorLock: Tools/build_firmware.sh
set -euo pipefail
cd "$(dirname "$0")/.."

OUT=build-ci
CC=arm-none-eabi-gcc
CPU="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard"
DEFS="-DUSE_HAL_DRIVER -DSTM32F411xE"
INCS="-IApp/Inc -ICore/Inc -IUSB_DEVICE/App -IUSB_DEVICE/Target
      -IDrivers/STM32F4xx_HAL_Driver/Inc -IDrivers/STM32F4xx_HAL_Driver/Inc/Legacy
      -IDrivers/CMSIS/Device/ST/STM32F4xx/Include -IDrivers/CMSIS/Include
      -IDrivers/BSP/buzzer -IDrivers/BSP/gc9a01 -IDrivers/BSP/gfx -IDrivers/BSP/joystick
      -IDrivers/BSP/lock -IDrivers/BSP/rc522
      -IMiddlewares/Third_Party/FreeRTOS/Source/include
      -IMiddlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2
      -IMiddlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F
      -IMiddlewares/ST/STM32_USB_Device_Library/Core/Inc
      -IMiddlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc"
CFLAGS="$CPU -std=gnu11 -Os -ffunction-sections -fdata-sections -Wall $DEFS $INCS"

rm -rf "$OUT"
mkdir -p "$OUT"

objs=()
while IFS= read -r src; do
    obj="$OUT/$(echo "${src%.*}" | tr '/' '_').o"
    $CC $CFLAGS -c "$src" -o "$obj"
    objs+=("$obj")
done < <(find App Core Drivers Middlewares USB_DEVICE -name '*.c' -o -name '*.s' | sort)

$CC $CPU -T STM32F411CEUX_FLASH.ld --specs=nano.specs -Wl,--gc-sections \
    -Wl,-Map="$OUT/SmartDoorLock.map" -o "$OUT/SmartDoorLock.elf" "${objs[@]}" -lc -lm
arm-none-eabi-objcopy -O binary "$OUT/SmartDoorLock.elf" "$OUT/SmartDoorLock.bin"
arm-none-eabi-size "$OUT/SmartDoorLock.elf"
