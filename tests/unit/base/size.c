#include <stdint.h>
#include <stdlib.h>
#include "liblexa/base.h"

#define REQUIRE(condition) do { if (!(condition)) abort(); } while (0)

#define CHECK(call, expected) do {         \
    lxa_status_t status = (call);           \
    REQUIRE(status == (expected));           \
    (void) status;                           \
} while (0)

int
main(void)
{
    size_t result = 17;
    uint32_t narrowed = 17;
    lxa_id_t slot = 17;
    uint32_t generation = 17;
    lxa_handle_t handle = 17;

    CHECK(lxa_size_add(0, 0, &result), LXA_OK);
    REQUIRE(result == 0);
    CHECK(lxa_size_add(SIZE_MAX - 1, 1, &result), LXA_OK);
    REQUIRE(result == SIZE_MAX);
    CHECK(lxa_size_add(SIZE_MAX, 1, &result), LXA_ERROR_OVERFLOW);
    REQUIRE(result == 0);
    CHECK(lxa_size_add(1, 1, NULL), LXA_ERROR_ARGUMENT);

    CHECK(lxa_size_mul(SIZE_MAX, 0, &result), LXA_OK);
    REQUIRE(result == 0);
    CHECK(lxa_size_mul(SIZE_MAX, 1, &result), LXA_OK);
    REQUIRE(result == SIZE_MAX);
    CHECK(lxa_size_mul(SIZE_MAX, 2, &result), LXA_ERROR_OVERFLOW);
    REQUIRE(result == 0);
    CHECK(lxa_size_mul(1, 1, NULL), LXA_ERROR_ARGUMENT);

    CHECK(lxa_size_to_u32(0, &narrowed), LXA_OK);
    REQUIRE(narrowed == 0);
    CHECK(lxa_size_to_u32(UINT32_MAX, &narrowed), LXA_OK);
    REQUIRE(narrowed == UINT32_MAX);
    CHECK(lxa_size_to_id(UINT32_MAX - 1, &narrowed), LXA_OK);
    REQUIRE(narrowed == UINT32_MAX - 1);
    CHECK(lxa_size_to_id(UINT32_MAX, &narrowed), LXA_ERROR_OVERFLOW);
    REQUIRE(narrowed == 0);
    if (SIZE_MAX > UINT32_MAX) {
        size_t above = (size_t) UINT32_MAX;
        ++above;
        CHECK(lxa_size_to_u32(above, &narrowed), LXA_ERROR_OVERFLOW);
        REQUIRE(narrowed == 0);
        CHECK(lxa_size_to_id(above, &narrowed), LXA_ERROR_OVERFLOW);
        REQUIRE(narrowed == 0);
    }
    CHECK(lxa_size_to_u32(1, NULL), LXA_ERROR_ARGUMENT);
    CHECK(lxa_size_to_id(1, NULL), LXA_ERROR_ARGUMENT);

    CHECK(lxa_handle_pack(0, 1, &handle), LXA_OK);
    REQUIRE(handle == UINT64_C(0x0000000100000000));
    CHECK(lxa_handle_unpack(handle, &slot, &generation), LXA_OK);
    REQUIRE(slot == 0 && generation == 1);
    CHECK(lxa_handle_pack(UINT32_MAX - 1, UINT32_MAX, &handle), LXA_OK);
    CHECK(lxa_handle_unpack(handle, &slot, &generation), LXA_OK);
    REQUIRE(slot == UINT32_MAX - 1 && generation == UINT32_MAX);
    CHECK(lxa_handle_pack(LXA_ID_NONE, 1, &handle), LXA_ERROR_ARGUMENT);
    REQUIRE(handle == LXA_HANDLE_NONE);
    CHECK(lxa_handle_pack(1, 0, &handle), LXA_ERROR_ARGUMENT);
    REQUIRE(handle == LXA_HANDLE_NONE);
    CHECK(lxa_handle_pack(1, 1, NULL), LXA_ERROR_ARGUMENT);
    CHECK(lxa_handle_unpack(LXA_HANDLE_NONE, &slot, &generation),
          LXA_ERROR_ARGUMENT);
    REQUIRE(slot == 0 && generation == 0);
    CHECK(lxa_handle_unpack(UINT64_C(0x00000001ffffffff),
                            &slot, &generation), LXA_ERROR_ARGUMENT);
    REQUIRE(slot == 0 && generation == 0);
    CHECK(lxa_handle_unpack(UINT64_C(0x0000000000000001),
                            &slot, &generation), LXA_ERROR_ARGUMENT);
    CHECK(lxa_handle_unpack(1, NULL, &generation), LXA_ERROR_ARGUMENT);
    CHECK(lxa_handle_unpack(1, &slot, NULL), LXA_ERROR_ARGUMENT);

    CHECK(lxa_size_align(0, 1, &result), LXA_OK);
    REQUIRE(result == 0);
    CHECK(lxa_size_align(9, 8, &result), LXA_OK);
    REQUIRE(result == 16);
    CHECK(lxa_size_align(SIZE_MAX - 7, 8, &result), LXA_OK);
    REQUIRE(result == SIZE_MAX - 7);
    CHECK(lxa_size_align(SIZE_MAX, 8, &result), LXA_ERROR_OVERFLOW);
    REQUIRE(result == 0);
    CHECK(lxa_size_align(1, 0, &result), LXA_ERROR_ARGUMENT);
    REQUIRE(result == 0);
    CHECK(lxa_size_align(1, 3, &result), LXA_ERROR_ARGUMENT);
    REQUIRE(result == 0);
    CHECK(lxa_size_align(1, 2, NULL), LXA_ERROR_ARGUMENT);
    return 0;
}
