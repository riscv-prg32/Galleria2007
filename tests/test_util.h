/* Minimal host test harness: no external framework required. */
#ifndef G2007_TEST_UTIL_H
#define G2007_TEST_UTIL_H
#include <stdio.h>
#include <stdlib.h>

static int g_checks, g_failures;
#define CHECK(cond)                                                        \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            ++g_failures;                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                  \
    } while (0)
#define CHECK_EQ(a, b)                                                     \
    do {                                                                   \
        long long va_ = (long long)(a), vb_ = (long long)(b);              \
        ++g_checks;                                                        \
        if (va_ != vb_) {                                                  \
            ++g_failures;                                                  \
            fprintf(stderr, "%s:%d: %s == %s failed (%lld vs %lld)\n",     \
                    __FILE__, __LINE__, #a, #b, va_, vb_);                 \
        }                                                                  \
    } while (0)
#define TEST_MAIN_END(name)                                                \
    printf("%s: %d checks, %d failures\n", name, g_checks, g_failures);   \
    return g_failures ? 1 : 0;
#endif
