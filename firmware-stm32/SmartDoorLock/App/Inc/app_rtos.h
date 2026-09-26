#ifndef APP_RTOS_H
#define APP_RTOS_H

#include <stdbool.h>
#include "cmsis_os2.h"
#include "app_config.h"
#include "app_events.h"

/* Ownership: every peripheral has exactly one task that touches it.
 *   tCtrl  - lock GPIO, buzzer (TIM3), Flash storage, RTC, state machine
 *   tRfid  - SPI1 + RC522
 *   tInput - ADC1 + buttons (EXTI)
 *   tUi    - SPI2 + DMA + GC9A01
 *   tNet   - USART1 + DMA + ESP (M5)
 *   tLog   - USB CDC
 *   tSuper - IWDG, alive LED */

extern osMessageQueueId_t qCtrl;     /* AppEvent  -> tCtrl */
extern osMessageQueueId_t qUi;       /* UiCommand -> tUi */
extern osMessageQueueId_t qLog;      /* LogMsg    -> tLog */
extern osMessageQueueId_t qNetTx;    /* NetLine   -> tNet */
extern osTimerId_t        tmrState;  /* one-shot state timer, posts EV_TIMEOUT */

extern osThreadId_t tCtrlHandle;
extern osThreadId_t tRfidHandle;
extern osThreadId_t tInputHandle;
extern osThreadId_t tUiHandle;
extern osThreadId_t tNetHandle;
extern osThreadId_t tLogHandle;
extern osThreadId_t tSuperHandle;

/* Thread flags */
#define FLAG_UI_DMA_DONE   0x0001U   /* tUi: SPI2 DMA transfer complete */
#define FLAG_BTN_EXIT      0x0001U   /* tInput: EXTI on the exit button */
#define FLAG_BTN_JOY       0x0002U   /* tInput: EXTI on the joystick button */

typedef struct {
    char line[CFG_NET_LINE_LEN];
} NetLine;

/* Non-blocking post to tCtrl; safe from tasks, timer callbacks and ISRs. */
bool App_PostCtrl(const AppEvent *ev);

void TaskCtrl(void *arg);
void TaskRfid(void *arg);
void TaskInput(void *arg);
void TaskUi(void *arg);
void TaskNet(void *arg);
void TaskLog(void *arg);
void TaskSuper(void *arg);

#endif
