#include "joystick_math.h"

static int abs8(int v)
{
    return (v < 0) ? -v : v;
}

int8_t JOY_Normalize(uint16_t raw, uint16_t center, uint16_t raw_max, uint8_t dead_zone_pct)
{
    int32_t v;
    if (dead_zone_pct >= 100U || center == 0U || center >= raw_max) {
        return 0;
    }
    if (raw >= center) {
        v = ((int32_t)(raw - center) * 100) / (int32_t)(raw_max - center);
    } else {
        v = -(((int32_t)(center - raw) * 100) / (int32_t)center);
    }
    if (v > 100) {
        v = 100;
    } else if (v < -100) {
        v = -100;
    }

    int32_t mag = (v < 0) ? -v : v;
    if (mag < (int32_t)dead_zone_pct) {
        return 0;
    }
    mag = ((mag - (int32_t)dead_zone_pct) * 100) / (100 - (int32_t)dead_zone_pct);
    return (int8_t)((v < 0) ? -mag : mag);
}

static int held_axis_value(JOY_Direction d, int8_t x, int8_t y)
{
    switch (d) {
    case JOY_UP:    return y;
    case JOY_DOWN:  return -y;
    case JOY_RIGHT: return x;
    case JOY_LEFT:  return -x;
    default:        return 0;
    }
}

JOY_Direction JOY_Classify(int8_t x, int8_t y, JOY_Direction prev, uint8_t press_pct, uint8_t release_pct)
{
    if (prev != JOY_NONE && held_axis_value(prev, x, y) >= (int)release_pct) {
        return prev;
    }

    int ax = abs8(x);
    int ay = abs8(y);
    if (ax < (int)press_pct && ay < (int)press_pct) {
        return JOY_NONE;
    }
    if (ay >= ax) {
        return (y > 0) ? JOY_UP : JOY_DOWN;
    }
    return (x > 0) ? JOY_RIGHT : JOY_LEFT;
}
