#ifndef TIMEUTIL_H
#define TIMEUTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Calendar <-> Unix time for 2000..2099 (the RTC range). HAL-free. */

typedef struct {
    uint16_t year;     /* 2000..2099 */
    uint8_t  month;    /* 1..12 */
    uint8_t  day;      /* 1..31 */
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  weekday;  /* 1 = Monday .. 7 = Sunday (RTC convention) */
} TU_DateTime;

bool     TU_IsValid(const TU_DateTime *dt);
uint32_t TU_ToEpoch(const TU_DateTime *dt);
void     TU_FromEpoch(uint32_t epoch, TU_DateTime *dt);
/* "YYYY-MM-DD HH:MM:SS" */
bool     TU_Parse(const char *s, TU_DateTime *dt);
void     TU_Format(const TU_DateTime *dt, char *buf, size_t len);

#endif
