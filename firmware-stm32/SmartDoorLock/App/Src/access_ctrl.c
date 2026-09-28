#include <string.h>
#include "access_ctrl.h"
#include "menu.h"

/* ------------------------------------------------------------------ helpers */

static AccessActions no_actions(void)
{
    AccessActions a;
    memset(&a, 0, sizeof(a));
    a.lock = LOCK_ACT_NONE;
    a.sound = BUZZ_NONE;
    a.timer_ms = AC_TIMER_KEEP;
    a.cmd = AC_CMD_NONE;
    return a;
}

static AccessState base_state(const AccessCtx *c)
{
    if (c->rfid_fault) {
        return AC_FAULT;
    }
    return (c->admin_count == 0U) ? AC_SETUP : AC_LOCKED;
}

static bool is_idle_state(AccessState s)
{
    return s == AC_LOCKED || s == AC_SETUP || s == AC_DENIED || s == AC_FAULT;
}

static bool is_admin_state(AccessState s)
{
    return s == AC_ADMIN_MENU || s == AC_ENROLL_WAIT_CARD || s == AC_DELETE_SELECT ||
           s == AC_LOG_VIEW || s == AC_SETTINGS || s == AC_RESULT;
}

static void arm(AccessCtx *c, AccessActions *a, uint32_t ms, uint32_t now)
{
    c->timer_active = true;
    c->deadline_ms = now + ms;
    a->timer_ms = (int32_t)ms;
}

static void disarm(AccessCtx *c, AccessActions *a)
{
    c->timer_active = false;
    a->timer_ms = 0;
}

static void enter(AccessCtx *c, AccessActions *a, AccessState s, uint32_t now)
{
    c->state = s;
    a->redraw = true;

    if (s == AC_UNLOCKED) {
        if (!c->lock_open) {
            a->lock = LOCK_ACT_OPEN;
            c->lock_open = true;
        }
    } else if (c->lock_open) {
        a->lock = LOCK_ACT_CLOSE;
        c->lock_open = false;
    }

    switch (s) {
    case AC_UNLOCKED:         arm(c, a, (uint32_t)c->unlock_time_s * 1000UL, now); break;
    case AC_DENIED:           arm(c, a, CFG_DENIED_SHOW_MS, now); break;
    case AC_LOCKOUT:          arm(c, a, CFG_LOCKOUT_TIME_MS, now); break;
    case AC_ENROLL_WAIT_CARD: arm(c, a, CFG_ENROLL_WAIT_MS, now); break;
    case AC_RESULT:           arm(c, a, CFG_RESULT_SHOW_MS, now); break;
    case AC_ADMIN_MENU:
    case AC_DELETE_SELECT:
    case AC_LOG_VIEW:
    case AC_SETTINGS:         arm(c, a, CFG_MENU_IDLE_MS, now); break;
    default:                  disarm(c, a); break;
    }
}

static void enter_base(AccessCtx *c, AccessActions *a, uint32_t now)
{
    enter(c, a, base_state(c), now);
}

static void set_log(AccessActions *a, LogEventType t, const uint8_t *uid, uint8_t len)
{
    a->log = true;
    a->log_type = t;
    if (uid != NULL && len > 0U) {
        a->uid_len = (len > UID_MAX_LEN) ? (uint8_t)UID_MAX_LEN : len;
        memcpy(a->uid, uid, a->uid_len);
    }
}

static void clear_failures(AccessCtx *c)
{
    c->fail_count = 0;
    c->fail_head = 0;
}

/* Returns true when this failure completes CFG_LOCKOUT_ATTEMPTS inside the window. */
static bool register_failure(AccessCtx *c, uint32_t now)
{
    c->fail_times[c->fail_head] = now;
    c->fail_head = (uint8_t)((c->fail_head + 1U) % CFG_LOCKOUT_ATTEMPTS);
    if (c->fail_count < CFG_LOCKOUT_ATTEMPTS) {
        c->fail_count++;
    }
    uint8_t recent = 0;
    for (uint8_t i = 0; i < c->fail_count; i++) {
        if ((uint32_t)(now - c->fail_times[i]) < CFG_LOCKOUT_WINDOW_MS) {
            recent++;
        }
    }
    return recent >= CFG_LOCKOUT_ATTEMPTS;
}

static void show_result(AccessCtx *c, AccessActions *a, bool ok, UiResult r, AccessState next, uint32_t now)
{
    c->result = r;
    c->result_next = next;
    a->sound = ok ? BUZZ_OK : BUZZ_ERROR;
    enter(c, a, AC_RESULT, now);
}

/* ------------------------------------------------------------------ card handling */

/* ISO/IEC 14443-3: a single-size UID starting with 0x08 is a random ID, regenerated on every
 * activation (phones, many bank cards). Such a card could never be recognised again. */
static bool uid_is_random(const AppEvent *ev)
{
    return ev->card.len == 4U && ev->card.uid[0] == 0x08U;
}

static void grant(AccessCtx *c, AccessActions *a, const AppEvent *ev, uint32_t now)
{
    a->sound = BUZZ_OK;
    a->notify_net = true;
    set_log(a, LOGE_ACCESS_GRANTED, ev->card.uid, ev->card.len);
    clear_failures(c);
    enter(c, a, AC_UNLOCKED, now);
}

static void deny(AccessCtx *c, AccessActions *a, const AppEvent *ev, uint32_t now, bool stay)
{
    a->notify_net = true;
    if (register_failure(c, now)) {
        clear_failures(c);
        a->sound = BUZZ_LOCKOUT;
        set_log(a, LOGE_LOCKOUT, ev->card.uid, ev->card.len);
        enter(c, a, AC_LOCKOUT, now);
        return;
    }
    a->sound = BUZZ_DENIED;
    set_log(a, LOGE_ACCESS_DENIED, ev->card.uid, ev->card.len);
    if (!stay) {
        enter(c, a, AC_DENIED, now);
    }
}

static void on_card(AccessCtx *c, AccessActions *a, const AppEvent *ev, uint32_t now)
{
    switch (c->state) {
    case AC_LOCKED:
    case AC_DENIED:
    case AC_FAULT:
    case AC_UNLOCKED:
        if (ev->card.cls == CARD_ADMIN) {
            c->menu_sel = 0;
            a->sound = BUZZ_OK;
            set_log(a, LOGE_ADMIN_LOGIN, ev->card.uid, ev->card.len);
            enter(c, a, AC_ADMIN_MENU, now);
        } else if (ev->card.cls == CARD_USER) {
            grant(c, a, ev, now);
        } else {
            deny(c, a, ev, now, c->state == AC_UNLOCKED);
        }
        break;

    case AC_LOCKOUT:
        /* Reader is blocked: record the attempt, do not count it, do not extend the block. */
        a->sound = BUZZ_DENIED;
        set_log(a, LOGE_ACCESS_DENIED, ev->card.uid, ev->card.len);
        break;

    case AC_SETUP:
        /* Without an admin anyone outside could claim the device: require the exit button,
         * which is inside the protected room, shortly before the admin card. */
        if (!AC_SetupArmed(c, now)) {
            c->setup_armed = false;
            a->sound = BUZZ_DENIED;
            set_log(a, LOGE_ACCESS_DENIED, ev->card.uid, ev->card.len);
            break;
        }
        if (uid_is_random(ev)) {
            /* keep the setup armed: the next, suitable card can still be presented */
            show_result(c, a, false, RES_RANDOM_UID, AC_LOCKED, now);
            break;
        }
        c->setup_armed = false;
        c->pending_cmd = AC_CMD_ENROLL_ADMIN;
        a->cmd = AC_CMD_ENROLL_ADMIN;
        memcpy(a->uid, ev->card.uid, ev->card.len);
        a->uid_len = ev->card.len;
        break;

    case AC_ENROLL_WAIT_CARD:
        if (ev->card.cls != CARD_UNKNOWN) {
            show_result(c, a, false, RES_ALREADY_EXISTS, AC_ADMIN_MENU, now);
        } else if (uid_is_random(ev)) {
            show_result(c, a, false, RES_RANDOM_UID, AC_ADMIN_MENU, now);
        } else {
            c->pending_cmd = c->enroll_admin ? AC_CMD_ENROLL_ADMIN : AC_CMD_ENROLL_USER;
            a->cmd = c->pending_cmd;
            memcpy(a->uid, ev->card.uid, ev->card.len);
            a->uid_len = ev->card.len;
        }
        break;

    default:
        break;   /* other admin screens ignore cards */
    }
}

/* ------------------------------------------------------------------ command results */

static void on_cmd_result(AccessCtx *c, AccessActions *a, const AppEvent *ev, uint32_t now)
{
    AcCommand cmd = c->pending_cmd;
    c->pending_cmd = AC_CMD_NONE;
    bool ok = ev->result.ok;
    UiResult fail_reason = (UiResult)ev->result.reason;

    switch (cmd) {
    case AC_CMD_ENROLL_ADMIN:
        /* From setup: continue to the base state computed at that moment (AC_LOCKED marker);
         * from the admin menu: back to the menu. */
        show_result(c, a, ok, ok ? RES_ADMIN_ADDED : fail_reason,
                    (c->state == AC_SETUP) ? AC_LOCKED : AC_ADMIN_MENU, now);
        break;
    case AC_CMD_ENROLL_USER:
        show_result(c, a, ok, ok ? RES_CARD_ADDED : fail_reason, AC_ADMIN_MENU, now);
        break;
    case AC_CMD_DELETE:
        if (ok && c->list_sel >= c->card_count && c->list_sel > 0U) {
            c->list_sel = (uint8_t)(c->list_sel - 1U);
        }
        show_result(c, a, ok, ok ? RES_CARD_REMOVED : fail_reason, AC_ADMIN_MENU, now);
        break;
    case AC_CMD_SAVE_SETTINGS:
        if (ok) {
            c->unlock_time_s = c->edit_unlock_s;
            set_log(a, LOGE_SETTINGS_CHANGED, NULL, 0);
        }
        show_result(c, a, ok, ok ? RES_SAVED : fail_reason, AC_ADMIN_MENU, now);
        break;
    default:
        return;
    }
    if (ok && (cmd == AC_CMD_ENROLL_ADMIN || cmd == AC_CMD_ENROLL_USER || cmd == AC_CMD_DELETE)) {
        set_log(a, (cmd == AC_CMD_DELETE) ? LOGE_CARD_REMOVED : LOGE_CARD_ADDED,
                ev->result.uid, ev->result.uid_len);
    }
}

/* ------------------------------------------------------------------ admin menu */

static void activate_menu_item(AccessCtx *c, AccessActions *a, uint32_t now)
{
    switch ((MenuItem)c->menu_sel) {
    case MENU_OPEN_DOOR:
        a->sound = BUZZ_OK;
        set_log(a, LOGE_ACCESS_GRANTED, NULL, 0);
        enter(c, a, AC_UNLOCKED, now);
        break;
    case MENU_ADD_CARD:
    case MENU_ADD_ADMIN:
        if (c->card_count >= CFG_MAX_CARDS) {
            show_result(c, a, false, RES_LIST_FULL, AC_ADMIN_MENU, now);
        } else {
            c->enroll_admin = ((MenuItem)c->menu_sel == MENU_ADD_ADMIN);
            enter(c, a, AC_ENROLL_WAIT_CARD, now);
        }
        break;
    case MENU_DELETE_CARD:
        if (c->card_count == 0U) {
            show_result(c, a, false, RES_LIST_EMPTY, AC_ADMIN_MENU, now);
        } else {
            c->list_sel = 0;
            enter(c, a, AC_DELETE_SELECT, now);
        }
        break;
    case MENU_EVENT_LOG:
        c->list_sel = 0;
        enter(c, a, AC_LOG_VIEW, now);
        break;
    case MENU_UNLOCK_TIME:
        c->edit_unlock_s = c->unlock_time_s;
        enter(c, a, AC_SETTINGS, now);
        break;
    case MENU_EXIT:
    default:
        enter_base(c, a, now);
        break;
    }
}

static bool is_back(const AppEvent *ev)
{
    return ev->type == EV_JOY_DIR && ev->dir == JOY_LEFT;
}

static bool is_confirm(const AppEvent *ev)
{
    return ev->type == EV_JOY_CLICK || (ev->type == EV_JOY_DIR && ev->dir == JOY_RIGHT);
}

static bool is_vertical(const AppEvent *ev)
{
    return ev->type == EV_JOY_DIR && (ev->dir == JOY_UP || ev->dir == JOY_DOWN);
}

static void on_joystick(AccessCtx *c, AccessActions *a, const AppEvent *ev, uint32_t now)
{
    switch (c->state) {
    case AC_ADMIN_MENU:
        if (is_vertical(ev)) {
            c->menu_sel = MENU_Move(c->menu_sel, MENU_COUNT, ev->dir);
            a->sound = BUZZ_CLICK;
            a->redraw = true;
            arm(c, a, CFG_MENU_IDLE_MS, now);
        } else if (is_back(ev)) {
            enter_base(c, a, now);
        } else if (is_confirm(ev)) {
            a->sound = BUZZ_CLICK;
            activate_menu_item(c, a, now);
        }
        break;

    case AC_ENROLL_WAIT_CARD:
        if (is_back(ev) || ev->type == EV_JOY_CLICK) {
            enter(c, a, AC_ADMIN_MENU, now);
        }
        break;

    case AC_DELETE_SELECT:
        if (is_vertical(ev)) {
            c->list_sel = MENU_Move(c->list_sel, (uint8_t)c->card_count, ev->dir);
            a->sound = BUZZ_CLICK;
            a->redraw = true;
            arm(c, a, CFG_MENU_IDLE_MS, now);
        } else if (is_back(ev)) {
            enter(c, a, AC_ADMIN_MENU, now);
        } else if (is_confirm(ev)) {
            c->pending_cmd = AC_CMD_DELETE;
            a->cmd = AC_CMD_DELETE;
            a->cmd_arg = c->list_sel;
        }
        break;

    case AC_LOG_VIEW:
        if (is_vertical(ev)) {
            c->list_sel = MENU_Move(c->list_sel, (uint8_t)c->log_count, ev->dir);
            a->sound = BUZZ_CLICK;
            a->redraw = true;
            arm(c, a, CFG_MENU_IDLE_MS, now);
        } else if (is_back(ev) || is_confirm(ev)) {
            enter(c, a, AC_ADMIN_MENU, now);
        }
        break;

    case AC_SETTINGS:
        if (ev->type == EV_JOY_DIR && ev->dir == JOY_UP && c->edit_unlock_s < CFG_UNLOCK_TIME_MAX_S) {
            c->edit_unlock_s++;
            a->sound = BUZZ_CLICK;
            a->redraw = true;
            arm(c, a, CFG_MENU_IDLE_MS, now);
        } else if (ev->type == EV_JOY_DIR && ev->dir == JOY_DOWN && c->edit_unlock_s > CFG_UNLOCK_TIME_MIN_S) {
            c->edit_unlock_s--;
            a->sound = BUZZ_CLICK;
            a->redraw = true;
            arm(c, a, CFG_MENU_IDLE_MS, now);
        } else if (is_back(ev)) {
            enter(c, a, AC_ADMIN_MENU, now);
        } else if (is_confirm(ev)) {
            c->pending_cmd = AC_CMD_SAVE_SETTINGS;
            a->cmd = AC_CMD_SAVE_SETTINGS;
            a->cmd_arg = c->edit_unlock_s;
        }
        break;

    case AC_RESULT:
        if (ev->type == EV_JOY_CLICK) {
            AccessState next = (c->result_next == AC_LOCKED) ? base_state(c) : c->result_next;
            enter(c, a, next, now);
        }
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ timeouts */

static void on_timeout(AccessCtx *c, AccessActions *a, uint32_t now)
{
    /* A timer event can be queued just before the state changed and re-armed the timer:
     * such an event arrives before the new deadline and is ignored. */
    if (!c->timer_active || (int32_t)(now - c->deadline_ms) < -(int32_t)CFG_TIMER_SLACK_MS) {
        return;
    }
    c->timer_active = false;

    switch (c->state) {
    case AC_LOCKOUT:
        clear_failures(c);
        enter_base(c, a, now);
        break;
    case AC_ENROLL_WAIT_CARD:
        enter(c, a, AC_ADMIN_MENU, now);
        break;
    case AC_RESULT:
        enter(c, a, (c->result_next == AC_LOCKED) ? base_state(c) : c->result_next, now);
        break;
    case AC_UNLOCKED:
    case AC_DENIED:
    case AC_ADMIN_MENU:
    case AC_DELETE_SELECT:
    case AC_LOG_VIEW:
    case AC_SETTINGS:
        enter_base(c, a, now);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ public API */

void AC_Init(AccessCtx *ctx, uint16_t unlock_time_s)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = AC_LOCKED;
    ctx->unlock_time_s = unlock_time_s;
    ctx->edit_unlock_s = unlock_time_s;
}

void AC_SetCounts(AccessCtx *ctx, uint16_t cards, uint16_t admins, uint16_t logs)
{
    ctx->card_count = cards;
    ctx->admin_count = admins;
    ctx->log_count = logs;
    if (ctx->list_sel >= cards && ctx->state == AC_DELETE_SELECT) {
        ctx->list_sel = (cards > 0U) ? (uint8_t)(cards - 1U) : 0U;
    }
}

AccessActions AC_Start(AccessCtx *ctx, uint32_t now_ms)
{
    AccessActions a = no_actions();
    ctx->lock_open = true;   /* forces an explicit CLOSE on start-up */
    enter_base(ctx, &a, now_ms);
    return a;
}

AccessActions AC_HandleEvent(AccessCtx *ctx, const AppEvent *ev, uint32_t now_ms)
{
    AccessActions a = no_actions();

    switch (ev->type) {
    case EV_CARD:
        on_card(ctx, &a, ev, now_ms);
        break;

    case EV_JOY_DIR:
    case EV_JOY_CLICK:
        on_joystick(ctx, &a, ev, now_ms);
        break;

    case EV_EXIT_BUTTON:
        /* Safety first: the exit button always releases the door, whatever the state. */
        ctx->pending_cmd = AC_CMD_NONE;
        if (ctx->admin_count == 0U) {
            ctx->setup_armed = true;
            ctx->setup_armed_until = now_ms + CFG_SETUP_ARM_MS;
        }
        a.sound = BUZZ_OK;
        set_log(&a, LOGE_EXIT_BUTTON, NULL, 0);
        enter(ctx, &a, AC_UNLOCKED, now_ms);
        break;

    case EV_TIMEOUT:
        on_timeout(ctx, &a, now_ms);
        break;

    case EV_CMD_RESULT:
        on_cmd_result(ctx, &a, ev, now_ms);
        break;

    case EV_RFID_FAULT:
        if (!ctx->rfid_fault) {
            ctx->rfid_fault = true;
            a.sound = BUZZ_ERROR;
            a.notify_net = true;
            set_log(&a, LOGE_RFID_FAULT, NULL, 0);
            if (is_idle_state(ctx->state)) {
                enter_base(ctx, &a, now_ms);
            }
        }
        break;

    case EV_RFID_OK:
        if (ctx->rfid_fault) {
            ctx->rfid_fault = false;
            a.notify_net = true;
            set_log(&a, LOGE_RFID_RECOVERED, NULL, 0);
            if (ctx->state == AC_FAULT) {
                enter_base(ctx, &a, now_ms);
            }
        }
        break;

    case EV_NET_UNLOCK:
        if (!is_admin_state(ctx->state)) {
            a.sound = BUZZ_OK;
            a.notify_net = true;
            set_log(&a, LOGE_NET_UNLOCK, NULL, 0);
            enter(ctx, &a, AC_UNLOCKED, now_ms);
        }
        break;

    case EV_NET_STATUS_REQ:
        a.notify_net = true;
        break;

    default:
        break;
    }
    return a;
}

AccessState AC_GetState(const AccessCtx *ctx)
{
    return ctx->state;
}

UiScreen AC_Screen(const AccessCtx *ctx)
{
    switch (ctx->state) {
    case AC_LOCKED:           return SCR_LOCKED;
    case AC_UNLOCKED:         return SCR_GRANTED;
    case AC_DENIED:           return SCR_DENIED;
    case AC_LOCKOUT:          return SCR_LOCKOUT;
    case AC_SETUP:            return SCR_SETUP;
    case AC_ADMIN_MENU:       return SCR_MENU;
    case AC_ENROLL_WAIT_CARD: return SCR_ENROLL;
    case AC_DELETE_SELECT:    return SCR_DELETE;
    case AC_LOG_VIEW:         return SCR_LOG;
    case AC_SETTINGS:         return SCR_SETTINGS;
    case AC_FAULT:            return SCR_FAULT;
    case AC_RESULT:
        switch (ctx->result) {
        case RES_CARD_ADDED:
        case RES_ADMIN_ADDED:
        case RES_CARD_REMOVED:
        case RES_SAVED:
            return SCR_RESULT_OK;
        default:
            return SCR_RESULT_FAIL;
        }
    default:
        return SCR_LOCKED;
    }
}

uint32_t AC_RemainingS(const AccessCtx *ctx, uint32_t now_ms)
{
    if (!ctx->timer_active) {
        return 0;
    }
    int32_t left = (int32_t)(ctx->deadline_ms - now_ms);
    return (left <= 0) ? 0U : ((uint32_t)left + 999U) / 1000U;
}

bool AC_SetupArmed(const AccessCtx *ctx, uint32_t now_ms)
{
    return ctx->setup_armed && (int32_t)(now_ms - ctx->setup_armed_until) < 0;
}

const char *AC_StateName(AccessState st)
{
    static const char *const names[AC_STATE_COUNT] = {
        [AC_LOCKED] = "LOCKED",           [AC_UNLOCKED] = "UNLOCKED",
        [AC_DENIED] = "DENIED",           [AC_LOCKOUT] = "LOCKOUT",
        [AC_SETUP] = "SETUP",             [AC_ADMIN_MENU] = "ADMIN_MENU",
        [AC_ENROLL_WAIT_CARD] = "ENROLL", [AC_DELETE_SELECT] = "DELETE",
        [AC_LOG_VIEW] = "LOG_VIEW",       [AC_SETTINGS] = "SETTINGS",
        [AC_RESULT] = "RESULT",           [AC_FAULT] = "FAULT",
    };
    return (st < AC_STATE_COUNT) ? names[st] : "?";
}
