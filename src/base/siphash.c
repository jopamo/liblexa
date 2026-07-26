#include "liblexa/base.h"

static uint64_t
rotate(uint64_t value, unsigned shift)
{
    return (value << shift) | (value >> (64 - shift));
}

static uint64_t
load64(const uint8_t *p)
{
    uint64_t result = 0;
    unsigned i;
    for (i = 0; i < 8; ++i) {
        result |= (uint64_t) p[i] << (8 * i);
    }
    return result;
}

static void
round24(uint64_t v[4])
{
    v[0] += v[1];
    v[1] = rotate(v[1], 13);
    v[1] ^= v[0];
    v[0] = rotate(v[0], 32);
    v[2] += v[3];
    v[3] = rotate(v[3], 16);
    v[3] ^= v[2];
    v[0] += v[3];
    v[3] = rotate(v[3], 21);
    v[3] ^= v[0];
    v[2] += v[1];
    v[1] = rotate(v[1], 17);
    v[1] ^= v[2];
    v[2] = rotate(v[2], 32);
}

lxa_status_t
lxa_siphash24(const uint8_t key[16], const uint8_t *data,
              size_t length, uint64_t *out)
{
    uint64_t v[4], k0, k1, block;
    size_t remaining = length;
    unsigned i, j;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = 0;
    if (key == NULL || (data == NULL && length != 0)) {
        return LXA_ERROR_ARGUMENT;
    }
    k0 = load64(key);
    k1 = load64(key + 8);
    v[0] = UINT64_C(0x736f6d6570736575) ^ k0;
    v[1] = UINT64_C(0x646f72616e646f6d) ^ k1;
    v[2] = UINT64_C(0x6c7967656e657261) ^ k0;
    v[3] = UINT64_C(0x7465646279746573) ^ k1;
    while (remaining >= 8) {
        block = load64(data);
        v[3] ^= block;
        for (j = 0; j < 2; ++j) {
            round24(v);
        }
        v[0] ^= block;
        data += 8;
        remaining -= 8;
    }
    block = (uint64_t) length << 56;
    for (i = 0; i < remaining; ++i) {
        block |= (uint64_t) data[i] << (8 * i);
    }
    v[3] ^= block;
    for (j = 0; j < 2; ++j) {
        round24(v);
    }
    v[0] ^= block;
    v[2] ^= 0xff;
    for (j = 0; j < 4; ++j) {
        round24(v);
    }
    *out = v[0] ^ v[1] ^ v[2] ^ v[3];
    return LXA_OK;
}
