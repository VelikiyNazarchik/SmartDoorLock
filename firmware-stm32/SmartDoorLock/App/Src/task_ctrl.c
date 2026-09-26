#include <stdio.h>
#include <string.h>
#include "main.h"
#include "tim.h"
#include "lock.h"
#include "buzzer.h"
#include "access_ctrl.h"
#include "access_list.h"
#include "app_config.h"
#include "app_rtos.h"
#include "app_time.h"
#include "event_log.h"
#include "flash_store.h"
#include "logger.h"
#include "menu.h"
#include "supervisor.h"
#include "timeutil.h"
#include "ui.h"

/* tCtrl: the brain. The only place where decisions are made (via access_ctrl) and the owner of
 * the lock output, the buzzer (TIM3), the card storage in Flash, the event log and the RTC. */

static AccessCtx   s_ac;
static AccessList  s_al;
static AccessList  s_al_backup;   /* rollback copy if a Flash write fails */
static EventLog    s_log;
static LOCK_Handle s_lock;
static BUZZ_Handle s_buzz;

/* ------------------------------------------------------------------ sounds */

static const BUZZ_Note n_click[]   = { { CFG_BEEP_CLICK_HZ, 12 }, { 0, 0 } };
static const BUZZ_Note n_ok[]      = { { CFG_BEEP_OK_HZ, 80 }, { 0, 60 }, { CFG_BEEP_OK_HZ, 80 }, { 0, 0 } };
static const BUZZ_Note n_denied[]  = { { CFG_BEEP_DENIED_HZ, 600 }, { 0, 0 } };
static const BUZZ_Note n_lockout[] = { { CFG_BEEP_ALARM_HZ, 250 }, { CFG_BEEP_ALARM_LOW_HZ, 250 }, { 0, 0 } };
static const BUZZ_Note n_boot[]    = { { 1500, 70 }, { 2000, 70 }, { 2600, 110 }, { 0, 0 } };
static const BUZZ_Note n_error[]   = { { CFG_BEEP_DENIED_HZ, 90 }, { 0, 70 }, { 0, 0 } };

static const BUZZ_Melody k_melodies[BUZZ_PATTERN_COUNT] = {
    [BUZZ_NONE]    = { NULL, 0 },
    [BUZZ_CLICK]   = { n_click, 1 },
    [BUZZ_OK]      = { n_ok, 1 },
    [BUZZ_DENIED]  = { n_denied, 1 },
    [BUZZ_LOCKOUT] = { n_lockout, 6 },
    [BUZZ_BOOT]    = { n_boot, 1 },
    [BUZZ_ERROR]   = { n_error, 3 },
};

/* ------------------------------------------------------------------ helpers */

static uint32_t now_ms(void)
{
    return osKernelGetTickCount();   /* 1 kHz tick */
}

static void uid_hex(const uint8_t *uid, uint8_t len, char *out, size_t n)
{
    out[0] = '\0';
    for (uint8_t i = 0; i < len && (size_t)(i * 2U + 2U) < n; i++) {
        (void)snprintf(&out[i * 2U], n - i * 2U, "%02X", uid[i]);
    }
}

static void format_ts(const EventRecord *r, char *out, size_t n)
{
    if (r->flags & ELOG_FLAG_TIME_VALID) {
        TU_DateTime dt;
        TU_FromEpoch(r->ts, &dt);
        (void)snprintf(out, n, "%02u-%02u %02u:%02u:%02u", (unsigned)dt.month, (unsigned)dt.day,
                       (unsigned)dt.hour, (unsigned)dt.minute, (unsigned)dt.second);
    } else {
        (void)snprintf(out, n, "+%lus", (unsigned long)r->ts);
    }
}

static void update_counts(void)
{
    AC_SetCounts(&s_ac, AL_Count(&s_al), AL_AdminCount(&s_al), ELOG_Count(&s_log));
}

static void add_log(LogEventType type, const uint8_t *uid, uint8_t len)
{
    uint32_t ts = TIME_Now();
    bool real = (ts != 0U);
    if (!real) {
        ts = now_ms() / 1000U;
    }
    ELOG_Add(&s_log, type, uid, len, ts, real);
    update_counts();

    char hex[2 * UID_MAX_LEN + 1];
    uid_hex(uid, len, hex, sizeof(hex));
    LOG_INFO("event %s%s%s", ELOG_TypeName(type), (len > 0U) ? " uid=" : "", hex);
}

/* ------------------------------------------------------------------ outputs */

static void send_ui(void)
{
    UiCommand u;
    memset(&u, 0, sizeof(u));
    u.screen = AC_Screen(&s_ac);
    u.result = (uint8_t)s_ac.result;

    switch (u.screen) {
    case SCR_MENU:
        u.sel = s_ac.menu_sel;
        u.count = MENU_COUNT;
        break;
    case SCR_DELETE: {
        u.sel = s_ac.list_sel;
        u.count = (uint8_t)AL_Count(&s_al);
        const AL_Card *c = AL_Get(&s_al, s_ac.list_sel);
        if (c != NULL) {
            (void)snprintf(u.line1, sizeof(u.line1), "%.*s", (int)CFG_CARD_NAME_LEN, c->name);
            uid_hex(c->uid, c->uid_len, u.line2, sizeof(u.line2));
        }
        break;
    }
    case SCR_LOG: {
        u.sel = s_ac.list_sel;
        u.count = (uint8_t)ELOG_Count(&s_log);
        const EventRecord *r = ELOG_Get(&s_log, s_ac.list_sel);
        if (r != NULL) {
            format_ts(r, u.line1, sizeof(u.line1));
            u.value = r->type;
            uid_hex(r->uid, r->uid_len, u.line2, sizeof(u.line2));
        }
        break;
    }
    case SCR_SETTINGS:
        u.value = s_ac.edit_unlock_s;
        break;
    case SCR_LOCKOUT:
        u.value = (uint16_t)AC_RemainingS(&s_ac, now_ms());
        break;
    case SCR_GRANTED:
        u.value = s_ac.unlock_time_s;
        break;
    case SCR_SETUP:
        u.value = AC_SetupArmed(&s_ac, now_ms()) ? 1U : 0U;
        break;
    default:
        break;
    }

    if (osMessageQueuePut(qUi, &u, 0, 0) != osOK) {
        UiCommand stale;
        (void)osMessageQueueGet(qUi, &stale, NULL, 0);   /* drop the oldest, the newest wins */
        (void)osMessageQueuePut(qUi, &u, 0, 0);
    }
}

static void notify_net(const AccessActions *a)
{
    NetLine l;
    char hex[2 * UID_MAX_LEN + 1];
    uid_hex(a->uid, a->uid_len, hex, sizeof(hex));
    if (a->log && a->log_type == LOGE_ACCESS_GRANTED) {
        (void)snprintf(l.line, sizeof(l.line), "EVT:ACCESS_OK:%s", hex);
        (void)osMessageQueuePut(qNetTx, &l, 0, 0);
    } else if (a->log && a->log_type == LOGE_ACCESS_DENIED) {
        (void)snprintf(l.line, sizeof(l.line), "EVT:ACCESS_DENIED:%s", hex);
        (void)osMessageQueuePut(qNetTx, &l, 0, 0);
    } else if (a->log && a->log_type == LOGE_LOCKOUT) {
        (void)snprintf(l.line, sizeof(l.line), "EVT:LOCKOUT");
        (void)osMessageQueuePut(qNetTx, &l, 0, 0);
    }
    (void)snprintf(l.line, sizeof(l.line), "STATE:%s", AC_StateName(AC_GetState(&s_ac)));
    (void)osMessageQueuePut(qNetTx, &l, 0, 0);
}

/* ------------------------------------------------------------------ storage commands */

static UiResult to_ui_result(AL_Status st)
{
    switch (st) {
    case AL_ERR_EXISTS:     return RES_ALREADY_EXISTS;
    case AL_ERR_FULL:       return RES_LIST_FULL;
    case AL_ERR_LAST_ADMIN: return RES_LAST_ADMIN;
    case AL_ERR_NOT_FOUND:  return RES_LIST_EMPTY;
    default:                return RES_STORAGE_ERROR;
    }
}

static void execute(const AccessActions *a);

static AL_Status save_or_rollback(void)
{
    AL_Status st = AL_Save(&s_al);
    if (st != AL_OK) {
        LOG_ERR("saving the card list failed (%s), change rolled back", AL_StatusStr(st));
        memcpy(&s_al, &s_al_backup, sizeof(s_al));
    }
    return st;
}

static void run_command(const AccessActions *a)
{
    AppEvent r;
    memset(&r, 0, sizeof(r));
    r.type = EV_CMD_RESULT;
    AL_Status st = AL_ERR_PARAM;
    memcpy(&s_al_backup, &s_al, sizeof(s_al));

    switch (a->cmd) {
    case AC_CMD_ENROLL_USER:
    case AC_CMD_ENROLL_ADMIN:
        st = AL_Add(&s_al, a->uid, a->uid_len, a->cmd == AC_CMD_ENROLL_ADMIN);
        if (st == AL_OK) {
            st = save_or_rollback();
        }
        memcpy(r.result.uid, a->uid, a->uid_len);
        r.result.uid_len = a->uid_len;
        break;
    case AC_CMD_DELETE: {
        const AL_Card *c = AL_Get(&s_al, a->cmd_arg);
        if (c != NULL) {
            memcpy(r.result.uid, c->uid, c->uid_len);
            r.result.uid_len = c->uid_len;
        }
        st = AL_RemoveAt(&s_al, a->cmd_arg);
        if (st == AL_OK) {
            st = save_or_rollback();
        }
        break;
    }
    case AC_CMD_SAVE_SETTINGS:
        AL_SetUnlockTime(&s_al, a->cmd_arg);
        st = save_or_rollback();
        break;
    default:
        return;
    }

    LOG_INFO("storage command %d: %s", (int)a->cmd, AL_StatusStr(st));
    r.result.ok = (st == AL_OK);
    r.result.reason = (uint8_t)to_ui_result(st);
    update_counts();
    AccessActions next = AC_HandleEvent(&s_ac, &r, now_ms());
    execute(&next);
}

/* ------------------------------------------------------------------ action executor */

static void execute(const AccessActions *a)
{
    if (a->lock == LOCK_ACT_OPEN) {
        LOCK_Open(&s_lock);
    } else if (a->lock == LOCK_ACT_CLOSE) {
        LOCK_Close(&s_lock);
    }
    if (a->sound != BUZZ_NONE) {
        BUZZER_Play(&s_buzz, a->sound);
    }
    if (a->timer_ms == 0) {
        (void)osTimerStop(tmrState);
    } else if (a->timer_ms > 0) {
        (void)osTimerStart(tmrState, (uint32_t)a->timer_ms);
    }
    if (a->log) {
        add_log(a->log_type, a->uid, a->uid_len);
    }
    if (a->notify_net) {
        notify_net(a);
    }
    if (a->redraw) {
        send_ui();
    }
    if (a->cmd != AC_CMD_NONE) {
        run_command(a);
    }
}

/* ------------------------------------------------------------------ console requests */

static void console_cards(void)
{
    LOG_Raw("%u cards, unlock time %u s:", (unsigned)AL_Count(&s_al), (unsigned)s_al.unlock_time_s);
    for (uint16_t i = 0; i < AL_Count(&s_al); i++) {
        const AL_Card *c = AL_Get(&s_al, i);
        char hex[2 * UID_MAX_LEN + 1];
        uid_hex(c->uid, c->uid_len, hex, sizeof(hex));
        LOG_Raw("  %2u  %-12.12s %-14s %s", (unsigned)i, c->name, hex, (c->flags & AL_FLAG_ADMIN) ? "admin" : "");
        if ((i % 8U) == 7U) {
            osDelay(30);   /* let tLog drain the queue */
        }
    }
}

static void console_log(void)
{
    LOG_Raw("%u events (newest first):", (unsigned)ELOG_Count(&s_log));
    for (uint16_t i = 0; i < ELOG_Count(&s_log); i++) {
        const EventRecord *r = ELOG_Get(&s_log, i);
        char ts[20];
        char hex[2 * UID_MAX_LEN + 1];
        format_ts(r, ts, sizeof(ts));
        uid_hex(r->uid, r->uid_len, hex, sizeof(hex));
        LOG_Raw("  %-14s %-15s %s", ts, ELOG_TypeName((LogEventType)r->type), hex);
        if ((i % 8U) == 7U) {
            osDelay(30);
        }
    }
}

static void console_time(uint32_t epoch)
{
    if (epoch != 0U) {
        LOG_Raw(TIME_Set(epoch) ? "time set" : "RTC not ready, time not set");
    }
    uint32_t t = TIME_Now();
    if (t == 0U) {
        LOG_Raw("time not set (RTC on %s)", TIME_SourceName());
        return;
    }
    TU_DateTime dt;
    char buf[24];
    TU_FromEpoch(t, &dt);
    TU_Format(&dt, buf, sizeof(buf));
    LOG_Raw("time %s UTC (RTC on %s)", buf, TIME_SourceName());
}

static void console_status(void)
{
    LOG_Raw("state %s, lock %s, reader %s, cards %u (admins %u), unlock %u s",
            AC_StateName(AC_GetState(&s_ac)), LOCK_IsOpen(&s_lock) ? "OPEN" : "closed",
            s_ac.rfid_fault ? "FAULT" : "ok", (unsigned)AL_Count(&s_al), (unsigned)AL_AdminCount(&s_al),
            (unsigned)s_ac.unlock_time_s);
}

static void console(const AppEvent *ev)
{
    switch ((ConsoleCmd)ev->console.cmd) {
    case CON_CMD_CARDS:    console_cards(); break;
    case CON_CMD_LOG:      console_log(); break;
    case CON_CMD_TIME_SET: console_time(ev->console.arg); break;
    case CON_CMD_STATUS:   console_status(); break;
    default:               break;
    }
}

/* ------------------------------------------------------------------ task */

static void handle_event(AppEvent *ev)
{
    if (ev->type == EV_CONSOLE) {
        console(ev);
        return;
    }
    if (ev->type == EV_CARD) {
        ev->card.cls = AL_Classify(&s_al, ev->card.uid, ev->card.len);
    }
    AccessState before = AC_GetState(&s_ac);
    AccessActions a = AC_HandleEvent(&s_ac, ev, now_ms());
    execute(&a);
    AccessState after = AC_GetState(&s_ac);
    if (after != before) {
        LOG_INFO("state %s -> %s", AC_StateName(before), AC_StateName(after));
    }
}

static void load_cards(void)
{
    if (!FLASH_STORE_IsSafe()) {
        LOG_ERR("firmware reaches into the card sector: saving disabled, check the linker script");
    }
    AL_Init(&s_al, FLASH_STORE_Get());
    AL_Status st = AL_Load(&s_al);
    switch (st) {
    case AL_OK:
        LOG_INFO("card list loaded: %u cards, %u admin", (unsigned)AL_Count(&s_al), (unsigned)AL_AdminCount(&s_al));
        break;
    case AL_EMPTY:
        LOG_INFO("no card list yet: setup mode (exit button, then the admin card)");
        break;
    case AL_ERR_CORRUPT:
        LOG_ERR("card list failed its CRC check and was ignored");
        add_log(LOGE_STORAGE_CORRUPT, NULL, 0);
        break;
    default:
        LOG_ERR("card list read error: %s", AL_StatusStr(st));
        break;
    }
}

void TaskCtrl(void *arg)
{
    (void)arg;
    (void)LOCK_Init(&s_lock, LOCK_CTRL_GPIO_Port, LOCK_CTRL_Pin, CFG_LOCK_ENERGIZE_TO_OPEN);
    if (BUZZER_Init(&s_buzz, &htim3, TIM_CHANNEL_1, CFG_BUZZER_TICK_HZ, k_melodies) != BUZZ_STATUS_OK) {
        LOG_ERR("buzzer init failed");
    }
    FLASH_STORE_SetBeforeErase(SUP_KickBeforeLongOp);

    ELOG_Init(&s_log);
    add_log(LOGE_BOOT, NULL, 0);
    if (SUP_WasWatchdogReset()) {
        add_log(LOGE_WATCHDOG_RESET, NULL, 0);
    }
    load_cards();

    BUZZER_Play(&s_buzz, BUZZ_BOOT);
    AC_Init(&s_ac, s_al.unlock_time_s);
    update_counts();

    /* Keep the boot screen up for a moment while the start-up melody plays. */
    uint32_t boot_until = now_ms() + 700U;
    while ((int32_t)(now_ms() - boot_until) < 0) {
        BUZZER_Update(&s_buzz);
        osDelay(10);
    }
    AccessActions start = AC_Start(&s_ac, now_ms());
    execute(&start);
    LOG_INFO("ready: state %s, unlock time %u s", AC_StateName(AC_GetState(&s_ac)), (unsigned)s_ac.unlock_time_s);

    uint32_t shown_remaining = 0;
    bool shown_armed = false;
    for (;;) {
        SUP_Heartbeat(TASK_CTRL);
        AppEvent ev;
        uint32_t wait = BUZZER_IsPlaying(&s_buzz) ? 10U : 100U;
        if (osMessageQueueGet(qCtrl, &ev, NULL, wait) == osOK) {
            handle_event(&ev);
        }
        BUZZER_Update(&s_buzz);

        if (AC_GetState(&s_ac) == AC_LOCKOUT) {
            uint32_t left = AC_RemainingS(&s_ac, now_ms());
            if (left != shown_remaining) {
                shown_remaining = left;
                send_ui();
            }
        } else {
            shown_remaining = 0;
        }

        /* Setup hint follows the arming window (armed by the exit button, expires by itself). */
        bool armed = (AC_GetState(&s_ac) == AC_SETUP) && AC_SetupArmed(&s_ac, now_ms());
        if (AC_GetState(&s_ac) == AC_SETUP && armed != shown_armed) {
            send_ui();
        }
        shown_armed = armed;
    }
}
