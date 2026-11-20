#include <string.h>
#include "main.h"
#include "adc.h"
#include "joystick.h"
#include "app_config.h"
#include "app_rtos.h"
#include "logger.h"
#include "supervisor.h"

/* tInput: власник ADC1 (джойстик) і кнопки джойстика. Кнопка викликає переривання EXTI;
 * переривання лише ставить прапорець задачі, а антидребезг робиться тут, у задачі. */

typedef struct {
    bool     pending;
    bool     pressed;
    bool     long_sent;
    uint32_t edge_ms;
} Button;

static Button     s_btn;
static JOY_Handle s_joy;

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (tInputHandle != NULL && pin == JOY_SW_Pin) {
        (void)osThreadFlagsSet(tInputHandle, FLAG_BTN_JOY);
    }
}

static void post(AppEventType type, JOY_Direction dir)
{
    AppEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.dir = dir;
    (void)App_PostCtrl(&ev);
}

static void handle_button(uint32_t flags, uint32_t now)
{
    bool down = (HAL_GPIO_ReadPin(JOY_SW_GPIO_Port, JOY_SW_Pin) == GPIO_PIN_RESET);

    if ((flags & FLAG_BTN_JOY) && !s_btn.pressed && !s_btn.pending) {
        s_btn.pending = true;
        s_btn.edge_ms = now;
    }
    if (s_btn.pending && (now - s_btn.edge_ms) >= CFG_BTN_DEBOUNCE_MS) {
        s_btn.pending = false;
        if (down) {
            s_btn.pressed = true;
            s_btn.long_sent = false;
            post(EV_JOY_CLICK, JOY_NONE);
        }
    }
    if (s_btn.pressed && !s_btn.long_sent && down && (now - s_btn.edge_ms) >= CFG_JOY_LONG_PRESS_MS) {
        s_btn.long_sent = true;
        post(EV_JOY_LONG, JOY_NONE);
    }
    if (s_btn.pressed && !down) {
        s_btn.pressed = false;
    }
}

void TaskInput(void *arg)
{
    (void)arg;
    static const JOY_Params params = {
        .dead_zone_pct = CFG_JOY_DEAD_ZONE_PCT,
        .press_pct = CFG_JOY_PRESS_PCT,
        .release_pct = CFG_JOY_RELEASE_PCT,
        .invert_x = CFG_JOY_INVERT_X,
        .invert_y = CFG_JOY_INVERT_Y,
        .swap_xy = CFG_JOY_SWAP_XY,
    };
    (void)JOY_Init(&s_joy, &hadc1, ADC_CHANNEL_0, ADC_CHANNEL_3, &params);
    JOY_Status js = JOY_Calibrate(&s_joy);
    if (js != JOY_OK) {
        LOG_WARN("joystick calibration failed (%d), using nominal center", (int)js);
    } else {
        LOG_INFO("joystick center x=%u y=%u", s_joy.center_x, s_joy.center_y);
    }

    JOY_Direction prev = JOY_NONE;
    uint32_t repeat_at = 0;

    for (;;) {
        SUP_Heartbeat(TASK_INPUT);
        uint32_t flags = osThreadFlagsWait(FLAG_BTN_JOY, osFlagsWaitAny, CFG_INPUT_POLL_MS);
        if (flags & osFlagsError) {
            flags = 0;
        }
        uint32_t now = osKernelGetTickCount();
        handle_button(flags, now);

        JOY_Direction dir;
        if (JOY_Read(&s_joy, &dir) != JOY_OK) {
            continue;
        }
        if (dir != prev) {
            prev = dir;
            if (dir != JOY_NONE) {
                post(EV_JOY_DIR, dir);
                repeat_at = now + CFG_JOY_REPEAT_DELAY_MS;
            }
        } else if ((dir == JOY_UP || dir == JOY_DOWN) && (int32_t)(now - repeat_at) >= 0) {
            post(EV_JOY_DIR, dir);
            repeat_at = now + CFG_JOY_REPEAT_MS;
        }
    }
}
