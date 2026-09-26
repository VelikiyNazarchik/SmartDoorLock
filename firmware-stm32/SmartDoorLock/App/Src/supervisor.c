#include <stdio.h>
#include <string.h>
#include "main.h"
#include "iwdg.h"
#include "FreeRTOS.h"
#include "task.h"
#include "app_config.h"
#include "app_rtos.h"
#include "app_time.h"
#include "logger.h"
#include "supervisor.h"

/* RTC backup register 0: 0x5AFE | reason << 8 | task id. Survives the reset, cleared at boot. */
#define BKP_REASON_IDX   0U
#define BKP_MAGIC        0x5AFE0000UL
#define BKP_MAGIC_MASK   0xFFFF0000UL
#define TASK_ID_UNKNOWN  0xFFU
#define ALL_TASK_BITS    ((1UL << TASK_COUNT) - 1UL)

typedef struct {
    osThreadId_t th;
    const char  *name;
    uint32_t     stack_words;
    uint32_t     last_seen;
} TaskSlot;

static TaskSlot          s_tasks[TASK_COUNT];
static osEventFlagsId_t  s_hb;
static volatile uint32_t s_hang_mask;
static uint32_t          s_reset_flags;
static uint32_t          s_reset_bkp;
static char              s_reset_text[80];
static osThreadId_t      s_super_th;

static const char *const k_reason_text[] = {
    [RESET_REASON_NONE]           = "",
    [RESET_REASON_TASK_HUNG]      = "hung",
    [RESET_REASON_STACK_OVERFLOW] = "stack overflow",
    [RESET_REASON_MALLOC_FAILED]  = "out of heap",
    [RESET_REASON_HARDFAULT]      = "hard fault",
    [RESET_REASON_CONSOLE]        = "console reboot",
};

static const char *task_name(uint32_t id)
{
    return (id < TASK_COUNT && s_tasks[id].name != NULL) ? s_tasks[id].name : "?";
}

static void build_reset_text(void)
{
    const char *cause;
    if (s_reset_flags & RCC_CSR_IWDGRSTF) {
        cause = "watchdog";
    } else if (s_reset_flags & RCC_CSR_SFTRSTF) {
        cause = "software";
    } else if (s_reset_flags & (RCC_CSR_PORRSTF | RCC_CSR_BORRSTF)) {
        cause = "power-on";
    } else if (s_reset_flags & RCC_CSR_PINRSTF) {
        cause = "reset pin";
    } else {
        cause = "unknown";
    }

    if ((s_reset_bkp & BKP_MAGIC_MASK) == BKP_MAGIC) {
        uint32_t reason = (s_reset_bkp >> 8) & 0xFFU;
        uint32_t id = s_reset_bkp & 0xFFU;
        const char *rt = (reason < sizeof(k_reason_text) / sizeof(k_reason_text[0])) ? k_reason_text[reason] : "?";
        if (id == TASK_ID_UNKNOWN) {
            (void)snprintf(s_reset_text, sizeof(s_reset_text), "%s (%s)", cause, rt);
        } else {
            (void)snprintf(s_reset_text, sizeof(s_reset_text), "%s (task %s: %s)", cause, task_name(id), rt);
        }
    } else {
        (void)snprintf(s_reset_text, sizeof(s_reset_text), "%s", cause);
    }
}

void SUP_Init(void)
{
    s_reset_flags = RCC->CSR;
    RCC->CSR |= RCC_CSR_RMVF;
    s_reset_bkp = TIME_BkpRead(BKP_REASON_IDX);
    TIME_BkpWrite(BKP_REASON_IDX, 0);

    static const osEventFlagsAttr_t attr = { .name = "heartbeat" };
    s_hb = osEventFlagsNew(&attr);
}

void SUP_Register(TaskId id, osThreadId_t thread, const char *name, uint32_t stack_words)
{
    if (id >= TASK_COUNT) {
        return;
    }
    s_tasks[id].th = thread;
    s_tasks[id].name = name;
    s_tasks[id].stack_words = stack_words;
}

void SUP_Heartbeat(TaskId id)
{
    uint32_t bit = 1UL << id;
    if (s_hang_mask & bit) {
        s_hang_mask &= ~bit;
        LOG_WARN("simulating a hang: no more heartbeats from this task");
        for (;;) {
            osDelay(1000);
        }
    }
    (void)osEventFlagsSet(s_hb, bit);
}

bool SUP_RequestHang(const char *name)
{
    for (uint32_t i = 0; i < TASK_COUNT; i++) {
        if (s_tasks[i].name != NULL && strcmp(s_tasks[i].name, name) == 0) {
            s_hang_mask |= 1UL << i;
            return true;
        }
    }
    return false;
}

static void kick(void)
{
    (void)HAL_IWDG_Refresh(&hiwdg);
}

void SUP_KickBeforeLongOp(void)
{
    kick();
}

bool SUP_WasWatchdogReset(void)
{
    return (s_reset_flags & RCC_CSR_IWDGRSTF) != 0U;
}

const char *SUP_ResetText(void)
{
    if (s_reset_text[0] == '\0') {
        build_reset_text();
    }
    return s_reset_text;
}

static void print_stack(const char *name, osThreadId_t th, uint32_t total)
{
    if (th == NULL || total == 0U) {
        return;
    }
    uint32_t free_words = (uint32_t)uxTaskGetStackHighWaterMark((TaskHandle_t)th);
    uint32_t used = (free_words < total) ? total - free_words : 0U;
    LOG_Raw("  %-7s stack %4lu/%4lu words used (%2lu%%)", name,
            (unsigned long)used, (unsigned long)total, (unsigned long)(used * 100U / total));
}

void SUP_PrintStats(void)
{
    LOG_Raw("--- uptime %lus, heap free %u B (min %u B), log lines dropped %lu",
            (unsigned long)(HAL_GetTick() / 1000U), (unsigned)xPortGetFreeHeapSize(),
            (unsigned)xPortGetMinimumEverFreeHeapSize(), (unsigned long)LOG_Dropped());
    for (uint32_t i = 0; i < TASK_COUNT; i++) {
        print_stack(s_tasks[i].name, s_tasks[i].th, s_tasks[i].stack_words);
    }
    print_stack("tSuper", s_super_th, CFG_STACK_SUPER);
}

void SUP_FatalReset(ResetReason reason, const char *task_name_hint)
{
    uint32_t id = TASK_ID_UNKNOWN;
    if (task_name_hint != NULL) {
        for (uint32_t i = 0; i < TASK_COUNT; i++) {
            if (s_tasks[i].name != NULL && strcmp(s_tasks[i].name, task_name_hint) == 0) {
                id = i;
                break;
            }
        }
    }
    TIME_BkpWrite(BKP_REASON_IDX, BKP_MAGIC | ((uint32_t)reason << 8) | id);
    if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) {
        __BKPT(0);   /* stop here for the debugger / Fault Analyzer */
    }
    NVIC_SystemReset();
}

static void wait_ms(uint32_t ms)
{
    osDelay(ms);
}

void TaskSuper(void *arg)
{
    (void)arg;
    s_super_th = osThreadGetId();

    /* Halting on a breakpoint must not let the watchdog reset the board. */
    if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) {
        __HAL_DBGMCU_FREEZE_IWDG();
    }
    kick();

    (void)TIME_Init(wait_ms, kick);
    LOG_INFO("SmartDoorLock %s, reset cause: %s", APP_VERSION, SUP_ResetText());
    LOG_INFO("RTC on %s, calendar %s", TIME_SourceName(), TIME_IsSet() ? "set" : "not set");

    uint32_t now = osKernelGetTickCount();
    for (uint32_t i = 0; i < TASK_COUNT; i++) {
        s_tasks[i].last_seen = now;
    }
    uint32_t next_stats = now + CFG_STATS_PERIOD_MS;
    bool reported = false;

    for (;;) {
        osDelay(CFG_SUPERVISOR_PERIOD_MS);
        now = osKernelGetTickCount();
        uint32_t beats = osEventFlagsClear(s_hb, ALL_TASK_BITS);   /* returns the bits before clearing */
        if (beats & osFlagsError) {
            beats = 0;
        }

        int hung = -1;
        for (uint32_t i = 0; i < TASK_COUNT; i++) {
            if (s_tasks[i].th == NULL) {
                continue;
            }
            if (beats & (1UL << i)) {
                s_tasks[i].last_seen = now;
            } else if ((now - s_tasks[i].last_seen) > CFG_TASK_TIMEOUT_MS && hung < 0) {
                hung = (int)i;
            }
        }

        if (hung < 0) {
            kick();
            /* Alive LED (PC13, active low): 1 Hz blink while every task reports in. */
            if ((now / 500U) & 1U) {
                HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
            } else {
                HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
            }
        } else if (!reported) {
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);   /* steady on: reset pending */
            reported = true;
            TIME_BkpWrite(BKP_REASON_IDX, BKP_MAGIC | ((uint32_t)RESET_REASON_TASK_HUNG << 8) | (uint32_t)hung);
            LOG_ERR("task %s silent for %lu ms: watchdog no longer fed, reset follows",
                    task_name((uint32_t)hung), (unsigned long)(now - s_tasks[hung].last_seen));
        }

        if ((int32_t)(now - next_stats) >= 0) {
            next_stats = now + CFG_STATS_PERIOD_MS;
            SUP_PrintStats();
        }
    }
}
