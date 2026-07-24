/* Copyright (C) 2026 Paul Moses */
#include <string.h>
#include "liblexa/base.h"

#if defined(__linux__)
#include <errno.h>
#include <sys/random.h>
#elif defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) \
   || defined(__NetBSD__)
#include <stdlib.h>
#endif

lxa_status_t
lxa_entropy_key(uint8_t key[16], lxa_entropy_callback_t callback, void *context)
{
    lxa_status_t status = LXA_ERROR_ENTROPY;
    uint8_t generated[16];
    if (key == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    memset(key, 0, 16);
    if (callback != NULL) {
        status = callback(context, generated);
    }
    else {
#if defined(__linux__)
        size_t received = 0;
        while (received < sizeof(generated)) {
            ssize_t count = getrandom(generated + received,
                                      sizeof(generated) - received, 0);
            if (count > 0) {
                received += (size_t) count;
            }
            else if (count != -1 || errno != EINTR) {
                break;
            }
        }
        if (received == sizeof(generated)) {
            status = LXA_OK;
        }
#elif defined(_WIN32)
        if (BCryptGenRandom(NULL, generated, sizeof(generated),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0) {
            status = LXA_OK;
        }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) \
   || defined(__NetBSD__)
        arc4random_buf(generated, sizeof(generated));
        status = LXA_OK;
#endif
    }
    if (status == LXA_OK) {
        memcpy(key, generated, sizeof(generated));
    }
    else {
        memset(generated, 0, sizeof(generated));
    }
    return status;
}
