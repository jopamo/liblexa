#ifndef LIBLEXA_CSS_TEST_CHECK_H
#define LIBLEXA_CSS_TEST_CHECK_H

#include <stdio.h>
#include <stdlib.h>

/* Evaluate setup and checks in every build, including NDEBUG builds.
 * assert() must never own state transitions or test setup.
 */
#define CSS_CHECK(expression) do {                                      \
    if (!(expression)) {                                                 \
        fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__,       \
                #expression);                                            \
        abort();                                                          \
    }                                                                    \
} while (0)

#endif /* LIBLEXA_CSS_TEST_CHECK_H */
