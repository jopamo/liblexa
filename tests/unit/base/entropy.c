/* Copyright (C) 2026 Paul Moses */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "liblexa/base.h"

#define REQUIRE(condition) do { if (!(condition)) abort(); } while (0)

static lxa_status_t
key_from_caller(void *context, uint8_t key[16])
{
    size_t i;
    const uint8_t value = *(const uint8_t *) context;
    for (i = 0; i < 16; ++i) {
        key[i] = (uint8_t) (value + i);
    }
    return LXA_OK;
}

static lxa_status_t
failed_caller(void *context, uint8_t key[16])
{
    (void) context;
    key[0] = 19;
    return LXA_ERROR_ENTROPY;
}

int
main(void)
{
    uint8_t key[16], expected[16] = {0};
    uint8_t start = 37;
    size_t i;
    REQUIRE(lxa_entropy_key(NULL, NULL, NULL) == LXA_ERROR_ARGUMENT);
    memset(key, 0xff, sizeof(key));
    REQUIRE(lxa_entropy_key(key, failed_caller, NULL) == LXA_ERROR_ENTROPY);
    REQUIRE(memcmp(key, expected, sizeof(key)) == 0);
    REQUIRE(lxa_entropy_key(key, key_from_caller, &start) == LXA_OK);
    for (i = 0; i < sizeof(key); ++i) {
        REQUIRE(key[i] == (uint8_t) (start + i));
    }
#if defined(__linux__) || defined(_WIN32) || defined(__APPLE__) \
    || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    REQUIRE(lxa_entropy_key(key, NULL, NULL) == LXA_OK);
#else
    REQUIRE(lxa_entropy_key(key, NULL, NULL) == LXA_ERROR_ENTROPY);
    REQUIRE(memcmp(key, expected, sizeof(key)) == 0);
#endif
    return 0;
}
