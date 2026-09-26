#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include <stdbool.h>
#include <stdint.h>
#include "cmsis_os2.h"

/* Task supervisor: every task reports a heartbeat; tSuper refreshes the IWDG only while all of
 * them are alive. One hung task -> no refresh -> hardware reset ~4 s later. The reason is kept
 * in an RTC backup register and reported after the reboot. */

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

/* Before the scheduler starts: reads and clears the reset flags. */
void        SUP_Init(void);
void        SUP_Register(TaskId id, osThreadId_t thread, const char *name, uint32_t stack_words);
void        SUP_Heartbeat(TaskId id);
/* Test hook for the debug console: the task blocks forever at its next heartbeat. */
bool        SUP_RequestHang(const char *name);
/* Feeds the watchdog right before a long CPU stall (Flash sector erase). */
void        SUP_KickBeforeLongOp(void);
void        SUP_PrintStats(void);
bool        SUP_WasWatchdogReset(void);
const char *SUP_ResetText(void);

/* Record the reason and reset. Safe from fault handlers and kernel hooks. */
void        SUP_FatalReset(ResetReason reason, const char *task_name) __attribute__((noreturn));

#endif
