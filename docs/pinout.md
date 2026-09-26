# Розпіновка (STM32F411CEU6, Black Pill)

| Функція | Пін | Режим | Мітка в коді |
|---|---|---|---|
| RC522 SCK / MISO / MOSI | PA5 / PA6 / PA7 | SPI1, 6 МГц | — |
| RC522 CS | PA1 | GPIO out, старт = 1 | `RFID_CS` |
| RC522 RST | PA4 | GPIO out, старт = 1 | `RFID_RST` |
| RC522 IRQ (не використовується) | PB1 | EXTI1, pull-up, NVIC вимкнено | `RFID_IRQ` |
| GC9A01 SCL / SDA | PB10 / PB15 | SPI2 Transmit Only, 24 МГц, DMA1 Stream4 | — |
| GC9A01 DC | PA8 | GPIO out | `LCD_DC` |
| GC9A01 CS | PA15 | GPIO out, старт = 1 | `LCD_CS` |
| GC9A01 RES | PB12 | GPIO out | `LCD_RES` |
| Джойстик X / Y | PA0 / PA3 | ADC1_IN0 / ADC1_IN3 | `JOY_X` / `JOY_Y` |
| Джойстик SW | PB8 | EXTI8, pull-up, спад | `JOY_SW` |
| Зумер (пасивний) | PB4 | TIM3_CH1 PWM | — |
| Замок (затвор LR7843) | PB13 | GPIO out, pull-down, старт = 0 | `LOCK_CTRL` |
| Кнопка виходу | PB7 | EXTI7, pull-up, спад | `EXIT_BTN` |
| ESP TX → STM32 RX / STM32 TX → ESP RX | PA10 / PA9 | USART1 115200 8N1, DMA2 S2 (RX, circular) / S7 (TX) | — |
| ESP EN | PB14 | GPIO out, старт = 0 (ESP у reset) | `ESP_EN` |
| USB-C (лог і консоль) | PA11 / PA12 | USB OTG FS, CDC | — |
| Світлодіод «живий» | PC13 | GPIO out, активний 0 | `LED` |
| SWD | PA13 / PA14 | Serial Wire | — |
| Кварц 25 МГц | PH0 / PH1 | HSE | — |
| Кварц 32,768 кГц | PC14 / PC15 | LSE (вмикає `app_time.c`) | — |
| Вільні | PA2, PB0, PB3, PB5, PB6 | | PB0 (ADC1_IN8) — під контроль 12 В |

## Тактування

HSE 25 МГц → PLL (M = 25, N = 192, P = 2, Q = 4) → SYSCLK 96 МГц, USB 48 МГц. APB1 = 48 МГц (таймери 96 МГц), APB2 = 96 МГц. Flash latency 3 WS.

| Периферія | Звідки частота | Результат |
|---|---|---|
| SPI1 | APB2 96 МГц / 16 | 6 МГц (RC522 до 10 МГц) |
| SPI2 | APB1 48 МГц / 2 | 24 МГц |
| TIM3 | 96 МГц / (95 + 1) | 1 МГц, тон = 1 000 000 / (ARR + 1) |
| IWDG | LSI 32 кГц / 128, reload 1000 | ~4 с |

## Підключення

```
Зумер (пасивний) через NPN:          Замок:
PB4 ──[1 кОм]──> база S8050           12 В ──> (+) замка
3V3 ──> (+) зумера                    (–) замка ──> сток LR7843
(–) зумера ──> колектор               витік LR7843 ──> GND
емітер ──> GND                        PB13 ──[100 Ом]──> затвор, затвор ──[10 кОм]──> GND
                                      1N4007 паралельно замку (катод до +12 В)

Кнопка виходу: PB7 ── кнопка ── GND (внутрішня підтяжка)
```

Режим замка задається в `app_config.h`: `CFG_LOCK_ENERGIZE_TO_OPEN = 1` — соленоїд/клямка (живлення відчиняє), `0` — електромагніт, що тримає двері під струмом.
