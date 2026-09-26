#include "test.h"
#include "event_log.h"

static EventLog log_;

static void test_empty(void)
{
    ELOG_Init(&log_);
    CHECK_EQ(ELOG_Count(&log_), 0);
    CHECK(ELOG_Get(&log_, 0) == NULL);
}

static void test_newest_first(void)
{
    ELOG_Init(&log_);
    uint8_t uid[4] = { 1, 2, 3, 4 };
    ELOG_Add(&log_, LOGE_BOOT, NULL, 0, 100, false);
    ELOG_Add(&log_, LOGE_ACCESS_GRANTED, uid, 4, 200, true);
    CHECK_EQ(ELOG_Count(&log_), 2);
    const EventRecord *r = ELOG_Get(&log_, 0);
    CHECK_EQ(r->type, LOGE_ACCESS_GRANTED);
    CHECK_EQ(r->ts, 200);
    CHECK_EQ(r->flags & ELOG_FLAG_TIME_VALID, ELOG_FLAG_TIME_VALID);
    CHECK_EQ(r->uid_len, 4);
    CHECK_EQ(r->uid[3], 4);
    r = ELOG_Get(&log_, 1);
    CHECK_EQ(r->type, LOGE_BOOT);
    CHECK_EQ(r->uid_len, 0);
    CHECK_EQ(r->flags, 0);
    CHECK(ELOG_Get(&log_, 2) == NULL);
}

static void test_wrap_around_keeps_newest(void)
{
    ELOG_Init(&log_);
    for (uint32_t i = 0; i < CFG_EVENT_LOG_SIZE + 10U; i++) {
        ELOG_Add(&log_, LOGE_ACCESS_DENIED, NULL, 0, i, false);
    }
    CHECK_EQ(ELOG_Count(&log_), CFG_EVENT_LOG_SIZE);
    CHECK_EQ(log_.total, CFG_EVENT_LOG_SIZE + 10U);
    CHECK_EQ(ELOG_Get(&log_, 0)->ts, CFG_EVENT_LOG_SIZE + 9U);
    CHECK_EQ(ELOG_Get(&log_, CFG_EVENT_LOG_SIZE - 1U)->ts, 10);
}

static void test_long_uid_truncated(void)
{
    ELOG_Init(&log_);
    uint8_t uid[10] = { 0 };
    ELOG_Add(&log_, LOGE_ACCESS_DENIED, uid, 10, 0, false);
    CHECK_EQ(ELOG_Get(&log_, 0)->uid_len, UID_MAX_LEN);
}

static void test_type_names(void)
{
    CHECK(strcmp(ELOG_TypeName(LOGE_LOCKOUT), "LOCKOUT") == 0);
    CHECK(strcmp(ELOG_TypeName(LOGE_COUNT), "?") == 0);
    for (int t = 0; t < LOGE_COUNT; t++) {
        CHECK(ELOG_TypeName((LogEventType)t) != NULL);
    }
}

int main(void)
{
    printf("test_event_log\n");
    RUN(test_empty);
    RUN(test_newest_first);
    RUN(test_wrap_around_keeps_newest);
    RUN(test_long_uid_truncated);
    RUN(test_type_names);
    return TEST_REPORT();
}
