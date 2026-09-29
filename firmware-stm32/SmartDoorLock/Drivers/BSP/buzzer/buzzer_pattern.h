#ifndef BUZZER_PATTERN_H
#define BUZZER_PATTERN_H

typedef enum {
    BUZZ_NONE = 0,
    BUZZ_CLICK,
    BUZZ_OK,
    BUZZ_DENIED,
    BUZZ_LOCKOUT,
    BUZZ_BOOT,
    BUZZ_ERROR,
    BUZZ_PATTERN_COUNT
} BUZZ_Pattern;

#endif
