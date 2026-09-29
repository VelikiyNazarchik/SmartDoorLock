#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <stdbool.h>
#include "stm32f4xx_hal.h"
#include "joystick_math.h"

typedef enum {
    JOY_OK = 0,
    JOY_ERR_PARAM,
    JOY_ERR_ADC,
    JOY_ERR_CALIBRATION
} JOY_Status;

typedef struct {
    uint8_t dead_zone_pct;
    uint8_t press_pct;
    uint8_t release_pct;
    bool    invert_x;
    bool    invert_y;
    bool    swap_xy;
} JOY_Params;

typedef struct {
    ADC_HandleTypeDef *hadc;
    uint32_t           ch_x;
    uint32_t           ch_y;
    JOY_Params         p;
    uint16_t           center_x;
    uint16_t           center_y;
    int8_t             x;
    int8_t             y;
    JOY_Direction      dir;
} JOY_Handle;

JOY_Status JOY_Init(JOY_Handle *j, ADC_HandleTypeDef *hadc, uint32_t ch_x, uint32_t ch_y, const JOY_Params *p);
JOY_Status JOY_Calibrate(JOY_Handle *j);
JOY_Status JOY_Read(JOY_Handle *j, JOY_Direction *dir);

#endif
