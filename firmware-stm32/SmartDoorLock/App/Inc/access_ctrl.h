#ifndef ACCESS_CTRL_H
#define ACCESS_CTRL_H

#include <stdbool.h>
#include <stdint.h>
#include "access_list.h"
#include "app_config.h"
#include "app_events.h"
#include "event_log.h"
#include "ui.h"

typedef enum {
    AC_LOCKED = 0,
    AC_UNLOCKED,
    AC_DENIED,
    AC_LOCKOUT,
    AC_SETUP,
    AC_FAULT,
    AC_ADMIN_MENU,
    AC_ENROLL_WAIT_CARD,
    AC_DELETE_SELECT,
    AC_LOG_VIEW,
    AC_SETTINGS,
    AC_RESULT,
    AC_RECOVERY_CARD,
    AC_RECOVERY_CODE,
    AC_STATE_COUNT
} AccessState;

typedef struct {
    AccessState     state;
    AccessList     *cards;
    const EventLog *events;
    AccessList      undo;

    bool     lock_open;
    bool     reader_fault;
    uint32_t timer_deadline;

    uint32_t fail_times[CFG_LOCKOUT_ATTEMPTS];
    uint8_t  fail_count;

    uint8_t  menu_pos;
    uint8_t  list_pos;
    uint16_t edit_seconds;
    bool     adding_admin;

    uint8_t  recovery_uid[UID_MAX_LEN];
    uint8_t  recovery_uid_len;
    uint8_t  code_pos;
    bool     code_wrong;

    UiResult    result;
    bool        result_ok;
    AccessState after_result;
} AccessCtx;

void        AC_Init(AccessCtx *c, AccessList *cards, const EventLog *events);
void        AC_Start(AccessCtx *c, uint32_t now);
void        AC_HandleEvent(AccessCtx *c, const AppEvent *ev, uint32_t now);

UiScreen    AC_Screen(const AccessCtx *c);
uint32_t    AC_SecondsLeft(const AccessCtx *c, uint32_t now);
const char *AC_StateName(AccessState s);

#endif
