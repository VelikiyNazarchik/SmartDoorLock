#ifndef BUZZER_H
#define BUZZER_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "buzzer_pattern.h"

typedef enum {
    BUZZ_STATUS_OK = 0,
    BUZZ_STATUS_ERR_PARAM,
    BUZZ_STATUS_ERR_HAL
} BUZZ_Status;

typedef struct {
    uint16_t freq_hz;   /* 0 = pause */
    uint16_t dur_ms;    /* 0 = end of sequence */
} BUZZ_Note;

typedef struct {
    const BUZZ_Note *notes;
    uint8_t          repeats;   /* how many times the sequence is played (0 is treated as 1) */
} BUZZ_Melody;

typedef struct {
    TIM_HandleTypeDef *htim;
    uint32_t           channel;
    uint32_t           tick_hz;       /* timer counter clock after the prescaler */
    const BUZZ_Melody *melodies;      /* indexed by BUZZ_Pattern */
    const BUZZ_Note   *seq;
    uint8_t            idx;
    uint8_t            repeats_left;
    uint32_t           note_start_ms;
    bool               playing;
} BUZZ_Handle;

BUZZ_Status BUZZER_Init(BUZZ_Handle *b, TIM_HandleTypeDef *htim, uint32_t channel,
                        uint32_t tick_hz, const BUZZ_Melody *melodies);
void        BUZZER_Play(BUZZ_Handle *b, BUZZ_Pattern p);
void        BUZZER_Update(BUZZ_Handle *b);
void        BUZZER_Stop(BUZZ_Handle *b);
bool        BUZZER_IsPlaying(const BUZZ_Handle *b);

#endif
