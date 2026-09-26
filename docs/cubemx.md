# CubeMX, збірка і CubeIDE

## Конфігурація `.ioc`

Проєкт `firmware-stm32/SmartDoorLock/SmartDoorLock.ioc`, MCU STM32F411CEUx, пакет STM32Cube FW_F4 V1.28.3, CubeMX 6.18.

| Блок | Налаштування |
|---|---|
| RCC | HSE кристал 25 МГц, PLL 25/192/2/4 → 96 МГц, USB 48 МГц, APB1 /2, APB2 /1 |
| SYS | Serial Wire, timebase **TIM11** (SysTick належить FreeRTOS) |
| SPI1 | Full-Duplex Master, 8 біт, CPOL 0 / CPHA 1Edge, NSS soft, /16 = 6 МГц |
| SPI2 | Transmit Only Master, /2 = 24 МГц, DMA1 Stream4 (SPI2_TX, Normal, байт) |
| USART1 | 115200 8N1, DMA2 Stream2 RX **Circular**, DMA2 Stream7 TX Normal, IRQ увімкнено |
| ADC1 | IN0, IN3, 12 біт, одиничне перетворення, програмний запуск |
| TIM3 | CH1 PWM на PB4, PSC 95, ARR 499, Pulse 0, ARR preload |
| IWDG | Prescaler **128**, Reload 1000 → ~4 с |
| USB_OTG_FS + USB_DEVICE | Device only, CDC |
| FreeRTOS | CMSIS_V2, heap_4, 32 КБ, mutex/timers, stack overflow check = 2, malloc failed hook |
| NVIC | EXTI9_5, DMA, USART1, SPI2, OTG_FS — пріоритет 5 (дозволено викликати FreeRTOS з ISR) |
| Project Manager | тулчейн **CMake**, окремі `.c/.h` на кожну периферію, лише потрібні бібліотеки |

RTC у `.ioc` навмисно **не** увімкнено: `app_time.c` сам запускає LSE з таймаутом і переходить на LSI, якщо кварц не завівся (див. [architecture.md](architecture.md)).

## Що змінено руками поверх згенерованого (і переживає регенерацію)

| Файл | Секція | Що |
|---|---|---|
| `Core/Src/freertos.c` | `USER CODE Includes`, `4`, `5`, `RTOS_THREADS`, `StartDefaultTask` | `App_CreateRtosObjects()`, хуки переповнення стеку / нестачі heap, `osThreadExit()` у defaultTask |
| `Core/Src/stm32f4xx_it.c` | `HardFault_IRQn 0` | запис причини + reset / `BKPT` під налагоджувачем |
| `USB_DEVICE/App/usbd_cdc_if.c` | `INCLUDE`, `6` | прийом байтів консолі |
| `STM32F411xx_FLASH.ld` | `MEMORY` | `FLASH LENGTH = 384K` (сектор 7 — дані) |
| `CMakeLists.txt` | генерується лише раз | джерела App/ і BSP, include-шляхи, `-Wall -Wextra -Werror` для власного коду |

`main.c` не змінювався зовсім. Якщо після регенерації лінкер-скрипт повернеться до 512K, прошивка це помітить (`FLASH_STORE_IsSafe`) і відмовиться стирати сектор, у лозі буде помилка.

## Регенерація коду

GUI: відкрити `.ioc` у STM32CubeMX → *Generate Code*.

Командний рядок (так генерувався цей репозиторій):

```text
config load "<повний шлях>\SmartDoorLock.ioc"
project generate
exit
```

```bash
"C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeMX/jre/bin/java" -jar "C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeMX/STM32CubeMX.exe" -q script.txt
```

Нюанси, знайдені під час налаштування:
- у тихому режимі CubeMX зависає назавжди, якщо показує діалог; причини, які траплялися: рукописний `ProjectManager.functionlistsort` і невалідний параметр джерела RTC у RCC;
- генерація з тулчейном *STM32CubeIDE* у тихому режимі зависає на створенні Eclipse-проєкту — тому CMake;
- вихід CubeMX не можна обрізати через `| head`: закритий pipe вбиває процес посеред генерації.

## Збірка з командного рядка

```bash
PL=/c/ST/STM32CubeIDE_2.2.0/STM32CubeIDE/plugins
export PATH="$PL/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/tools/bin:$PL/com.st.stm32cube.ide.mcu.externaltools.cmake.win32_1.1.200.202605190741/tools/bin:$PL/com.st.stm32cube.ide.mcu.externaltools.ninja.win32_1.1.200.202606260906/tools/bin:$PATH"
cd firmware-stm32/SmartDoorLock
cmake --preset Debug && cmake --build --preset Debug
```

(Назви папок плагінів залежать від версії CubeIDE.)

## CubeIDE

- **CubeIDE 2.x**: імпортувати папку `firmware-stm32/SmartDoorLock` як CMake-проєкт (майстер імпорту STM32 CMake у меню *File → Import*). Для налагодження — конфігурація *STM32 C/C++ Application* на `build/Debug/SmartDoorLock.elf`, ST-Link, SWD.
- Хто хоче класичний Eclipse-проєкт: у CubeMX змінити тулчейн на *STM32CubeIDE* і згенерувати з GUI; потім додати `App` як *Source Folder* і include-шляхи `App/Inc` та кожну папку `Drivers/BSP/*`.
- VS Code з розширенням STM32 працює з `CMakePresets.json` без змін.
