# CubeMX і STM32CubeIDE

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
| FreeRTOS | CMSIS_V2, heap_4, 32 КБ, mutex/timers, stack overflow check = 2, malloc failed hook, **USE_NEWLIB_REENTRANT** (логер викликає `vsnprintf` з кількох задач) |
| NVIC | EXTI9_5, DMA, USART1, SPI2, OTG_FS — пріоритет 5 (дозволено викликати FreeRTOS з ISR) |
| Project Manager | тулчейн **STM32CubeIDE**, *Generate Under Root*, окремі `.c/.h` на кожну периферію, лише потрібні бібліотеки |

RTC у `.ioc` навмисно **не** увімкнено: `app_time.c` сам запускає LSE з таймаутом і переходить на LSI, якщо кварц не завівся (див. [architecture.md](architecture.md)).

## Що змінено руками поверх згенерованого (і переживає регенерацію)

| Файл | Секція | Що |
|---|---|---|
| `Core/Src/freertos.c` | `USER CODE Includes`, `4`, `5`, `RTOS_THREADS`, `StartDefaultTask` | `App_CreateRtosObjects()`, хуки переповнення стеку / нестачі heap, `osThreadExit()` у defaultTask |
| `Core/Src/stm32f4xx_it.c` | `HardFault_IRQn 0` | запис причини + reset / `BKPT` під налагоджувачем |
| `USB_DEVICE/App/usbd_cdc_if.c` | `INCLUDE`, `6` | прийом байтів консолі |
| `STM32F411CEUX_FLASH.ld` | `MEMORY` | `FLASH LENGTH = 384K` (сектор 7 — дані) |
| `.cproject` | Debug і Release | `App` як source-папка; include-шляхи `../App/Inc` і `../Drivers/BSP/*` (BSP компілюється як частина `Drivers`) |

`main.c` не змінювався зовсім. Регенерацію з цими змінами перевірено: CubeMX зберігає і USER CODE, і додані в `.cproject` папки/шляхи, і лінкер-скрипт. Якщо лінкер-скрипт колись повернеться до 512K, прошивка це помітить (`FLASH_STORE_IsSafe`) і відмовиться стирати сектор, у лозі буде помилка.

## Робота в CubeIDE

1. *File → Import → General → Existing Projects into Workspace* → *Select root directory*: `firmware-stm32/SmartDoorLock` → *Finish*. Не вмикати «Copy projects into workspace» — проєкт має лишатися в репозиторії.
2. *Project → Build Project* (Debug / Release). Очікувано: `Build Finished. 0 errors, 0 warnings.`
3. *Run → Debug As → STM32 C/C++ Application*, інтерфейс ST-Link SWD. Під налагоджувачем watchdog заморожується, breakpoint не перезавантажить плату.
4. Змінити периферію: двічі клікнути `SmartDoorLock.ioc` (відкриється STM32CubeMX) → змінити → *Generate Code*. Свій код писати лише в `App/`, `Drivers/BSP/` і в секціях `USER CODE`.

## Регенерація з командного рядка

```text
config load "<повний шлях>\SmartDoorLock.ioc"
project generate
exit
```

```bash
"C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeMX/jre/bin/java" -jar "C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeMX/STM32CubeMX.exe" -q script.txt
```

Нюанси, знайдені під час налаштування:
- у тихому режимі CubeMX зависає назавжди, якщо показує діалог. Траплялося: рукописний `ProjectManager.functionlistsort`, невалідний параметр джерела RTC у RCC і попередження «USE_NEWLIB_REENTRANT must be set» (показується лише для тулчейну STM32CubeIDE — тому опцію ввімкнено);
- вихід CubeMX не можна обрізати через `| head`: закритий pipe вбиває процес посеред генерації.

## Збірка без GUI

```bash
cd C:/ST/STM32CubeIDE_2.2.0/STM32CubeIDE
./stm32cubeidec.exe --launcher.suppressErrors -nosplash \
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild \
  -data <тимчасовий workspace> -import <шлях>/firmware-stm32/SmartDoorLock \
  -build SmartDoorLock/Debug
```

Результат: `firmware-stm32/SmartDoorLock/Debug/SmartDoorLock.elf` (папки `Debug/` і `Release/` у `.gitignore`).
