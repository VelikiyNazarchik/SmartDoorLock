#ifndef BUZZER_H
#define BUZZER_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "buzzer_pattern.h"

/* Passive buzzer on a PWM channel. Non-blocking: BUZZER_Play() only starts a melody,
 * BUZZER_Update() (called every ~10 ms) switches notes when their time is up. */

typedef struct {
    uint16_t freq_hz;
    uint16_t dur_ms;
} BUZZ_Note;

typedef struct {
    TIM_HandleTypeDef *htim;
    uint32_t           channel;
    const BUZZ_Note   *melody;
    uint8_t            note;
    uint8_t            repeats_left;
    uint32_t           note_started;
} BUZZ_Handle;

bool BUZZER_Init(BUZZ_Handle *b, TIM_HandleTypeDef *htim, uint32_t channel);
void BUZZER_Play(BUZZ_Handle *b, BUZZ_Pattern p);
void BUZZER_Update(BUZZ_Handle *b);
void BUZZER_Stop(BUZZ_Handle *b);
bool BUZZER_IsPlaying(const BUZZ_Handle *b);

#endif
