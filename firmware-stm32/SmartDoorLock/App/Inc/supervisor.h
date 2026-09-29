#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include <stdbool.h>
#include <stdint.h>
#include "cmsis_os2.h"

/* Супервізор задач.
 * Кожна задача в своєму циклі викликає SUP_Heartbeat() ("я жива").
 * tSuper раз на 250 мс перевіряє: якщо всі задачі відзвітували за останні 2 с — годує watchdog.
 * Якщо хоч одна мовчить — перестає годувати, і через ~4 с апаратний IWDG перезавантажує плату.
 * Причина (яка задача зависла) зберігається в backup-регістрі RTC і показується після reset. */

typedef enum {
    TASK_CTRL = 0,
    TASK_RFID,
    TASK_INPUT,
    TASK_UI,
    TASK_NET,
    TASK_LOG,
    TASK_COUNT
} TaskId;

typedef enum {
    RESET_REASON_NONE = 0,
    RESET_REASON_TASK_HUNG,
    RESET_REASON_STACK_OVERFLOW,
    RESET_REASON_MALLOC_FAILED,
    RESET_REASON_HARDFAULT,
    RESET_REASON_CONSOLE
} ResetReason;

void        SUP_Init(void);
void        SUP_Register(TaskId id, osThreadId_t thread, const char *name, uint32_t stack_words);
void        SUP_Heartbeat(TaskId id);
bool        SUP_RequestHang(const char *name);
void        SUP_KickWatchdog(void);
void        SUP_PrintStats(void);
bool        SUP_WasWatchdogReset(void);
const char *SUP_ResetText(void);

void        SUP_FatalReset(ResetReason reason, const char *task_name) __attribute__((noreturn));

#endif
