#ifndef ACCESS_CTRL_H
#define ACCESS_CTRL_H

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "app_events.h"
#include "buzzer_pattern.h"
#include "event_log.h"
#include "ui.h"

/* Access state machine as a pure function: event + state -> new state + list of actions.
 * No HAL, no RTOS, time comes in as now_ms: tCtrl executes the actions, the PC tests just
 * inspect them (Tests/test_access_ctrl.c). */

typedef enum {
    AC_LOCKED = 0,
    AC_UNLOCKED,
    AC_DENIED,
    AC_LOCKOUT,
    AC_SETUP,               /* no admin card yet: after the exit button (inside) was pressed,
                               the next card presented becomes the admin */
    AC_ADMIN_MENU,
    AC_ENROLL_WAIT_CARD,
    AC_DELETE_SELECT,
    AC_LOG_VIEW,
    AC_SETTINGS,
    AC_RESULT,              /* short "done / failed" screen after a command */
    AC_FAULT,               /* card reader is down: exit button still works */
    AC_STATE_COUNT
} AccessState;

typedef enum {
    AC_CMD_NONE = 0,
    AC_CMD_ENROLL_USER,     /* add actions.uid as a user card */
    AC_CMD_ENROLL_ADMIN,    /* add actions.uid as an admin card */
    AC_CMD_DELETE,          /* remove the card at index cmd_arg */
    AC_CMD_SAVE_SETTINGS    /* persist unlock time = cmd_arg seconds */
} AcCommand;

typedef enum {
    LOCK_ACT_NONE = 0,
    LOCK_ACT_OPEN,
    LOCK_ACT_CLOSE
} LockAction;

#define AC_TIMER_KEEP  (-1)

typedef struct {
    LockAction   lock;
    BUZZ_Pattern sound;
    int32_t      timer_ms;       /* AC_TIMER_KEEP, 0 = stop, > 0 = (re)start one-shot timer */
    bool         redraw;         /* screen or its content changed */
    bool         log;
    LogEventType log_type;
    bool         notify_net;
    AcCommand    cmd;
    uint16_t     cmd_arg;
    uint8_t      uid[UID_MAX_LEN];
    uint8_t      uid_len;
} AccessActions;

typedef struct {
    AccessState state;
    AccessState result_next;     /* where AC_RESULT goes when it times out */
    UiResult    result;
    AcCommand   pending_cmd;
    uint8_t     menu_sel;
    uint8_t     list_sel;
    uint16_t    unlock_time_s;
    uint16_t    edit_unlock_s;   /* value being edited in AC_SETTINGS */
    uint16_t    card_count;
    uint16_t    admin_count;
    uint16_t    log_count;
    bool        lock_open;
    bool        rfid_fault;
    bool        timer_active;
    uint32_t    deadline_ms;
    uint32_t    fail_times[CFG_LOCKOUT_ATTEMPTS];
    uint8_t     fail_head;
    uint8_t     fail_count;
    bool        setup_armed;
    uint32_t    setup_armed_until;
} AccessCtx;

void          AC_Init(AccessCtx *ctx, uint16_t unlock_time_s);
/* Tells the machine how many cards/admins/log entries exist; call after every change. */
void          AC_SetCounts(AccessCtx *ctx, uint16_t cards, uint16_t admins, uint16_t logs);
/* Initial state (SETUP when there is no admin, LOCKED otherwise) and its actions. */
AccessActions AC_Start(AccessCtx *ctx, uint32_t now_ms);
AccessActions AC_HandleEvent(AccessCtx *ctx, const AppEvent *ev, uint32_t now_ms);

AccessState   AC_GetState(const AccessCtx *ctx);
UiScreen      AC_Screen(const AccessCtx *ctx);
/* Seconds left on the running state timer, 0 when none (e.g. lockout countdown). */
uint32_t      AC_RemainingS(const AccessCtx *ctx, uint32_t now_ms);
/* True while setup mode accepts an admin card (the exit button was pressed recently). */
bool          AC_SetupArmed(const AccessCtx *ctx, uint32_t now_ms);
const char   *AC_StateName(AccessState st);

#endif
