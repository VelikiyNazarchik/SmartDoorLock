#ifndef LOCK_H
#define LOCK_H

#include <stdbool.h>
#include "stm32f4xx_hal.h"

typedef enum {
    LOCK_OK = 0,
    LOCK_ERR_PARAM
} LOCK_Status;

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    bool          energize_to_open;   /* true: solenoid/strike (power opens), false: maglock (power holds) */
    bool          is_open;
} LOCK_Handle;

LOCK_Status LOCK_Init(LOCK_Handle *h, GPIO_TypeDef *port, uint16_t pin, bool energize_to_open);
void        LOCK_Open(LOCK_Handle *h);
void        LOCK_Close(LOCK_Handle *h);
bool        LOCK_IsOpen(const LOCK_Handle *h);

#endif
