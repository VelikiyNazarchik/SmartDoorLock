#ifndef BUZZER_PATTERN_H
#define BUZZER_PATTERN_H

/* HAL-free on purpose: access_ctrl (pure logic, PC-tested) returns these values. */
typedef enum {
    BUZZ_NONE = 0,
    BUZZ_CLICK,        /* short tick on joystick move */
    BUZZ_OK,           /* two short high beeps */
    BUZZ_DENIED,       /* one long low beep */
    BUZZ_LOCKOUT,      /* repeating siren */
    BUZZ_BOOT,         /* short start-up melody */
    BUZZ_ERROR,        /* three short low beeps */
    BUZZ_PATTERN_COUNT
} BUZZ_Pattern;

#endif
