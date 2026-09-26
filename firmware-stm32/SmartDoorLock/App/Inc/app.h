#ifndef APP_H
#define APP_H

/* Creates queues, timers, event flags and all application tasks.
 * Called from MX_FREERTOS_Init() (USER CODE RTOS_THREADS) before the scheduler starts. */
void App_CreateRtosObjects(void);

#endif
