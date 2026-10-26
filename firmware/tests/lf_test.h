/* A minimal test runner: CHECK records a failure, main returns the count. */
#ifndef LF_TEST_H
#define LF_TEST_H
#include <stdio.h>

static int lf_failures;
#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
            lf_failures++;                                                    \
        }                                                                     \
    } while (0)
#define LF_DONE(name)                                                         \
    do {                                                                      \
        printf("%s: %s\n", name, lf_failures ? "FAILED" : "ok");              \
        return lf_failures ? 1 : 0;                                           \
    } while (0)
#endif
