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
    lxa_budget_t budget, split;
    lxa_limits_t limits = lxa_limits_default();
    lxa_limit_reason_t reason = LXA_BUDGET_COUNT;

    REQUIRE(limits.max_input_bytes == 256u * 1024u * 1024u);
    REQUIRE(limits.max_memory_bytes == 256u * 1024u * 1024u);
    REQUIRE(limits.max_nodes == 2000000);
    REQUIRE(limits.max_tree_depth == 4096);
    REQUIRE(limits.max_attributes_per_element == 65535);
    REQUIRE(limits.max_token_bytes == 16u * 1024u * 1024u);
    REQUIRE(limits.max_name_bytes == 1024u * 1024u);
    REQUIRE(limits.initial_work_credit == 1000000
           && limits.work_per_input_byte == 128);

    CHECK(lxa_budget_init(NULL, &limits), LXA_ERROR_ARGUMENT);
    CHECK(lxa_budget_init(&budget, NULL), LXA_OK);
    REQUIRE(budget.work_credit == 1000000);
    CHECK(lxa_budget_accept_input(&budget, 3, &reason), LXA_OK);
    REQUIRE(budget.used[LXA_BUDGET_INPUT] == 3
           && budget.work_credit == 1000384
           && reason == LXA_BUDGET_COUNT);
    CHECK(lxa_budget_init(&split, NULL), LXA_OK);
    CHECK(lxa_budget_accept_input(&split, 1, NULL), LXA_OK);
    CHECK(lxa_budget_accept_input(&split, 2, NULL), LXA_OK);
    CHECK(lxa_budget_accept_input(&split, 0, NULL), LXA_OK);
    REQUIRE(split.work_credit == budget.work_credit);
    CHECK(lxa_budget_check(&budget, LXA_BUDGET_WORK, 1000384, &reason),
          LXA_OK);
    REQUIRE(budget.used[LXA_BUDGET_WORK] == 0);
    CHECK(lxa_budget_charge(&budget, LXA_BUDGET_WORK, 1000384, &reason),
          LXA_OK);
    CHECK(lxa_budget_charge(&budget, LXA_BUDGET_WORK, 1, &reason),
          LXA_ERROR_LIMIT);
    REQUIRE(reason == LXA_BUDGET_WORK
           && budget.used[LXA_BUDGET_WORK] == 1000384);
    CHECK(lxa_budget_release(&budget, LXA_BUDGET_WORK, 1),
          LXA_ERROR_ARGUMENT);

    limits.max_input_bytes = 3;
    limits.max_memory_bytes = 2;
    limits.max_string_bytes = 2;
    limits.max_css_nesting = 1;
    limits.max_work = UINT64_MAX;
    CHECK(lxa_budget_init(&budget, &limits), LXA_OK);
    CHECK(lxa_budget_accept_input(&budget, 3, &reason), LXA_OK);
    CHECK(lxa_budget_accept_input(&budget, 1, &reason), LXA_ERROR_LIMIT);
    REQUIRE(reason == LXA_BUDGET_INPUT
           && budget.used[LXA_BUDGET_INPUT] == 3
           && budget.work_credit == 1000384);
    CHECK(lxa_budget_charge(&budget, LXA_BUDGET_INPUT, 0, &reason),
          LXA_ERROR_ARGUMENT);
    CHECK(lxa_budget_charge(&budget, LXA_BUDGET_MEMORY, 2, &reason),
          LXA_OK);
    CHECK(lxa_budget_charge(&budget, LXA_BUDGET_MEMORY, 1, &reason),
          LXA_ERROR_LIMIT);
    REQUIRE(reason == LXA_BUDGET_MEMORY
           && budget.used[LXA_BUDGET_MEMORY] == 2);
    CHECK(lxa_budget_release(&budget, LXA_BUDGET_MEMORY, 2), LXA_OK);
    CHECK(lxa_budget_charge(&budget, LXA_BUDGET_MEMORY, 2, NULL), LXA_OK);
    CHECK(lxa_budget_charge(&budget, LXA_BUDGET_STRINGS, 2, NULL), LXA_OK);
    CHECK(lxa_budget_release(&budget, LXA_BUDGET_STRINGS, 2),
          LXA_ERROR_ARGUMENT);
    CHECK(lxa_budget_charge(&budget, LXA_BUDGET_CSS_NESTING, 1, NULL),
          LXA_OK);
    CHECK(lxa_budget_charge(&budget, LXA_BUDGET_CSS_NESTING, 1, &reason),
          LXA_ERROR_LIMIT);
    REQUIRE(reason == LXA_BUDGET_CSS_NESTING);
    CHECK(lxa_budget_release(&budget, LXA_BUDGET_CSS_NESTING, 1), LXA_OK);
    /* A new session cannot erase a retained buffer's memory charge. */
    lxa_budget_reset_session(&budget);
    REQUIRE(budget.used[LXA_BUDGET_MEMORY] == 2
           && budget.used[LXA_BUDGET_STRINGS] == 2
           && budget.used[LXA_BUDGET_INPUT] == 0
           && budget.used[LXA_BUDGET_WORK] == 0
           && budget.work_credit == limits.initial_work_credit);
    CHECK(lxa_budget_accept_input(&budget, 3, NULL), LXA_OK);
    CHECK(lxa_budget_charge(&budget, LXA_BUDGET_MEMORY, 1, &reason),
          LXA_ERROR_LIMIT);
    REQUIRE(reason == LXA_BUDGET_MEMORY);
    CHECK(lxa_budget_release(&budget, LXA_BUDGET_MEMORY, 2), LXA_OK);
    CHECK(lxa_budget_check_item(&budget, LXA_BUDGET_TOKEN_BYTES,
                                limits.max_token_bytes, NULL), LXA_OK);
    CHECK(lxa_budget_check_item(&budget, LXA_BUDGET_TOKEN_BYTES,
                                (uint64_t) limits.max_token_bytes + 1,
                                &reason), LXA_ERROR_LIMIT);
    REQUIRE(reason == LXA_BUDGET_TOKEN_BYTES);
    CHECK(lxa_budget_check_item(&budget, LXA_BUDGET_NAME_BYTES,
                                (uint64_t) limits.max_name_bytes + 1,
                                &reason), LXA_ERROR_LIMIT);
    REQUIRE(reason == LXA_BUDGET_NAME_BYTES);
    CHECK(lxa_budget_check_item(&budget, LXA_BUDGET_ATTRS_PER_ELEMENT,
                                65536, &reason), LXA_ERROR_LIMIT);
    REQUIRE(reason == LXA_BUDGET_ATTRS_PER_ELEMENT);
    CHECK(lxa_budget_check_item(&budget, LXA_BUDGET_MEMORY, 1, &reason),
          LXA_ERROR_ARGUMENT);
    CHECK(lxa_budget_charge(&budget, LXA_BUDGET_COUNT, 1, &reason),
          LXA_ERROR_ARGUMENT);
    REQUIRE(reason == LXA_BUDGET_COUNT);
    CHECK(lxa_budget_charge(NULL, LXA_BUDGET_MEMORY, 1, NULL),
          LXA_ERROR_ARGUMENT);

    limits.max_input_bytes = 1;
    budget.limits = limits;
    CHECK(lxa_budget_check(&budget, LXA_BUDGET_INPUT, 0, &reason),
          LXA_ERROR_LIMIT);
    REQUIRE(reason == LXA_BUDGET_INPUT);
    lxa_budget_reset(&budget);
    REQUIRE(budget.work_credit == limits.initial_work_credit);
    CHECK(lxa_budget_accept_input(&budget, 1, NULL), LXA_OK);

    limits = lxa_limits_unbounded();
    REQUIRE(limits.max_input_bytes == SIZE_MAX
           && limits.max_nodes == UINT32_MAX
           && limits.max_token_bytes == SIZE_MAX
           && limits.initial_work_credit == UINT64_MAX);
    CHECK(lxa_budget_init(&budget, &limits), LXA_OK);
    CHECK(lxa_budget_charge(&budget, LXA_BUDGET_WORK, UINT64_MAX, NULL),
          LXA_OK);
    CHECK(lxa_budget_charge(&budget, LXA_BUDGET_WORK, 1, &reason),
          LXA_ERROR_OVERFLOW);
    REQUIRE(reason == LXA_BUDGET_COUNT);
    return 0;
}
