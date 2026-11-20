#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include <stdint.h>
#include "joystick_math.h"

#define UID_MAX_LEN  7U

typedef enum {
    EV_CARD,
    EV_RFID_FAULT,
    EV_RFID_OK,
    EV_JOY_DIR,
    EV_JOY_CLICK,
    EV_JOY_LONG,
    EV_TIMEOUT,
    EV_NET_UNLOCK,
    EV_CONSOLE
} AppEventType;

typedef enum {
    CON_CARDS = 0,
    CON_LOG,
    CON_TIME,
    CON_STATUS
} ConsoleCmd;

typedef struct {
    AppEventType  type;
    uint8_t       uid[UID_MAX_LEN];
    uint8_t       uid_len;
    JOY_Direction dir;
    uint8_t       console_cmd;
    uint32_t      console_arg;
} AppEvent;

#endif
