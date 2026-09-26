#ifndef MENU_H
#define MENU_H

#include <stdint.h>
#include "joystick_math.h"

/* Admin menu model, HAL-free. */

typedef enum {
    MENU_OPEN_DOOR = 0,
    MENU_ADD_CARD,
    MENU_DELETE_CARD,
    MENU_EVENT_LOG,
    MENU_UNLOCK_TIME,
    MENU_EXIT,
    MENU_COUNT
} MenuItem;

const char *MENU_Label(MenuItem item);

/* Moves a list cursor with UP/DOWN, wrapping around; other directions leave it unchanged. */
uint8_t MENU_Move(uint8_t sel, uint8_t count, JOY_Direction dir);

#endif
