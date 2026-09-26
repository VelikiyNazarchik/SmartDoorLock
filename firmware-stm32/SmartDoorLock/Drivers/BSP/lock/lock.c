#include "lock.h"

static void lock_drive(LOCK_Handle *h, bool open)
{
    bool energize = (open == h->energize_to_open);
    HAL_GPIO_WritePin(h->port, h->pin, energize ? GPIO_PIN_SET : GPIO_PIN_RESET);
    h->is_open = open;
}

LOCK_Status LOCK_Init(LOCK_Handle *h, GPIO_TypeDef *port, uint16_t pin, bool energize_to_open)
{
    if (h == NULL || port == NULL) {
        return LOCK_ERR_PARAM;
    }
    h->port = port;
    h->pin = pin;
    h->energize_to_open = energize_to_open;
    lock_drive(h, false);
    return LOCK_OK;
}

void LOCK_Open(LOCK_Handle *h)
{
    lock_drive(h, true);
}

void LOCK_Close(LOCK_Handle *h)
{
    lock_drive(h, false);
}

bool LOCK_IsOpen(const LOCK_Handle *h)
{
    return h->is_open;
}
