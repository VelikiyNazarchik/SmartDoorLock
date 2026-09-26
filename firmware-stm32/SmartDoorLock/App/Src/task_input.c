#include <string.h>
#include "main.h"
#include "adc.h"
#include "joystick.h"
#include "app_config.h"
#include "app_rtos.h"
#include "logger.h"
#include "supervisor.h"

/* tInput: owner of ADC1 and the two buttons. Buttons raise EXTI interrupts; the ISR only sets a
 * thread flag, debouncing happens here in task context. */

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint32_t      flag;
    AppEventType  event;
    bool          pending;    /* edge seen, waiting for the debounce time */
    bool          pressed;    /* reported, waiting for release */
    uint32_t      edge_ms;
} Button;

static Button s_buttons[] = {
    { EXIT_BTN_GPIO_Port, EXIT_BTN_Pin, FLAG_BTN_EXIT, EV_EXIT_BUTTON, false, false, 0 },
    { JOY_SW_GPIO_Port,   JOY_SW_Pin,   FLAG_BTN_JOY,  EV_JOY_CLICK,   false, false, 0 },
};
#define BUTTON_COUNT (sizeof(s_buttons) / sizeof(s_buttons[0]))

static JOY_Handle s_joy;

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (tInputHandle == NULL) {
        return;   /* before the scheduler starts */
    }
    if (pin == EXIT_BTN_Pin) {
        (void)osThreadFlagsSet(tInputHandle, FLAG_BTN_EXIT);
    } else if (pin == JOY_SW_Pin) {
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

static void handle_buttons(uint32_t flags, uint32_t now)
{
    for (uint32_t i = 0; i < BUTTON_COUNT; i++) {
        Button *b = &s_buttons[i];
        bool down = (HAL_GPIO_ReadPin(b->port, b->pin) == GPIO_PIN_RESET);   /* pull-up, active low */

        if ((flags & b->flag) && !b->pressed && !b->pending) {
            b->pending = true;
            b->edge_ms = now;
        }
        if (b->pending && (now - b->edge_ms) >= CFG_BTN_DEBOUNCE_MS) {
            b->pending = false;
            if (down) {
                b->pressed = true;
                post(b->event, JOY_NONE);
            }
        }
        if (b->pressed && !down) {
            b->pressed = false;
        }
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
        uint32_t flags = osThreadFlagsWait(FLAG_BTN_EXIT | FLAG_BTN_JOY, osFlagsWaitAny, CFG_INPUT_POLL_MS);
        if (flags & osFlagsError) {
            flags = 0;
        }
        uint32_t now = osKernelGetTickCount();
        handle_buttons(flags, now);

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
            post(EV_JOY_DIR, dir);   /* auto-repeat for scrolling lists */
            repeat_at = now + CFG_JOY_REPEAT_MS;
        }
    }
}
