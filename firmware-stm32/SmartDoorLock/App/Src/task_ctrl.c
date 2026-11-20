#include <stdio.h>
#include <string.h>
#include "main.h"
#include "tim.h"
#include "lock.h"
#include "buzzer.h"
#include "access_ctrl.h"
#include "access_io.h"
#include "access_list.h"
#include "app_config.h"
#include "app_rtos.h"
#include "app_time.h"
#include "event_log.h"
#include "logger.h"
#include "menu.h"
#include "storage.h"
#include "supervisor.h"
#include "timeutil.h"
#include "ui.h"

static AccessCtx   s_ac;
static AccessList  s_cards;
static EventLog    s_events;
static LOCK_Handle s_lock;
static BUZZ_Handle s_buzz;

static uint32_t now_ms(void)
{
    return osKernelGetTickCount();
}

static void uid_to_hex(const uint8_t *uid, uint8_t len, char *out, size_t out_size)
{
    out[0] = '\0';
    for (uint8_t i = 0; i < len && i * 2U + 2U < out_size; i++) {
        snprintf(&out[i * 2U], out_size - i * 2U, "%02X", uid[i]);
    }
}

static void time_to_text(const EventRecord *r, char *out, size_t out_size)
{
    if (r->flags & ELOG_FLAG_TIME_VALID) {
        TU_DateTime dt;
        TU_FromEpoch(r->ts, &dt);
        snprintf(out, out_size, "%02u-%02u %02u:%02u:%02u", dt.month, dt.day, dt.hour, dt.minute, dt.second);
    } else {
        snprintf(out, out_size, "+%lus", (unsigned long)r->ts);
    }
}

void IO_Lock(bool open)
{
    if (open) {
        LOCK_Open(&s_lock);
    } else {
        LOCK_Close(&s_lock);
    }
}

void IO_Sound(BUZZ_Pattern pattern)
{
    BUZZER_Play(&s_buzz, pattern);
}

void IO_Timer(uint32_t ms)
{
    if (ms == 0U) {
        (void)osTimerStop(tmrState);
    } else {
        (void)osTimerStart(tmrState, ms);
    }
}

static void send_to_net(const char *line)
{
    NetLine l;
    snprintf(l.line, sizeof(l.line), "%s", line);
    (void)osMessageQueuePut(qNetTx, &l, 0, 0);
}

void IO_Log(LogEventType type, const uint8_t *uid, uint8_t uid_len)
{
    uint32_t ts = TIME_Now();
    bool real_time = (ts != 0U);
    if (!real_time) {
        ts = now_ms() / 1000U;
    }
    ELOG_Add(&s_events, type, uid, uid_len, ts, real_time);

    char hex[2 * UID_MAX_LEN + 1];
    uid_to_hex(uid, uid_len, hex, sizeof(hex));
    LOG_INFO("event %s %s", ELOG_TypeName(type), hex);

    char line[CFG_NET_LINE_LEN];
    if (type == LOGE_ACCESS_GRANTED || type == LOGE_ACCESS_DENIED || type == LOGE_LOCKOUT ||
        type == LOGE_ADMIN_RECOVERY) {
        snprintf(line, sizeof(line), "EVT:%s:%s", ELOG_TypeName(type), hex);
        send_to_net(line);
    }
}

bool IO_SaveCards(void)
{
    AL_Status st = AL_Save(&s_cards);
    if (st != AL_OK) {
        LOG_ERR("saving cards to Flash failed: %s", AL_StatusStr(st));
    }
    return st == AL_OK;
}

void IO_Screen(void)
{
    UiCommand u;
    memset(&u, 0, sizeof(u));
    u.screen = AC_Screen(&s_ac);
    u.result = (uint8_t)s_ac.result;

    switch (u.screen) {
    case SCR_MENU:
        u.sel = s_ac.menu_pos;
        u.count = MENU_COUNT;
        break;
    case SCR_DELETE:
        u.sel = s_ac.list_pos;
        u.count = (uint8_t)s_cards.count;
        if (s_ac.list_pos < s_cards.count) {
            const AL_Card *card = &s_cards.cards[s_ac.list_pos];
            snprintf(u.line1, sizeof(u.line1), "%.*s", (int)CFG_CARD_NAME_LEN, card->name);
            uid_to_hex(card->uid, card->uid_len, u.line2, sizeof(u.line2));
        }
        break;
    case SCR_LOG: {
        u.sel = s_ac.list_pos;
        u.count = (uint8_t)ELOG_Count(&s_events);
        const EventRecord *r = ELOG_Get(&s_events, s_ac.list_pos);
        if (r != NULL) {
            time_to_text(r, u.line1, sizeof(u.line1));
            u.value = r->type;
            uid_to_hex(r->uid, r->uid_len, u.line2, sizeof(u.line2));
        }
        break;
    }
    case SCR_SETTINGS:
        u.value = s_ac.edit_seconds;
        break;
    case SCR_LOCKOUT:
        u.value = (uint16_t)AC_SecondsLeft(&s_ac, now_ms());
        break;
    case SCR_ENROLL:
        u.value = s_ac.adding_admin ? 1U : 0U;
        break;
    case SCR_RECOVERY_CODE:
        u.value = s_ac.code_pos;
        u.count = CFG_RECOVERY_CODE_LEN;
        break;
    default:
        break;
    }

    /* Черга на 4 команди. Якщо переповнена — викидаємо найстарішу: важливий лише останній стан. */
    if (osMessageQueuePut(qUi, &u, 0, 0) != osOK) {
        UiCommand oldest;
        (void)osMessageQueueGet(qUi, &oldest, NULL, 0);
        (void)osMessageQueuePut(qUi, &u, 0, 0);
    }
}

static void console_cards(void)
{
    LOG_Raw("%u cards, unlock time %u s:", s_cards.count, s_cards.unlock_time_s);
    for (uint16_t i = 0; i < s_cards.count; i++) {
        const AL_Card *card = &s_cards.cards[i];
        char hex[2 * UID_MAX_LEN + 1];
        uid_to_hex(card->uid, card->uid_len, hex, sizeof(hex));
        LOG_Raw("  %2u  %-12.12s %-14s %s", i, card->name, hex, (card->flags & AL_FLAG_ADMIN) ? "admin" : "");
        if (i % 8U == 7U) {
            osDelay(30);
        }
    }
}

static void console_log(void)
{
    LOG_Raw("%u events (newest first):", ELOG_Count(&s_events));
    for (uint16_t i = 0; i < ELOG_Count(&s_events); i++) {
        const EventRecord *r = ELOG_Get(&s_events, i);
        char when[20];
        char hex[2 * UID_MAX_LEN + 1];
        time_to_text(r, when, sizeof(when));
        uid_to_hex(r->uid, r->uid_len, hex, sizeof(hex));
        LOG_Raw("  %-14s %-15s %s", when, ELOG_TypeName((LogEventType)r->type), hex);
        if (i % 8U == 7U) {
            osDelay(30);
        }
    }
}

static void console_time(uint32_t new_time)
{
    if (new_time != 0U) {
        LOG_Raw(TIME_Set(new_time) ? "time set" : "RTC not ready");
    }
    uint32_t t = TIME_Now();
    if (t == 0U) {
        LOG_Raw("time not set (RTC on %s)", TIME_SourceName());
        return;
    }
    TU_DateTime dt;
    char text[24];
    TU_FromEpoch(t, &dt);
    TU_Format(&dt, text, sizeof(text));
    LOG_Raw("time %s UTC (RTC on %s)", text, TIME_SourceName());
}

static void console_status(void)
{
    LOG_Raw("state %s, lock %s, reader %s, cards %u (admins %u), unlock %u s",
            AC_StateName(s_ac.state), LOCK_IsOpen(&s_lock) ? "OPEN" : "closed",
            s_ac.reader_fault ? "FAULT" : "ok", s_cards.count, AL_AdminCount(&s_cards), s_cards.unlock_time_s);
}

static void handle_console(const AppEvent *ev)
{
    switch (ev->console_cmd) {
    case CON_CARDS:  console_cards(); break;
    case CON_LOG:    console_log(); break;
    case CON_TIME:   console_time(ev->console_arg); break;
    case CON_STATUS: console_status(); break;
    default:         break;
    }
}

static void load_cards(void)
{
    if (!STORE_IsSafe()) {
        LOG_ERR("firmware reaches into the card sector: saving disabled, check the linker script");
    }
    AL_Status st = AL_Load(&s_cards);
    if (st == AL_OK) {
        LOG_INFO("cards loaded: %u (admins %u)", s_cards.count, AL_AdminCount(&s_cards));
    } else if (st == AL_EMPTY) {
        LOG_INFO("no cards yet: setup mode (hold the joystick button, then the card and the code)");
    } else {
        LOG_ERR("card list rejected: %s", AL_StatusStr(st));
        IO_Log(LOGE_STORAGE_CORRUPT, NULL, 0);
    }
}

static void refresh_timed_screens(void)
{
    static uint32_t shown_seconds;
    uint32_t now = now_ms();

    if (s_ac.state == AC_LOCKOUT && AC_SecondsLeft(&s_ac, now) != shown_seconds) {
        shown_seconds = AC_SecondsLeft(&s_ac, now);
        IO_Screen();
    }
}

void TaskCtrl(void *arg)
{
    (void)arg;
    LOCK_Init(&s_lock, LOCK_CTRL_GPIO_Port, LOCK_CTRL_Pin, CFG_LOCK_ENERGIZE_TO_OPEN);
    if (!BUZZER_Init(&s_buzz, &htim3, TIM_CHANNEL_1)) {
        LOG_ERR("buzzer init failed");
    }

    ELOG_Init(&s_events);
    IO_Log(LOGE_BOOT, NULL, 0);
    if (SUP_WasWatchdogReset()) {
        IO_Log(LOGE_WATCHDOG_RESET, NULL, 0);
    }
    load_cards();

    BUZZER_Play(&s_buzz, BUZZ_BOOT);
    for (int i = 0; i < 70; i++) {
        BUZZER_Update(&s_buzz);
        osDelay(10);
    }

    AC_Init(&s_ac, &s_cards, &s_events);
    AC_Start(&s_ac, now_ms());
    LOG_INFO("ready: state %s", AC_StateName(s_ac.state));

    for (;;) {
        SUP_Heartbeat(TASK_CTRL);

        uint32_t wait_ms = BUZZER_IsPlaying(&s_buzz) ? 10U : 100U;
        AppEvent ev;
        if (osMessageQueueGet(qCtrl, &ev, NULL, wait_ms) == osOK) {
            if (ev.type == EV_CONSOLE) {
                handle_console(&ev);
            } else {
                AccessState before = s_ac.state;
                AC_HandleEvent(&s_ac, &ev, now_ms());
                if (s_ac.state != before) {
                    LOG_INFO("state %s -> %s", AC_StateName(before), AC_StateName(s_ac.state));
                    char line[CFG_NET_LINE_LEN];
                    snprintf(line, sizeof(line), "STATE:%s", AC_StateName(s_ac.state));
                    send_to_net(line);
                }
            }
        }

        BUZZER_Update(&s_buzz);
        refresh_timed_screens();
    }
}
