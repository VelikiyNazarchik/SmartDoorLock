#include <stdio.h>
#include "timeutil.h"

#define SECS_PER_DAY      86400UL
#define EPOCH_2000        946684800UL   /* 2000-01-01 00:00:00 UTC, субота */

static bool is_leap(uint16_t y)
{
    return ((y % 4U) == 0U && (y % 100U) != 0U) || ((y % 400U) == 0U);
}

static uint8_t days_in_month(uint16_t y, uint8_t m)
{
    static const uint8_t dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return (m == 2U && is_leap(y)) ? 29U : dim[m - 1U];
}

bool TU_IsValid(const TU_DateTime *dt)
{
    return dt->year >= 2000U && dt->year <= 2099U &&
           dt->month >= 1U && dt->month <= 12U &&
           dt->day >= 1U && dt->day <= days_in_month(dt->year, dt->month) &&
           dt->hour < 24U && dt->minute < 60U && dt->second < 60U;
}

uint32_t TU_ToEpoch(const TU_DateTime *dt)
{
    uint32_t days = 0;
    for (uint16_t y = 2000U; y < dt->year; y++) {
        days += is_leap(y) ? 366U : 365U;
    }
    for (uint8_t m = 1U; m < dt->month; m++) {
        days += days_in_month(dt->year, m);
    }
    days += (uint32_t)dt->day - 1U;
    return EPOCH_2000 + days * SECS_PER_DAY +
           (uint32_t)dt->hour * 3600UL + (uint32_t)dt->minute * 60UL + dt->second;
}

void TU_FromEpoch(uint32_t epoch, TU_DateTime *dt)
{
    if (epoch < EPOCH_2000) {
        epoch = EPOCH_2000;
    }
    uint32_t t = epoch - EPOCH_2000;
    uint32_t days = t / SECS_PER_DAY;
    uint32_t rem = t % SECS_PER_DAY;

    dt->hour = (uint8_t)(rem / 3600UL);
    dt->minute = (uint8_t)((rem % 3600UL) / 60UL);
    dt->second = (uint8_t)(rem % 60UL);
    dt->weekday = (uint8_t)(((days + 5U) % 7U) + 1U);   /* 2000-01-01 — субота (6) */

    uint16_t y = 2000U;
    for (;;) {
        uint32_t ylen = is_leap(y) ? 366U : 365U;
        if (days < ylen) {
            break;
        }
        days -= ylen;
        y++;
    }
    uint8_t m = 1U;
    while (days >= days_in_month(y, m)) {
        days -= days_in_month(y, m);
        m++;
    }
    dt->year = y;
    dt->month = m;
    dt->day = (uint8_t)(days + 1U);
}

bool TU_Parse(const char *s, TU_DateTime *dt)
{
    unsigned y, mo, d, h, mi, se;
    char tail;
    if (sscanf(s, "%4u-%2u-%2u %2u:%2u:%2u%c", &y, &mo, &d, &h, &mi, &se, &tail) != 6) {
        return false;
    }
    dt->year = (uint16_t)y;
    dt->month = (uint8_t)mo;
    dt->day = (uint8_t)d;
    dt->hour = (uint8_t)h;
    dt->minute = (uint8_t)mi;
    dt->second = (uint8_t)se;
    if (!TU_IsValid(dt)) {
        return false;
    }
    TU_DateTime tmp;
    TU_FromEpoch(TU_ToEpoch(dt), &tmp);
    dt->weekday = tmp.weekday;
    return true;
}

void TU_Format(const TU_DateTime *dt, char *buf, size_t len)
{
    (void)snprintf(buf, len, "%04u-%02u-%02u %02u:%02u:%02u",
                   (unsigned)dt->year, (unsigned)dt->month, (unsigned)dt->day,
                   (unsigned)dt->hour, (unsigned)dt->minute, (unsigned)dt->second);
}
