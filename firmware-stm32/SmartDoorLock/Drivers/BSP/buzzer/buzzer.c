#include "buzzer.h"

/* Passive buzzer on a PWM channel: frequency = tick_hz / (ARR + 1), 50 % duty = loudest, CCR = 0 = silence.
 * ARR/CCR preload is enabled, so a new tone starts cleanly at the next update event. */
static void buzzer_tone(BUZZ_Handle *b, uint16_t freq_hz)
{
    if (freq_hz == 0U) {
        __HAL_TIM_SET_COMPARE(b->htim, b->channel, 0U);
        return;
    }
    uint32_t arr = b->tick_hz / freq_hz;
    if (arr < 2U) {
        arr = 2U;
    }
    if (arr > 0x10000U) {
        arr = 0x10000U;
    }
    arr -= 1U;
    __HAL_TIM_SET_AUTORELOAD(b->htim, arr);
    __HAL_TIM_SET_COMPARE(b->htim, b->channel, (arr + 1U) / 2U);
}

static void buzzer_start_note(BUZZ_Handle *b)
{
    buzzer_tone(b, b->seq[b->idx].freq_hz);
    b->note_start_ms = HAL_GetTick();
}

BUZZ_Status BUZZER_Init(BUZZ_Handle *b, TIM_HandleTypeDef *htim, uint32_t channel,
                        uint32_t tick_hz, const BUZZ_Melody *melodies)
{
    if (b == NULL || htim == NULL || tick_hz == 0U || melodies == NULL) {
        return BUZZ_STATUS_ERR_PARAM;
    }
    b->htim = htim;
    b->channel = channel;
    b->tick_hz = tick_hz;
    b->melodies = melodies;
    b->seq = NULL;
    b->playing = false;
    __HAL_TIM_SET_COMPARE(htim, channel, 0U);
    if (HAL_TIM_PWM_Start(htim, channel) != HAL_OK) {
        return BUZZ_STATUS_ERR_HAL;
    }
    return BUZZ_STATUS_OK;
}

void BUZZER_Play(BUZZ_Handle *b, BUZZ_Pattern p)
{
    if (p == BUZZ_NONE || p >= BUZZ_PATTERN_COUNT) {
        return;
    }
    const BUZZ_Melody *m = &b->melodies[p];
    if (m->notes == NULL || m->notes[0].dur_ms == 0U) {
        return;
    }
    b->seq = m->notes;
    b->idx = 0;
    b->repeats_left = (m->repeats > 0U) ? (uint8_t)(m->repeats - 1U) : 0U;
    b->playing = true;
    buzzer_start_note(b);
}

void BUZZER_Update(BUZZ_Handle *b)
{
    if (!b->playing) {
        return;
    }
    if ((HAL_GetTick() - b->note_start_ms) < b->seq[b->idx].dur_ms) {
        return;
    }
    b->idx++;
    if (b->seq[b->idx].dur_ms == 0U) {
        if (b->repeats_left == 0U) {
            BUZZER_Stop(b);
            return;
        }
        b->repeats_left--;
        b->idx = 0;
    }
    buzzer_start_note(b);
}

void BUZZER_Stop(BUZZ_Handle *b)
{
    buzzer_tone(b, 0U);
    b->playing = false;
    b->seq = NULL;
}

bool BUZZER_IsPlaying(const BUZZ_Handle *b)
{
    return b->playing;
}
