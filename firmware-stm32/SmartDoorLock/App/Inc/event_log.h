#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "app_events.h"

/* Who / when / what: RAM ring buffer, oldest entries are overwritten. HAL-free. */

typedef enum {
    LOGE_BOOT = 0,
    LOGE_ACCESS_GRANTED,
    LOGE_ACCESS_DENIED,
    LOGE_LOCKOUT,
    LOGE_EXIT_BUTTON,
    LOGE_ADMIN_LOGIN,
    LOGE_CARD_ADDED,
    LOGE_CARD_REMOVED,
    LOGE_RFID_FAULT,
    LOGE_RFID_RECOVERED,
    LOGE_NET_UNLOCK,
    LOGE_SETTINGS_CHANGED,
    LOGE_STORAGE_CORRUPT,
    LOGE_WATCHDOG_RESET,
    LOGE_COUNT
} LogEventType;

#define ELOG_FLAG_TIME_VALID 0x01U   /* ts is Unix time; otherwise seconds since boot */

typedef struct {
    uint32_t ts;
    uint8_t  type;
    uint8_t  flags;
    uint8_t  uid_len;
    uint8_t  uid[UID_MAX_LEN];
} EventRecord;

typedef struct {
    EventRecord rec[CFG_EVENT_LOG_SIZE];
    uint16_t    head;     /* next slot to write */
    uint16_t    count;
    uint32_t    total;    /* events ever added, survives wrap-around */
} EventLog;

void               ELOG_Init(EventLog *log);
void               ELOG_Add(EventLog *log, LogEventType type, const uint8_t *uid, uint8_t uid_len,
                            uint32_t ts, bool ts_is_real_time);
uint16_t           ELOG_Count(const EventLog *log);
/* idx 0 = newest */
const EventRecord *ELOG_Get(const EventLog *log, uint16_t idx);
const char        *ELOG_TypeName(LogEventType type);

#endif
