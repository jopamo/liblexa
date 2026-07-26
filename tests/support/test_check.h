#ifndef LIBLEXA_DOM_TEST_CHECK_H
#define LIBLEXA_DOM_TEST_CHECK_H

/* Test-only check: always evaluates the expression, including in Release
 * builds, and reports the failed condition before stopping.
 */
#include <stdio.h>
#include <stdlib.h>

static inline void
lxa_dom_test_fail(const char *file, int line, const char *expression)
{
    fprintf(stderr, "%s:%d: failed: %s\n", file, line, expression);
    abort();
}

#define TEST_CHECK(expression) do { \
    if (!(expression)) { \
        lxa_dom_test_fail(__FILE__, __LINE__, #expression); \
    } \
} while (0)

#endif
