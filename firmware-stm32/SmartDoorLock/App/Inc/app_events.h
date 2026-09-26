#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include <stdbool.h>
#include <stdint.h>
#include "joystick_math.h"

/* The language the tasks speak: everything that reaches tCtrl is an AppEvent. HAL-free. */

#define UID_MAX_LEN  7U

typedef enum {
    CARD_UNKNOWN = 0,
    CARD_USER,
    CARD_ADMIN
} CardClass;

typedef enum {
    EV_CARD,              /* tRfid: card presented (tCtrl fills card.cls before the state machine) */
    EV_RFID_FAULT,        /* tRfid: RC522 stopped responding */
    EV_RFID_OK,           /* tRfid: RC522 back after a fault */
    EV_JOY_DIR,           /* tInput: joystick direction (edge or auto-repeat) */
    EV_JOY_CLICK,         /* tInput: joystick button */
    EV_EXIT_BUTTON,       /* tInput: exit button inside */
    EV_TIMEOUT,           /* state timer expired */
    EV_NET_UNLOCK,        /* tNet: unlock command from the phone */
    EV_NET_STATUS_REQ,    /* tNet: status request */
    EV_CMD_RESULT,        /* tCtrl -> state machine: outcome of a storage command */
    EV_CONSOLE            /* tLog: debug console request that needs data owned by tCtrl */
} AppEventType;

typedef enum {
    CON_CMD_CARDS = 0,
    CON_CMD_LOG,
    CON_CMD_TIME_SET,
    CON_CMD_STATUS
} ConsoleCmd;

typedef struct {
    AppEventType type;
    union {
        struct {
            uint8_t   uid[UID_MAX_LEN];
            uint8_t   len;
            CardClass cls;
        } card;
        JOY_Direction dir;
        struct {
            bool    ok;
            uint8_t reason;   /* UiResult */
            uint8_t uid[UID_MAX_LEN];
            uint8_t uid_len;
        } result;
        struct {
            uint8_t  cmd;     /* ConsoleCmd */
            uint32_t arg;
        } console;
    };
} AppEvent;

#endif
