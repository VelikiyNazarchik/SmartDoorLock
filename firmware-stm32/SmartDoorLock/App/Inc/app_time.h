#ifndef APP_TIME_H
#define APP_TIME_H

#include <stdbool.h>
#include <stdint.h>

/* Real-time clock for event-log time stamps, plus the RTC backup registers.
 *
 * Configured here with direct register access instead of CubeMX: the generated SystemClock_Config
 * starts LSE together with HSE and ends in Error_Handler() forever if the 32.768 kHz crystal does
 * not oscillate. This code waits for LSE with a timeout and falls back to LSI (less accurate,
 * but the lock keeps working). The calendar survives resets; without a VBAT cell it is lost at
 * power-off and has to be set again (debug console "time", later over the network). */

typedef enum {
    TIME_SRC_NONE = 0,
    TIME_SRC_LSE,
    TIME_SRC_LSI
} TimeSource;

/* wait_ms/kick are called while waiting for LSE: yield to other tasks and feed the watchdog. */
TimeSource  TIME_Init(void (*wait_ms)(uint32_t ms), void (*kick)(void));
TimeSource  TIME_Source(void);
const char *TIME_SourceName(void);
bool        TIME_IsSet(void);
/* Unix time, 0 while the calendar has not been set. */
uint32_t    TIME_Now(void);
bool        TIME_Set(uint32_t epoch);

/* RTC backup registers 0..19: survive resets (not power loss). Usable before TIME_Init. */
uint32_t    TIME_BkpRead(uint32_t idx);
void        TIME_BkpWrite(uint32_t idx, uint32_t value);

#endif
