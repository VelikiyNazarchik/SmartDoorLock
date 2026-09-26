#include "test.h"
#include "timeutil.h"

static void test_known_epochs(void)
{
    TU_DateTime dt = { 2000, 1, 1, 0, 0, 0, 0 };
    CHECK_EQ(TU_ToEpoch(&dt), 946684800UL);
    TU_DateTime d2 = { 2026, 9, 26, 12, 34, 56, 0 };
    CHECK_EQ(TU_ToEpoch(&d2), 1790426096UL);
    TU_DateTime leap = { 2024, 2, 29, 23, 59, 59, 0 };
    CHECK_EQ(TU_ToEpoch(&leap), 1709251199UL);
}

static void test_roundtrip_and_weekday(void)
{
    TU_DateTime dt;
    TU_FromEpoch(1790426096UL, &dt);
    CHECK_EQ(dt.year, 2026);
    CHECK_EQ(dt.month, 9);
    CHECK_EQ(dt.day, 26);
    CHECK_EQ(dt.hour, 12);
    CHECK_EQ(dt.minute, 34);
    CHECK_EQ(dt.second, 56);
    CHECK_EQ(dt.weekday, 6);          /* Saturday */

    TU_FromEpoch(946684800UL, &dt);
    CHECK_EQ(dt.weekday, 6);          /* 2000-01-01 was a Saturday */

    for (uint32_t t = 946684800UL; t < 4102444800UL; t += 7777777UL) {
        TU_FromEpoch(t, &dt);
        CHECK(TU_IsValid(&dt));
        CHECK_EQ(TU_ToEpoch(&dt), t);
    }
}

static void test_parse_and_format(void)
{
    TU_DateTime dt;
    CHECK(TU_Parse("2026-09-26 12:34:56", &dt));
    CHECK_EQ(TU_ToEpoch(&dt), 1790426096UL);
    CHECK_EQ(dt.weekday, 6);
    char buf[24];
    TU_Format(&dt, buf, sizeof(buf));
    CHECK(strcmp(buf, "2026-09-26 12:34:56") == 0);

    CHECK(!TU_Parse("2026-02-30 00:00:00", &dt));
    CHECK(!TU_Parse("2026-13-01 00:00:00", &dt));
    CHECK(!TU_Parse("1999-12-31 23:59:59", &dt));
    CHECK(!TU_Parse("2026-09-26 24:00:00", &dt));
    CHECK(!TU_Parse("2026-09-26", &dt));
    CHECK(!TU_Parse("2026-09-26 12:34:56x", &dt));
}

int main(void)
{
    printf("test_timeutil\n");
    RUN(test_known_epochs);
    RUN(test_roundtrip_and_weekday);
    RUN(test_parse_and_format);
    return TEST_REPORT();
}
