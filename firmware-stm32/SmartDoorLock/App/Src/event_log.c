#include <string.h>
#include "event_log.h"

void ELOG_Init(EventLog *log)
{
    memset(log, 0, sizeof(*log));
}

void ELOG_Add(EventLog *log, LogEventType type, const uint8_t *uid, uint8_t uid_len,
              uint32_t ts, bool ts_is_real_time)
{
    EventRecord *r = &log->rec[log->head];
    memset(r, 0, sizeof(*r));
    r->ts = ts;
    r->type = (uint8_t)type;
    r->flags = ts_is_real_time ? ELOG_FLAG_TIME_VALID : 0U;
    if (uid != NULL && uid_len > 0U) {
        r->uid_len = (uid_len > UID_MAX_LEN) ? (uint8_t)UID_MAX_LEN : uid_len;
        memcpy(r->uid, uid, r->uid_len);
    }
    log->head = (uint16_t)((log->head + 1U) % CFG_EVENT_LOG_SIZE);
    if (log->count < CFG_EVENT_LOG_SIZE) {
        log->count++;
    }
    log->total++;
}

uint16_t ELOG_Count(const EventLog *log)
{
    return log->count;
}

const EventRecord *ELOG_Get(const EventLog *log, uint16_t idx)
{
    if (idx >= log->count) {
        return NULL;
    }
    uint16_t pos = (uint16_t)((log->head + CFG_EVENT_LOG_SIZE - 1U - idx) % CFG_EVENT_LOG_SIZE);
    return &log->rec[pos];
}

const char *ELOG_TypeName(LogEventType type)
{
    static const char *const names[LOGE_COUNT] = {
        [LOGE_BOOT]             = "BOOT",
        [LOGE_ACCESS_GRANTED]   = "ACCESS_OK",
        [LOGE_ACCESS_DENIED]    = "ACCESS_DENIED",
        [LOGE_LOCKOUT]          = "LOCKOUT",
        [LOGE_ADMIN_LOGIN]      = "ADMIN_LOGIN",
        [LOGE_CARD_ADDED]       = "CARD_ADDED",
        [LOGE_CARD_REMOVED]     = "CARD_REMOVED",
        [LOGE_RFID_FAULT]       = "RFID_FAULT",
        [LOGE_RFID_RECOVERED]   = "RFID_OK",
        [LOGE_NET_UNLOCK]       = "NET_UNLOCK",
        [LOGE_SETTINGS_CHANGED] = "SETTINGS",
        [LOGE_STORAGE_CORRUPT]  = "STORAGE_CORRUPT",
        [LOGE_WATCHDOG_RESET]   = "WATCHDOG_RESET",
        [LOGE_ADMIN_RECOVERY]   = "ADMIN_RECOVERY",
    };
    return (type < LOGE_COUNT) ? names[type] : "?";
}
