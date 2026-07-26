#include <stdint.h>
#include <stdlib.h>
#include "liblexa/base.h"

#define REQUIRE(condition) do { if (!(condition)) abort(); } while (0)

/* SipHash-2-4 published reference vectors for key 00..0f and input 00..n-1. */
static const uint64_t vectors[] = {
    UINT64_C(0x726fdb47dd0e0e31), UINT64_C(0x74f839c593dc67fd),
    UINT64_C(0x0d6c8009d9a94f5a), UINT64_C(0x85676696d7fb7e2d),
    UINT64_C(0xcf2794e0277187b7), UINT64_C(0x18765564cd99a68d),
    UINT64_C(0xcbc9466e58fee3ce), UINT64_C(0xab0200f58b01d137),
    UINT64_C(0x93f5f5799a932462), UINT64_C(0x9e0082df0ba9e4b0),
    UINT64_C(0x7a5dbbc594ddb9f3), UINT64_C(0xf4b32f46226bada7),
    UINT64_C(0x751e8fbc860ee5fb), UINT64_C(0x14ea5627c0843d90),
    UINT64_C(0xf723ca908e7af2ee), UINT64_C(0xa129ca6149be45e5),
    UINT64_C(0x3f2acc7f57c29bdb), UINT64_C(0x699ae9f52cbe4794)
};

int
main(void)
{
    uint8_t key[16], input[sizeof(vectors) / sizeof(vectors[0])];
    uint64_t output = 7, other;
    size_t i;
    for (i = 0; i < 16; ++i) {
        key[i] = (uint8_t) i;
    }
    for (i = 0; i < sizeof(input); ++i) {
        input[i] = (uint8_t) i;
    }
    REQUIRE(lxa_siphash24(NULL, input, 1, &output) == LXA_ERROR_ARGUMENT);
    REQUIRE(output == 0);
    REQUIRE(lxa_siphash24(key, NULL, 1, &output) == LXA_ERROR_ARGUMENT);
    REQUIRE(output == 0);
    REQUIRE(lxa_siphash24(key, input, 1, NULL) == LXA_ERROR_ARGUMENT);
    for (i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
        REQUIRE(lxa_siphash24(key, input, i, &output) == LXA_OK);
        REQUIRE(output == vectors[i]);
    }
    REQUIRE(lxa_siphash24(key, NULL, 0, &output) == LXA_OK);
    REQUIRE(output == vectors[0]);
    key[0]++;
    REQUIRE(lxa_siphash24(key, input, 7, &other) == LXA_OK);
    REQUIRE(other != output && other != vectors[7]);
    return 0;
}
