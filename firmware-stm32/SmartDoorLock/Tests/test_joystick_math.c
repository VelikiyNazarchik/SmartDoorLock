#include "test.h"
#include "joystick_math.h"

#define MAXV 4095U

static void test_center_and_ends(void)
{
    CHECK_EQ(JOY_Normalize(2048, 2048, MAXV, 0), 0);
    CHECK_EQ(JOY_Normalize(MAXV, 2048, MAXV, 0), 100);
    CHECK_EQ(JOY_Normalize(0, 2048, MAXV, 0), -100);
}

static void test_off_center_calibration(void)
{
    /* a stick resting at 1800 must still reach +-100 at both end stops */
    CHECK_EQ(JOY_Normalize(1800, 1800, MAXV, 10), 0);
    CHECK_EQ(JOY_Normalize(MAXV, 1800, MAXV, 10), 100);
    CHECK_EQ(JOY_Normalize(0, 1800, MAXV, 10), -100);
    CHECK_EQ(JOY_Normalize(900, 1800, MAXV, 0), -50);
}

static void test_dead_zone(void)
{
    /* 10 % off center with a 15 % dead zone is still 0 */
    CHECK_EQ(JOY_Normalize(2048 + 205, 2048, MAXV, 15), 0);
    CHECK_EQ(JOY_Normalize(2048 - 205, 2048, MAXV, 15), 0);
    /* just outside the dead zone the output starts near 0, not at 15 */
    int8_t v = JOY_Normalize(2048 + 350, 2048, MAXV, 15);
    CHECK(v > 0 && v < 10);
    CHECK_EQ(JOY_Normalize(MAXV, 2048, MAXV, 15), 100);
}

static void test_bad_calibration_is_safe(void)
{
    CHECK_EQ(JOY_Normalize(4000, 0, MAXV, 10), 0);
    CHECK_EQ(JOY_Normalize(4000, MAXV, MAXV, 10), 0);
    CHECK_EQ(JOY_Normalize(4000, 2048, MAXV, 100), 0);
}

static void test_classify_directions(void)
{
    CHECK_EQ(JOY_Classify(0, 0, JOY_NONE, 60, 35), JOY_NONE);
    CHECK_EQ(JOY_Classify(0, 80, JOY_NONE, 60, 35), JOY_UP);
    CHECK_EQ(JOY_Classify(0, -80, JOY_NONE, 60, 35), JOY_DOWN);
    CHECK_EQ(JOY_Classify(80, 0, JOY_NONE, 60, 35), JOY_RIGHT);
    CHECK_EQ(JOY_Classify(-80, 0, JOY_NONE, 60, 35), JOY_LEFT);
    CHECK_EQ(JOY_Classify(70, 90, JOY_NONE, 60, 35), JOY_UP);      /* dominant axis wins */
    CHECK_EQ(JOY_Classify(-95, 70, JOY_NONE, 60, 35), JOY_LEFT);
    CHECK_EQ(JOY_Classify(50, 50, JOY_NONE, 60, 35), JOY_NONE);    /* below press threshold */
}

static void test_classify_hysteresis(void)
{
    /* once UP is active it holds down to the release threshold... */
    CHECK_EQ(JOY_Classify(0, 40, JOY_UP, 60, 35), JOY_UP);
    /* ...and is dropped below it */
    CHECK_EQ(JOY_Classify(0, 30, JOY_UP, 60, 35), JOY_NONE);
    /* sliding to a new direction switches as soon as the old one is released */
    CHECK_EQ(JOY_Classify(90, 20, JOY_UP, 60, 35), JOY_RIGHT);
    /* a reversed axis never counts as "held" */
    CHECK_EQ(JOY_Classify(0, -90, JOY_UP, 60, 35), JOY_DOWN);
}

int main(void)
{
    printf("test_joystick_math\n");
    RUN(test_center_and_ends);
    RUN(test_off_center_calibration);
    RUN(test_dead_zone);
    RUN(test_bad_calibration_is_safe);
    RUN(test_classify_directions);
    RUN(test_classify_hysteresis);
    return TEST_REPORT();
}
