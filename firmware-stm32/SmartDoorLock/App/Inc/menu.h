#ifndef MENU_H
#define MENU_H

#include <stdint.h>
#include "joystick_math.h"

typedef enum {
    MENU_OPEN_DOOR = 0,
    MENU_ADD_CARD,
    MENU_ADD_ADMIN,
    MENU_DELETE_CARD,
    MENU_EVENT_LOG,
    MENU_UNLOCK_TIME,
    MENU_EXIT,
    MENU_COUNT
} MenuItem;

const char *MENU_Label(MenuItem item);

uint8_t MENU_Move(uint8_t sel, uint8_t count, JOY_Direction dir);

#endif
