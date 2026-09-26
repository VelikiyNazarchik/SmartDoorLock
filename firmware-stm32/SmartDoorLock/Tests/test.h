#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failures;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        g_checks++;                                                                   \
        if (!(cond)) {                                                                \
            g_failures++;                                                             \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                  \
        }                                                                             \
    } while (0)

#define CHECK_EQ(a, b)                                                                \
    do {                                                                              \
        long long va_ = (long long)(a), vb_ = (long long)(b);                         \
        g_checks++;                                                                   \
        if (va_ != vb_) {                                                             \
            g_failures++;                                                             \
            printf("  FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__,     \
                   #a, #b, va_, vb_);                                                 \
        }                                                                             \
    } while (0)

#define RUN(test)                                                                     \
    do {                                                                              \
        int before_ = g_failures;                                                     \
        test();                                                                       \
        printf("%s %s\n", (g_failures == before_) ? "  ok  " : "  FAIL", #test);      \
    } while (0)

#define TEST_REPORT()                                                                 \
    (printf("%d checks, %d failures\n", g_checks, g_failures), g_failures ? 1 : 0)

#endif
