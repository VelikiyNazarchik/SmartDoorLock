#include <stdio.h>
#include <string.h>
#include "main.h"
#include "iwdg.h"
#include "FreeRTOS.h"
#include "task.h"
#include "app_config.h"
#include "app_time.h"
#include "logger.h"
#include "supervisor.h"

/* Backup-регістр 0 переживає reset. Формат: 0x5AFE | причина << 8 | номер задачі. */
#define BKP_REASON    0U
#define BKP_MAGIC     0x5AFE0000UL
#define NO_TASK       0xFFU

static osThreadId_t      s_thread[TASK_COUNT];
static const char       *s_name[TASK_COUNT];
static uint32_t          s_stack_words[TASK_COUNT];
static volatile uint32_t s_last_beat[TASK_COUNT];
static volatile uint32_t s_hang_request;
static osThreadId_t      s_super_thread;

static uint32_t s_reset_flags;
static uint32_t s_reset_bkp;
static char     s_reset_text[80];

void SUP_Init(void)
{
    s_reset_flags = RCC->CSR;
    RCC->CSR |= RCC_CSR_RMVF;
    s_reset_bkp = TIME_BkpRead(BKP_REASON);
    TIME_BkpWrite(BKP_REASON, 0);
}

static void save_reason(ResetReason reason, uint32_t task)
{
    TIME_BkpWrite(BKP_REASON, BKP_MAGIC | ((uint32_t)reason << 8) | task);
}

bool SUP_WasWatchdogReset(void)
{
    return (s_reset_flags & RCC_CSR_IWDGRSTF) != 0U;
}

const char *SUP_ResetText(void)
{
    const char *cause = "unknown";
    if (s_reset_flags & RCC_CSR_IWDGRSTF) {
        cause = "watchdog";
    } else if (s_reset_flags & RCC_CSR_SFTRSTF) {
        cause = "software";
    } else if (s_reset_flags & (RCC_CSR_PORRSTF | RCC_CSR_BORRSTF)) {
        cause = "power-on";
    } else if (s_reset_flags & RCC_CSR_PINRSTF) {
        cause = "reset pin";
    }

    static const char *const reasons[] = { "", "hung", "stack overflow", "out of heap", "hard fault", "console reboot" };
    uint32_t reason = (s_reset_bkp >> 8) & 0xFFU;
    uint32_t task = s_reset_bkp & 0xFFU;
    bool ours = (s_reset_bkp & 0xFFFF0000UL) == BKP_MAGIC && reason < sizeof(reasons) / sizeof(reasons[0]);

    if (!ours) {
        snprintf(s_reset_text, sizeof(s_reset_text), "%s", cause);
    } else if (task < TASK_COUNT && s_name[task] != NULL) {
        snprintf(s_reset_text, sizeof(s_reset_text), "%s (task %s: %s)", cause, s_name[task], reasons[reason]);
    } else {
        snprintf(s_reset_text, sizeof(s_reset_text), "%s (%s)", cause, reasons[reason]);
    }
    return s_reset_text;
}

void SUP_FatalReset(ResetReason reason, const char *task_name)
{
    uint32_t task = NO_TASK;
    for (uint32_t i = 0; i < TASK_COUNT && task_name != NULL; i++) {
        if (s_name[i] != NULL && strcmp(s_name[i], task_name) == 0) {
            task = i;
        }
    }
    save_reason(reason, task);
    if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) {
        __BKPT(0);   /* під налагоджувачем зупиняємось тут, щоб подивитись Fault Analyzer */
    }
    NVIC_SystemReset();
}

void SUP_Register(TaskId id, osThreadId_t thread, const char *name, uint32_t stack_words)
{
    s_thread[id] = thread;
    s_name[id] = name;
    s_stack_words[id] = stack_words;
}

void SUP_Heartbeat(TaskId id)
{
    if (s_hang_request & (1UL << id)) {
        LOG_WARN("simulating a hang: this task stops sending heartbeats");
        for (;;) {
            osDelay(1000);
        }
    }
    s_last_beat[id] = osKernelGetTickCount();
}

bool SUP_RequestHang(const char *name)
{
    for (uint32_t i = 0; i < TASK_COUNT; i++) {
        if (s_name[i] != NULL && strcmp(s_name[i], name) == 0) {
            s_hang_request |= 1UL << i;
            return true;
        }
    }
    return false;
}

void SUP_KickWatchdog(void)
{
    (void)HAL_IWDG_Refresh(&hiwdg);
}

static int find_silent_task(uint32_t now)
{
    for (int i = 0; i < TASK_COUNT; i++) {
        if (s_thread[i] != NULL && (now - s_last_beat[i]) > CFG_TASK_TIMEOUT_MS) {
            return i;
        }
    }
    return -1;
}

static void print_stack(const char *name, osThreadId_t thread, uint32_t words)
{
    uint32_t free_words = uxTaskGetStackHighWaterMark((TaskHandle_t)thread);
    uint32_t used = words - free_words;
    LOG_Raw("  %-7s stack %4lu/%4lu words (%2lu%%)", name,
            (unsigned long)used, (unsigned long)words, (unsigned long)(used * 100U / words));
}

void SUP_PrintStats(void)
{
    LOG_Raw("--- uptime %lus, heap free %u B (min %u B), log lines dropped %lu",
            (unsigned long)(HAL_GetTick() / 1000U), (unsigned)xPortGetFreeHeapSize(),
            (unsigned)xPortGetMinimumEverFreeHeapSize(), (unsigned long)LOG_Dropped());
    for (int i = 0; i < TASK_COUNT; i++) {
        if (s_thread[i] != NULL) {
            print_stack(s_name[i], s_thread[i], s_stack_words[i]);
        }
    }
    if (s_super_thread != NULL) {
        print_stack("tSuper", s_super_thread, CFG_STACK_SUPER);
    }
}

static void set_led(bool on)
{
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void wait_ms(uint32_t ms)
{
    osDelay(ms);
}

void TaskSuper(void *arg)
{
    (void)arg;
    s_super_thread = osThreadGetId();

    /* Під налагоджувачем зупинка на breakpoint не повинна перезавантажувати плату. */
    if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) {
        __HAL_DBGMCU_FREEZE_IWDG();
    }
    SUP_KickWatchdog();

    (void)TIME_Init(wait_ms, SUP_KickWatchdog);
    LOG_INFO("SmartDoorLock %s, reset cause: %s", APP_VERSION, SUP_ResetText());
    LOG_INFO("RTC on %s, calendar %s", TIME_SourceName(), TIME_IsSet() ? "set" : "not set");

    uint32_t now = osKernelGetTickCount();
    for (int i = 0; i < TASK_COUNT; i++) {
        s_last_beat[i] = now;
    }
    uint32_t next_stats = now + CFG_STATS_PERIOD_MS;
    bool reported = false;

    for (;;) {
        osDelay(CFG_SUPERVISOR_PERIOD_MS);
        now = osKernelGetTickCount();

        int silent = find_silent_task(now);
        if (silent < 0) {
            SUP_KickWatchdog();
            set_led((now / 500U) % 2U == 0U);
        } else if (!reported) {
            reported = true;
            set_led(true);
            save_reason(RESET_REASON_TASK_HUNG, (uint32_t)silent);
            LOG_ERR("task %s is silent: watchdog is no longer fed, reset follows", s_name[silent]);
        }

        if ((int32_t)(now - next_stats) >= 0) {
            next_stats = now + CFG_STATS_PERIOD_MS;
            SUP_PrintStats();
        }
    }
}
