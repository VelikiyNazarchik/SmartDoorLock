#ifndef TIMEUTIL_H
#define TIMEUTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  weekday;
} TU_DateTime;

bool     TU_IsValid(const TU_DateTime *dt);
uint32_t TU_ToEpoch(const TU_DateTime *dt);
void     TU_FromEpoch(uint32_t epoch, TU_DateTime *dt);
bool     TU_Parse(const char *s, TU_DateTime *dt);
void     TU_Format(const TU_DateTime *dt, char *buf, size_t len);

#endif
