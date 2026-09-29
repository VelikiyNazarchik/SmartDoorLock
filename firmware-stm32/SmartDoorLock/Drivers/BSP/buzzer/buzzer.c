#include "buzzer.h"
#include "app_config.h"

/* Melodies: {frequency Hz, duration ms}, 0 Hz = pause, {0, 0} = end. Tones come from app_config.h. */
static const BUZZ_Note click[]   = { { CFG_BEEP_CLICK_HZ, 12 }, { 0, 0 } };
static const BUZZ_Note ok[]      = { { CFG_BEEP_OK_HZ, 80 }, { 0, 60 }, { CFG_BEEP_OK_HZ, 80 }, { 0, 0 } };
static const BUZZ_Note denied[]  = { { CFG_BEEP_DENIED_HZ, 600 }, { 0, 0 } };
static const BUZZ_Note siren[]   = { { CFG_BEEP_ALARM_HZ, 250 }, { CFG_BEEP_ALARM_LOW_HZ, 250 }, { 0, 0 } };
static const BUZZ_Note boot[]    = { { 1500, 70 }, { 2000, 70 }, { 2600, 110 }, { 0, 0 } };
static const BUZZ_Note error[]   = { { CFG_BEEP_DENIED_HZ, 90 }, { 0, 70 }, { 0, 0 } };

static const struct {
    const BUZZ_Note *notes;
    uint8_t          times;
} melodies[BUZZ_PATTERN_COUNT] = {
    [BUZZ_NONE]    = { NULL, 0 },
    [BUZZ_CLICK]   = { click, 1 },
    [BUZZ_OK]      = { ok, 1 },
    [BUZZ_DENIED]  = { denied, 1 },
    [BUZZ_LOCKOUT] = { siren, 6 },
    [BUZZ_BOOT]    = { boot, 1 },
    [BUZZ_ERROR]   = { error, 3 },
};

/* Timer counts at 1 MHz: tone frequency = 1 000 000 / (ARR + 1).
 * Duty 50 % (CCR = (ARR + 1) / 2) is the loudest, CCR = 0 is silence.
 * ARR/CCR preload is enabled, so the new tone starts cleanly at the next timer period. */
static void play_tone(BUZZ_Handle *b, uint16_t freq_hz)
{
    if (freq_hz == 0U) {
        __HAL_TIM_SET_COMPARE(b->htim, b->channel, 0U);
        return;
    }
    uint32_t period = CFG_BUZZER_TICK_HZ / freq_hz;
    __HAL_TIM_SET_AUTORELOAD(b->htim, period - 1U);
    __HAL_TIM_SET_COMPARE(b->htim, b->channel, period / 2U);
}

static void start_note(BUZZ_Handle *b)
{
    play_tone(b, b->melody[b->note].freq_hz);
    b->note_started = HAL_GetTick();
}

bool BUZZER_Init(BUZZ_Handle *b, TIM_HandleTypeDef *htim, uint32_t channel)
{
    b->htim = htim;
    b->channel = channel;
    b->melody = NULL;
    __HAL_TIM_SET_COMPARE(htim, channel, 0U);
    return HAL_TIM_PWM_Start(htim, channel) == HAL_OK;
}

void BUZZER_Play(BUZZ_Handle *b, BUZZ_Pattern p)
{
    if (p == BUZZ_NONE || p >= BUZZ_PATTERN_COUNT) {
        return;
    }
    b->melody = melodies[p].notes;
    b->repeats_left = (uint8_t)(melodies[p].times - 1U);
    b->note = 0;
    start_note(b);
}

void BUZZER_Update(BUZZ_Handle *b)
{
    if (b->melody == NULL || HAL_GetTick() - b->note_started < b->melody[b->note].dur_ms) {
        return;
    }
    b->note++;
    if (b->melody[b->note].dur_ms == 0U) {
        if (b->repeats_left == 0U) {
            BUZZER_Stop(b);
            return;
        }
        b->repeats_left--;
        b->note = 0;
    }
    start_note(b);
}

void BUZZER_Stop(BUZZ_Handle *b)
{
    play_tone(b, 0);
    b->melody = NULL;
}

bool BUZZER_IsPlaying(const BUZZ_Handle *b)
{
    return b->melody != NULL;
}
