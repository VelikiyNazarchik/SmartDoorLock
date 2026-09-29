#ifndef JOYSTICK_MATH_H
#define JOYSTICK_MATH_H

#include <stdint.h>

typedef enum {
    JOY_NONE = 0,
    JOY_UP,
    JOY_DOWN,
    JOY_LEFT,
    JOY_RIGHT
} JOY_Direction;

/* Maps a raw ADC sample to -100..100 around a calibrated center; values inside the dead zone become 0
 * and the rest is rescaled so the output still reaches +-100 at the end stops. */
int8_t JOY_Normalize(uint16_t raw, uint16_t center, uint16_t raw_max, uint8_t dead_zone_pct);

/* Picks a direction from normalized axes with hysteresis: a new direction needs |axis| >= press_pct,
 * the current one is held until its axis drops below release_pct. x > 0 is RIGHT, y > 0 is UP. */
JOY_Direction JOY_Classify(int8_t x, int8_t y, JOY_Direction prev, uint8_t press_pct, uint8_t release_pct);

#endif
