#include "joystick.h"

#define JOY_ADC_MAX          4095U
#define JOY_CAL_SAMPLES      16U
#define JOY_CAL_MIN_CENTER   1000U
#define JOY_CAL_MAX_CENTER   3100U
#define JOY_ADC_TIMEOUT_MS   2U

static JOY_Status read_channel(JOY_Handle *j, uint32_t channel, uint16_t *out)
{
    ADC_ChannelConfTypeDef cfg = {0};
    cfg.Channel = channel;
    cfg.Rank = 1;
    cfg.SamplingTime = ADC_SAMPLETIME_84CYCLES;
    if (HAL_ADC_ConfigChannel(j->hadc, &cfg) != HAL_OK) {
        return JOY_ERR_ADC;
    }
    if (HAL_ADC_Start(j->hadc) != HAL_OK) {
        return JOY_ERR_ADC;
    }
    HAL_StatusTypeDef st = HAL_ADC_PollForConversion(j->hadc, JOY_ADC_TIMEOUT_MS);
    uint32_t v = HAL_ADC_GetValue(j->hadc);
    (void)HAL_ADC_Stop(j->hadc);
    if (st != HAL_OK) {
        return JOY_ERR_ADC;
    }
    *out = (uint16_t)v;
    return JOY_OK;
}

JOY_Status JOY_Init(JOY_Handle *j, ADC_HandleTypeDef *hadc, uint32_t ch_x, uint32_t ch_y, const JOY_Params *p)
{
    if (j == NULL || hadc == NULL || p == NULL) {
        return JOY_ERR_PARAM;
    }
    j->hadc = hadc;
    j->ch_x = ch_x;
    j->ch_y = ch_y;
    j->p = *p;
    j->center_x = JOY_ADC_MAX / 2U;
    j->center_y = JOY_ADC_MAX / 2U;
    j->x = 0;
    j->y = 0;
    j->dir = JOY_NONE;
    return JOY_OK;
}

JOY_Status JOY_Calibrate(JOY_Handle *j)
{
    uint32_t sx = 0, sy = 0;
    for (uint32_t i = 0; i < JOY_CAL_SAMPLES; i++) {
        uint16_t vx, vy;
        if (read_channel(j, j->ch_x, &vx) != JOY_OK || read_channel(j, j->ch_y, &vy) != JOY_OK) {
            return JOY_ERR_ADC;
        }
        sx += vx;
        sy += vy;
    }
    uint16_t cx = (uint16_t)(sx / JOY_CAL_SAMPLES);
    uint16_t cy = (uint16_t)(sy / JOY_CAL_SAMPLES);
    if (cx < JOY_CAL_MIN_CENTER || cx > JOY_CAL_MAX_CENTER ||
        cy < JOY_CAL_MIN_CENTER || cy > JOY_CAL_MAX_CENTER) {
        /* Stick held or disconnected at boot: keep the nominal center. */
        return JOY_ERR_CALIBRATION;
    }
    j->center_x = cx;
    j->center_y = cy;
    return JOY_OK;
}

JOY_Status JOY_Read(JOY_Handle *j, JOY_Direction *dir)
{
    uint16_t rx, ry;
    JOY_Status st = read_channel(j, j->ch_x, &rx);
    if (st == JOY_OK) {
        st = read_channel(j, j->ch_y, &ry);
    }
    if (st != JOY_OK) {
        return st;
    }

    int8_t nx = JOY_Normalize(rx, j->center_x, JOY_ADC_MAX, j->p.dead_zone_pct);
    int8_t ny = JOY_Normalize(ry, j->center_y, JOY_ADC_MAX, j->p.dead_zone_pct);
    if (j->p.swap_xy) {
        int8_t t = nx;
        nx = ny;
        ny = t;
    }
    if (j->p.invert_x) {
        nx = (int8_t)-nx;
    }
    if (j->p.invert_y) {
        ny = (int8_t)-ny;
    }

    j->x = nx;
    j->y = ny;
    j->dir = JOY_Classify(nx, ny, j->dir, j->p.press_pct, j->p.release_pct);
    if (dir != NULL) {
        *dir = j->dir;
    }
    return JOY_OK;
}
