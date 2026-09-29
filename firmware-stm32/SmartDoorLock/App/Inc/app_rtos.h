#ifndef APP_RTOS_H
#define APP_RTOS_H

#include <stdbool.h>
#include "cmsis_os2.h"
#include "app_config.h"
#include "app_events.h"

/* Головне правило: кожну периферію чіпає рівно одна задача-власник.
 * Тому м'ютекси на шинах не потрібні і дві задачі ніколи не "б'ються" за SPI.
 *   tCtrl  - замок, зумер (TIM3), Flash зі списком карток, годинник, стан-машина
 *   tRfid  - SPI1 + RC522
 *   tInput - ADC1 (джойстик) + кнопки (EXTI)
 *   tUi    - SPI2 + DMA + дисплей GC9A01
 *   tNet   - USART1 + DMA + ESP (етап M5)
 *   tLog   - USB (лог і консоль)
 *   tSuper - watchdog (IWDG), світлодіод "живий" */

extern osMessageQueueId_t qCtrl;
extern osMessageQueueId_t qUi;
extern osMessageQueueId_t qLog;
extern osMessageQueueId_t qNetTx;
extern osTimerId_t        tmrState;

extern osThreadId_t tCtrlHandle;
extern osThreadId_t tRfidHandle;
extern osThreadId_t tInputHandle;
extern osThreadId_t tUiHandle;
extern osThreadId_t tNetHandle;
extern osThreadId_t tLogHandle;
extern osThreadId_t tSuperHandle;

#define FLAG_UI_DMA_DONE   0x0001U
#define FLAG_BTN_EXIT      0x0001U
#define FLAG_BTN_JOY       0x0002U

typedef struct {
    char line[CFG_NET_LINE_LEN];
} NetLine;

bool App_PostCtrl(const AppEvent *ev);

void TaskCtrl(void *arg);
void TaskRfid(void *arg);
void TaskInput(void *arg);
void TaskUi(void *arg);
void TaskNet(void *arg);
void TaskLog(void *arg);
void TaskSuper(void *arg);

#endif
