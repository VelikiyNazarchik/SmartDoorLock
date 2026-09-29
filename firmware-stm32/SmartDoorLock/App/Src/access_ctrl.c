#include <string.h>
#include "access_ctrl.h"
#include "access_io.h"
#include "menu.h"

static AccessState base_state(const AccessCtx *c)
{
    if (c->reader_fault) {
        return AC_FAULT;
    }
    if (AL_AdminCount(c->cards) == 0U) {
        return AC_SETUP;
    }
    return AC_LOCKED;
}

static uint32_t state_duration_ms(const AccessCtx *c, AccessState s)
{
    switch (s) {
    case AC_UNLOCKED:         return (uint32_t)c->cards->unlock_time_s * 1000U;
    case AC_DENIED:           return CFG_DENIED_SHOW_MS;
    case AC_LOCKOUT:          return CFG_LOCKOUT_TIME_MS;
    case AC_ENROLL_WAIT_CARD: return CFG_ENROLL_WAIT_MS;
    case AC_RESULT:           return CFG_RESULT_SHOW_MS;
    case AC_ADMIN_MENU:
    case AC_DELETE_SELECT:
    case AC_LOG_VIEW:
    case AC_SETTINGS:         return CFG_MENU_IDLE_MS;
    default:                  return 0;
    }
}

/* Єдине місце, де змінюється стан. Замок, таймер і екран завжди відповідають стану.
 * Виклик з тим самим станом = "залишитись, перезапустити таймер, перемалювати". */
static void go_to(AccessCtx *c, AccessState s, uint32_t now)
{
    c->state = s;

    bool open = (s == AC_UNLOCKED);
    if (open != c->lock_open) {
        c->lock_open = open;
        IO_Lock(open);
    }

    uint32_t ms = state_duration_ms(c, s);
    c->timer_deadline = (ms > 0U) ? now + ms : 0U;
    IO_Timer(ms);
    IO_Screen();
}

static void show_result(AccessCtx *c, bool ok, UiResult r, AccessState after, uint32_t now)
{
    c->result = r;
    c->result_ok = ok;
    c->after_result = after;
    IO_Sound(ok ? BUZZ_OK : BUZZ_ERROR);
    go_to(c, AC_RESULT, now);
}

static bool add_failure(AccessCtx *c, uint32_t now)
{
    if (c->fail_count == CFG_LOCKOUT_ATTEMPTS) {
        memmove(&c->fail_times[0], &c->fail_times[1], (CFG_LOCKOUT_ATTEMPTS - 1U) * sizeof(uint32_t));
        c->fail_count--;
    }
    c->fail_times[c->fail_count++] = now;
    return c->fail_count == CFG_LOCKOUT_ATTEMPTS && (now - c->fail_times[0]) < CFG_LOCKOUT_WINDOW_MS;
}

/* ISO 14443-3: 4-байтовий UID, що починається з 0x08, — випадковий. Телефон або банківська
 * картка генерує його заново при кожному прикладанні, тож як ключ така картка не годиться. */
static bool uid_is_random(const AppEvent *ev)
{
    return ev->uid_len == 4U && ev->uid[0] == 0x08U;
}

static bool is_up_down(const AppEvent *ev)
{
    return ev->type == EV_JOY_DIR && (ev->dir == JOY_UP || ev->dir == JOY_DOWN);
}

static bool is_back(const AppEvent *ev)
{
    return ev->type == EV_JOY_DIR && ev->dir == JOY_LEFT;
}

static bool is_select(const AppEvent *ev)
{
    return ev->type == EV_JOY_CLICK || (ev->type == EV_JOY_DIR && ev->dir == JOY_RIGHT);
}

static UiResult result_for(AL_Status st)
{
    switch (st) {
    case AL_ERR_EXISTS:     return RES_ALREADY_EXISTS;
    case AL_ERR_FULL:       return RES_LIST_FULL;
    case AL_ERR_LAST_ADMIN: return RES_LAST_ADMIN;
    default:                return RES_STORAGE_ERROR;
    }
}

/* Будь-яка зміна списку: спочатку копія в undo, потім зміна в RAM, потім запис у Flash.
 * Якщо запис не вдався — повертаємо копію, щоб RAM і Flash не розійшлися. */
static bool save_or_undo(AccessCtx *c)
{
    if (IO_SaveCards()) {
        return true;
    }
    *c->cards = c->undo;
    return false;
}

static UiResult add_card(AccessCtx *c, const AppEvent *ev, bool admin)
{
    if (uid_is_random(ev)) {
        return RES_RANDOM_UID;
    }
    c->undo = *c->cards;
    AL_Status st = AL_Add(c->cards, ev->uid, ev->uid_len, admin);
    if (st != AL_OK) {
        return result_for(st);
    }
    if (!save_or_undo(c)) {
        return RES_STORAGE_ERROR;
    }
    IO_Log(LOGE_CARD_ADDED, ev->uid, ev->uid_len);
    return admin ? RES_ADMIN_ADDED : RES_CARD_ADDED;
}

static void delete_selected_card(AccessCtx *c, uint32_t now)
{
    AL_Card removed = c->cards->cards[c->list_pos];
    c->undo = *c->cards;

    AL_Status st = AL_RemoveAt(c->cards, c->list_pos);
    if (st == AL_OK && !save_or_undo(c)) {
        st = AL_ERR_STORAGE;
    }
    if (st != AL_OK) {
        show_result(c, false, result_for(st), AC_ADMIN_MENU, now);
        return;
    }
    IO_Log(LOGE_CARD_REMOVED, removed.uid, removed.uid_len);
    if (c->list_pos >= c->cards->count && c->list_pos > 0U) {
        c->list_pos--;
    }
    show_result(c, true, RES_CARD_REMOVED, AC_ADMIN_MENU, now);
}

static void save_unlock_time(AccessCtx *c, uint32_t now)
{
    c->undo = *c->cards;
    c->cards->unlock_time_s = c->edit_seconds;
    if (!save_or_undo(c)) {
        show_result(c, false, RES_STORAGE_ERROR, AC_ADMIN_MENU, now);
        return;
    }
    IO_Log(LOGE_SETTINGS_CHANGED, NULL, 0);
    show_result(c, true, RES_SAVED, AC_ADMIN_MENU, now);
}

static void grant(AccessCtx *c, const AppEvent *ev, uint32_t now)
{
    c->fail_count = 0;
    IO_Sound(BUZZ_OK);
    IO_Log(LOGE_ACCESS_GRANTED, ev->uid, ev->uid_len);
    go_to(c, AC_UNLOCKED, now);
}

static void deny(AccessCtx *c, const AppEvent *ev, uint32_t now)
{
    if (add_failure(c, now)) {
        c->fail_count = 0;
        IO_Sound(BUZZ_LOCKOUT);
        IO_Log(LOGE_LOCKOUT, ev->uid, ev->uid_len);
        go_to(c, AC_LOCKOUT, now);
        return;
    }
    IO_Sound(BUZZ_DENIED);
    IO_Log(LOGE_ACCESS_DENIED, ev->uid, ev->uid_len);
    if (c->state != AC_UNLOCKED) {   /* відчинені двері лишаються відчиненими до свого таймауту */
        go_to(c, AC_DENIED, now);
    }
}

static void open_menu(AccessCtx *c, const AppEvent *ev, uint32_t now)
{
    c->menu_pos = 0;
    IO_Sound(BUZZ_OK);
    IO_Log(LOGE_ADMIN_LOGIN, ev->uid, ev->uid_len);
    go_to(c, AC_ADMIN_MENU, now);
}

static void on_card_at_door(AccessCtx *c, const AppEvent *ev, uint32_t now)
{
    switch (AL_Classify(c->cards, ev->uid, ev->uid_len)) {
    case CARD_ADMIN: open_menu(c, ev, now); break;
    case CARD_USER:  grant(c, ev, now); break;
    default:         deny(c, ev, now); break;
    }
}

/* Режим налаштування: без адміна будь-хто ззовні міг би стати адміном, тому картку приймаємо
 * лише протягом 60 с після кнопки виходу — а вона всередині приміщення. */
static void on_card_in_setup(AccessCtx *c, const AppEvent *ev, uint32_t now)
{
    if (!AC_SetupArmed(c, now)) {
        IO_Sound(BUZZ_DENIED);
        IO_Log(LOGE_ACCESS_DENIED, ev->uid, ev->uid_len);
        return;
    }
    UiResult r = add_card(c, ev, true);
    bool ok = (r == RES_ADMIN_ADDED);
    if (ok) {
        c->setup_armed_until = 0;
    }
    show_result(c, ok, r, base_state(c), now);
}

static void on_menu(AccessCtx *c, const AppEvent *ev, uint32_t now)
{
    if (is_up_down(ev)) {
        c->menu_pos = MENU_Move(c->menu_pos, MENU_COUNT, ev->dir);
        IO_Sound(BUZZ_CLICK);
        go_to(c, c->state, now);
        return;
    }
    if (is_back(ev)) {
        go_to(c, base_state(c), now);
        return;
    }
    if (!is_select(ev)) {
        return;
    }

    IO_Sound(BUZZ_CLICK);
    switch ((MenuItem)c->menu_pos) {
    case MENU_OPEN_DOOR:
        IO_Sound(BUZZ_OK);
        IO_Log(LOGE_ACCESS_GRANTED, NULL, 0);
        go_to(c, AC_UNLOCKED, now);
        break;
    case MENU_ADD_CARD:
    case MENU_ADD_ADMIN:
        if (c->cards->count >= CFG_MAX_CARDS) {
            show_result(c, false, RES_LIST_FULL, AC_ADMIN_MENU, now);
        } else {
            c->adding_admin = (c->menu_pos == MENU_ADD_ADMIN);
            go_to(c, AC_ENROLL_WAIT_CARD, now);
        }
        break;
    case MENU_DELETE_CARD:
        c->list_pos = 0;
        go_to(c, AC_DELETE_SELECT, now);
        break;
    case MENU_EVENT_LOG:
        c->list_pos = 0;
        go_to(c, AC_LOG_VIEW, now);
        break;
    case MENU_UNLOCK_TIME:
        c->edit_seconds = c->cards->unlock_time_s;
        go_to(c, AC_SETTINGS, now);
        break;
    default:
        go_to(c, base_state(c), now);
        break;
    }
}

static void on_enroll(AccessCtx *c, const AppEvent *ev, uint32_t now)
{
    if (ev->type == EV_CARD) {
        UiResult r = add_card(c, ev, c->adding_admin);
        bool ok = (r == RES_CARD_ADDED || r == RES_ADMIN_ADDED);
        show_result(c, ok, r, AC_ADMIN_MENU, now);
    } else if (is_back(ev) || ev->type == EV_JOY_CLICK) {
        go_to(c, AC_ADMIN_MENU, now);
    }
}

static void on_delete_select(AccessCtx *c, const AppEvent *ev, uint32_t now)
{
    if (is_up_down(ev)) {
        c->list_pos = MENU_Move(c->list_pos, (uint8_t)c->cards->count, ev->dir);
        IO_Sound(BUZZ_CLICK);
        go_to(c, c->state, now);
    } else if (is_back(ev)) {
        go_to(c, AC_ADMIN_MENU, now);
    } else if (is_select(ev)) {
        delete_selected_card(c, now);
    }
}

static void on_log_view(AccessCtx *c, const AppEvent *ev, uint32_t now)
{
    if (is_up_down(ev)) {
        c->list_pos = MENU_Move(c->list_pos, (uint8_t)ELOG_Count(c->events), ev->dir);
        IO_Sound(BUZZ_CLICK);
        go_to(c, c->state, now);
    } else if (is_back(ev) || is_select(ev)) {
        go_to(c, AC_ADMIN_MENU, now);
    }
}

static void on_settings(AccessCtx *c, const AppEvent *ev, uint32_t now)
{
    if (ev->type == EV_JOY_DIR && ev->dir == JOY_UP && c->edit_seconds < CFG_UNLOCK_TIME_MAX_S) {
        c->edit_seconds++;
        IO_Sound(BUZZ_CLICK);
        go_to(c, c->state, now);
    } else if (ev->type == EV_JOY_DIR && ev->dir == JOY_DOWN && c->edit_seconds > CFG_UNLOCK_TIME_MIN_S) {
        c->edit_seconds--;
        IO_Sound(BUZZ_CLICK);
        go_to(c, c->state, now);
    } else if (is_back(ev)) {
        go_to(c, AC_ADMIN_MENU, now);
    } else if (is_select(ev)) {
        save_unlock_time(c, now);
    }
}

static void on_exit_button(AccessCtx *c, uint32_t now)
{
    if (AL_AdminCount(c->cards) == 0U) {
        c->setup_armed_until = now + CFG_SETUP_ARM_MS;
    }
    IO_Sound(BUZZ_OK);
    IO_Log(LOGE_EXIT_BUTTON, NULL, 0);
    go_to(c, AC_UNLOCKED, now);   /* безпека понад усе: кнопка виходу відчиняє завжди */
}

static void on_timeout(AccessCtx *c, uint32_t now)
{
    /* Подія таймера могла стати в чергу якраз перед зміною стану (і перезапуском таймера).
     * Така "запізніла" подія приходить раніше нового дедлайну — ігноруємо її. */
    if (c->timer_deadline == 0U || (int32_t)(now - c->timer_deadline) < -(int32_t)CFG_TIMER_SLACK_MS) {
        return;
    }
    switch (c->state) {
    case AC_ENROLL_WAIT_CARD:
        go_to(c, AC_ADMIN_MENU, now);
        break;
    case AC_RESULT:
        go_to(c, c->after_result, now);
        break;
    case AC_LOCKOUT:
        c->fail_count = 0;
        go_to(c, base_state(c), now);
        break;
    default:
        go_to(c, base_state(c), now);
        break;
    }
}

static void on_reader_fault(AccessCtx *c, uint32_t now)
{
    if (c->reader_fault) {
        return;
    }
    c->reader_fault = true;
    IO_Sound(BUZZ_ERROR);
    IO_Log(LOGE_RFID_FAULT, NULL, 0);
    if (c->state == AC_LOCKED || c->state == AC_SETUP || c->state == AC_DENIED) {
        go_to(c, AC_FAULT, now);
    }
}

static void on_reader_ok(AccessCtx *c, uint32_t now)
{
    if (!c->reader_fault) {
        return;
    }
    c->reader_fault = false;
    IO_Log(LOGE_RFID_RECOVERED, NULL, 0);
    if (c->state == AC_FAULT) {
        go_to(c, base_state(c), now);
    }
}

static bool in_admin_mode(const AccessCtx *c)
{
    return c->state >= AC_ADMIN_MENU;   /* ADMIN_MENU і всі наступні стани в enum */
}

static void on_net_unlock(AccessCtx *c, uint32_t now)
{
    if (in_admin_mode(c)) {
        return;
    }
    IO_Sound(BUZZ_OK);
    IO_Log(LOGE_NET_UNLOCK, NULL, 0);
    go_to(c, AC_UNLOCKED, now);
}

void AC_Init(AccessCtx *c, AccessList *cards, const EventLog *events)
{
    memset(c, 0, sizeof(*c));
    c->cards = cards;
    c->events = events;
    c->state = AC_LOCKED;
}

void AC_Start(AccessCtx *c, uint32_t now)
{
    c->lock_open = true;   /* щоб go_to() гарантовано подав команду "зачинити" */
    go_to(c, base_state(c), now);
}

void AC_HandleEvent(AccessCtx *c, const AppEvent *ev, uint32_t now)
{
    switch (ev->type) {
    case EV_EXIT_BUTTON: on_exit_button(c, now); return;
    case EV_TIMEOUT:     on_timeout(c, now); return;
    case EV_RFID_FAULT:  on_reader_fault(c, now); return;
    case EV_RFID_OK:     on_reader_ok(c, now); return;
    case EV_NET_UNLOCK:  on_net_unlock(c, now); return;
    default:             break;
    }

    switch (c->state) {
    case AC_LOCKED:
    case AC_DENIED:
    case AC_UNLOCKED:
    case AC_FAULT:
        if (ev->type == EV_CARD) {
            on_card_at_door(c, ev, now);
        }
        break;
    case AC_LOCKOUT:
        if (ev->type == EV_CARD) {   /* під час блокування не пускаємо нікого, але записуємо спробу */
            IO_Sound(BUZZ_DENIED);
            IO_Log(LOGE_ACCESS_DENIED, ev->uid, ev->uid_len);
        }
        break;
    case AC_SETUP:
        if (ev->type == EV_CARD) {
            on_card_in_setup(c, ev, now);
        }
        break;
    case AC_ADMIN_MENU:       on_menu(c, ev, now); break;
    case AC_ENROLL_WAIT_CARD: on_enroll(c, ev, now); break;
    case AC_DELETE_SELECT:    on_delete_select(c, ev, now); break;
    case AC_LOG_VIEW:         on_log_view(c, ev, now); break;
    case AC_SETTINGS:         on_settings(c, ev, now); break;
    case AC_RESULT:
        if (ev->type == EV_JOY_CLICK) {
            go_to(c, c->after_result, now);
        }
        break;
    default:
        break;
    }
}

UiScreen AC_Screen(const AccessCtx *c)
{
    switch (c->state) {
    case AC_LOCKED:           return SCR_LOCKED;
    case AC_UNLOCKED:         return SCR_GRANTED;
    case AC_DENIED:           return SCR_DENIED;
    case AC_LOCKOUT:          return SCR_LOCKOUT;
    case AC_SETUP:            return SCR_SETUP;
    case AC_FAULT:            return SCR_FAULT;
    case AC_ADMIN_MENU:       return SCR_MENU;
    case AC_ENROLL_WAIT_CARD: return SCR_ENROLL;
    case AC_DELETE_SELECT:    return SCR_DELETE;
    case AC_LOG_VIEW:         return SCR_LOG;
    case AC_SETTINGS:         return SCR_SETTINGS;
    case AC_RESULT:           return c->result_ok ? SCR_RESULT_OK : SCR_RESULT_FAIL;
    default:                  return SCR_LOCKED;
    }
}

uint32_t AC_SecondsLeft(const AccessCtx *c, uint32_t now)
{
    if (c->timer_deadline == 0U) {
        return 0;
    }
    int32_t left_ms = (int32_t)(c->timer_deadline - now);
    return (left_ms <= 0) ? 0U : ((uint32_t)left_ms + 999U) / 1000U;
}

bool AC_SetupArmed(const AccessCtx *c, uint32_t now)
{
    return c->setup_armed_until != 0U && (int32_t)(now - c->setup_armed_until) < 0;
}

const char *AC_StateName(AccessState s)
{
    static const char *const names[AC_STATE_COUNT] = {
        "LOCKED", "UNLOCKED", "DENIED", "LOCKOUT", "SETUP", "FAULT",
        "ADMIN_MENU", "ENROLL", "DELETE", "LOG_VIEW", "SETTINGS", "RESULT",
    };
    return (s < AC_STATE_COUNT) ? names[s] : "?";
}
