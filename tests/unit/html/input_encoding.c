/* Copyright (C) 2026 Paul Moses */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "liblexa/html.h"

typedef struct {
    size_t calls, fail_at, live;
} fault_t;

static void *
fail_allocate(void *context, size_t size)
{
    fault_t *fault = context;
    void *ptr;
    if (fault->calls++ == fault->fail_at) {
        return NULL;
    }
    ptr = malloc(size);
    if (ptr != NULL) {
        fault->live++;
    }
    return ptr;
}

static void *
fail_resize(void *context, void *old, size_t size)
{
    fault_t *fault = context;
    void *ptr;
    if (fault->calls++ == fault->fail_at) {
        return NULL;
    }
    ptr = realloc(old, size);
    if (ptr != NULL && old == NULL) {
        fault->live++;
    }
    return ptr;
}

static void
fail_release(void *context, void *ptr)
{
    fault_t *fault = context;
    fault->live--;
    free(ptr);
}

typedef struct {
    size_t offsets[4], count, fail_at;
} diagnostics_t;

static lxa_status_t
diagnostic(void *context, size_t offset)
{
    diagnostics_t *state = context;
    if (state->count == state->fail_at) {
        return LXA_ERROR_NO_MEMORY;
    }
    if (state->count >= 4) {
        return LXA_ERROR_ARGUMENT;
    }
    state->offsets[state->count++] = offset;
    return LXA_OK;
}

typedef struct {
    size_t previous, length, count;
} diagnostic_sweep_t;

static lxa_status_t
sweep_diagnostic(void *context, size_t offset)
{
    diagnostic_sweep_t *state = context;
    if (offset >= state->length
        || (state->count != 0 && offset < state->previous)) {
        return LXA_ERROR_ARGUMENT;
    }
    state->previous = offset;
    state->count++;
    return LXA_OK;
}

static int
bx_input_boundary_tests(void)
{
    const size_t limit = 16u * 1024u * 1024u;
    uint8_t *page = malloc(limit + 1);
    const lxa_encoding_t *encoding = NULL;
    lxa_span_t input;
    size_t skip = 0;
    int result = 0;
    if (page == NULL) return 1;
    page[0] = 0xef;
    page[1] = 0xbb;
    page[2] = 0xbf;
    input = (lxa_span_t) {page, limit};
    if (lxa_html_input_select(input, limit, &encoding, &skip)
        != LXA_HTML_INPUT_OK
        || encoding != lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "utf-8", 5
        }) || skip != 3) {
        result = 1;
    }
    input.length++;
    if (lxa_html_input_select(input, limit, &encoding, &skip)
        != LXA_HTML_INPUT_LIMIT || encoding != NULL || skip != 0) {
        result = 1;
    }
    free(page);
    return result;
}

static int
diagnostics_tests(void)
{
    static const uint8_t malformed_utf8[] = {'a', 0xe2, 0xff, 0xe2};
    static const uint8_t malformed_utf16[] =
        {0xff, 0xfe, 0x00, 0xd8, 0x00, 0xd8, 0x00, 0xd8};
    static const uint8_t malformed_euc_jp[] =
        "<meta charset=euc-jp>\x8f\xa2Z";
    const lxa_span_t inputs[] = {
        {malformed_utf8, sizeof(malformed_utf8)},
        {malformed_utf16, sizeof(malformed_utf16)}
    };
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_budget_t budget;
    lxa_limits_t limits;
    lxa_buffer_t output;
    lxa_limit_reason_t reason;
    lxa_status_t callback_status;
    diagnostics_t state;
    const lxa_encoding_t *encoding;
    size_t skip, malformed, n;
    for (n = 0; n < 2; n++) {
        state = (diagnostics_t) {{0}, 0, 4};
        if (lxa_budget_init(&budget, NULL) != LXA_OK
            || lxa_buffer_init(&output, &allocator) != LXA_OK
            || lxa_buffer_bind_budget(&output, &budget) != LXA_OK
            || lxa_html_input_select(inputs[n], inputs[n].length,
                                     &encoding, &skip) != LXA_HTML_INPUT_OK
            || lxa_html_input_decode_report(
                inputs[n], encoding, skip, &budget, &output, &malformed,
                &reason, diagnostic, &state, &callback_status)
               != LXA_HTML_INPUT_OK
            || callback_status != LXA_OK || reason != LXA_BUDGET_COUNT
            || state.count != 3 || malformed != 3
            || state.offsets[0] != (n == 0 ? 1u : 2u)
            || state.offsets[1] != (n == 0 ? 2u : 4u)
            || state.offsets[2] != (n == 0 ? 3u : 6u)) {
            return 1;
        }
        lxa_buffer_destroy(&output);
        if (budget.used[LXA_BUDGET_MEMORY] != 0) {
            return 1;
        }
    }
    state = (diagnostics_t) {{0}, 0, 4};
    {
        lxa_span_t legacy = {malformed_euc_jp, sizeof(malformed_euc_jp) - 1};
        if (lxa_budget_init(&budget, NULL) != LXA_OK
            || lxa_buffer_init(&output, &allocator) != LXA_OK
            || lxa_buffer_bind_budget(&output, &budget) != LXA_OK
            || lxa_html_input_select(legacy, legacy.length, &encoding, &skip)
               != LXA_HTML_INPUT_OK
            || lxa_html_input_decode_report(
                legacy, encoding, skip, &budget, &output, &malformed,
                &reason, diagnostic, &state, &callback_status)
               != LXA_HTML_INPUT_OK
            || malformed != 1 || state.count != 1
            || state.offsets[0] != legacy.length - 3
            || reason != LXA_BUDGET_COUNT || callback_status != LXA_OK) {
            return 1;
        }
        lxa_buffer_destroy(&output);
    }
    state = (diagnostics_t) {{0}, 0, 1};
    if (lxa_budget_init(&budget, NULL) != LXA_OK
        || lxa_buffer_init(&output, &allocator) != LXA_OK
        || lxa_buffer_bind_budget(&output, &budget) != LXA_OK
        || lxa_html_input_select(inputs[0], inputs[0].length,
                                 &encoding, &skip) != LXA_HTML_INPUT_OK
        || lxa_html_input_decode_report(
            inputs[0], encoding, skip, &budget, &output, &malformed,
            &reason, diagnostic, &state, &callback_status)
           != LXA_HTML_INPUT_CALLBACK_FAILURE
        || callback_status != LXA_ERROR_NO_MEMORY || state.count != 1
        || output.data != NULL || malformed != 0 || reason != LXA_BUDGET_COUNT
        || budget.used[LXA_BUDGET_MEMORY] != 0) {
        return 1;
    }
    lxa_buffer_destroy(&output);
    {
        size_t attempt;
        bool reached_success = false;
        for (attempt = 0; attempt < 8; attempt++) {
            fault_t fault = {0, attempt, 0};
            lxa_allocator_t failing = {&fault, fail_allocate, fail_resize,
                                       fail_release};
            lxa_html_input_result_t result;
            state = (diagnostics_t) {{0}, 0, 4};
            if (lxa_budget_init(&budget, NULL) != LXA_OK
                || lxa_buffer_init(&output, &failing) != LXA_OK
                || lxa_buffer_bind_budget(&output, &budget) != LXA_OK
                || lxa_html_input_select(inputs[0], inputs[0].length,
                                         &encoding, &skip) != LXA_HTML_INPUT_OK) {
                return 1;
            }
            result = lxa_html_input_decode_report(
                inputs[0], encoding, skip, &budget, &output, &malformed,
                &reason, diagnostic, &state, &callback_status);
            if ((result != LXA_HTML_INPUT_OK
                 && (result != LXA_HTML_INPUT_NO_MEMORY
                     || output.data != NULL || malformed != 0))
                || reason != LXA_BUDGET_COUNT || callback_status != LXA_OK) {
                return 1;
            }
            lxa_buffer_destroy(&output);
            if (fault.live != 0 || budget.used[LXA_BUDGET_MEMORY] != 0) {
                return 1;
            }
            if (result == LXA_HTML_INPUT_OK) {
                reached_success = true;
                break;
            }
        }
        if (!reached_success || attempt < 2) {
            return 1;
        }
    }
    limits = lxa_limits_default();
    limits.initial_work_credit = 8;
    limits.work_per_input_byte = 0;
    state = (diagnostics_t) {{0}, 0, 4};
    if (lxa_budget_init(&budget, &limits) != LXA_OK
        || lxa_buffer_init(&output, &allocator) != LXA_OK
        || lxa_buffer_bind_budget(&output, &budget) != LXA_OK
        || lxa_html_input_decode_report(
            inputs[0], encoding, 0, &budget, &output, &malformed,
            &reason, diagnostic, &state, &callback_status)
           != LXA_HTML_INPUT_LIMIT
        || reason != LXA_BUDGET_WORK || state.count != 0
        || output.data != NULL || malformed != 0
        || budget.used[LXA_BUDGET_MEMORY] != 0) {
        return 1;
    }
    lxa_buffer_destroy(&output);
    {
        uint32_t seed = 0x9182745u;
        uint8_t raw[60];
        size_t attempt, codec;
        const char *names[] = {"utf-8", "utf-16le", "utf-16be",
                               "euc-jp", "euc-kr", "windows-1252"};
        for (attempt = 0; attempt < 128 * 6; attempt++) {
            size_t length, i;
            diagnostic_sweep_t sweep = {0};
            codec = attempt % 6;
            encoding = lxa_encoding_lookup((lxa_span_t) {
                (const uint8_t *) names[codec], strlen(names[codec])
            });
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            length = seed % (sizeof(raw) + 1);
            for (i = 0; i < length; i++) {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                raw[i] = (uint8_t) seed;
            }
            sweep.length = length;
            if (lxa_budget_init(&budget, NULL) != LXA_OK
                || lxa_buffer_init(&output, &allocator) != LXA_OK
                || lxa_buffer_bind_budget(&output, &budget) != LXA_OK
                || lxa_html_input_decode_report(
                    (lxa_span_t) {raw, length}, encoding, 0, &budget,
                    &output, &malformed, &reason, sweep_diagnostic, &sweep,
                    &callback_status) != LXA_HTML_INPUT_OK
                || sweep.count != malformed || callback_status != LXA_OK
                || reason != LXA_BUDGET_COUNT) {
                return 1;
            }
            lxa_buffer_destroy(&output);
            if (budget.used[LXA_BUDGET_MEMORY] != 0) {
                return 1;
            }
        }
    }
    return 0;
}

static int
decode_tests(void)
{
    static const uint8_t cp1252[] =
        "<meta charset=windows-1252><a href='\x80\x81'>";
    static const uint8_t malformed[] = "a\xe2\x82";
    static const uint8_t bom[] = {0xff, 0xfe, 'x', 0, 0x3d, 0xd8, 0, 0xde};
    const lxa_encoding_t *encoding;
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_limits_t limits = lxa_limits_default();
    lxa_budget_t budget;
    lxa_buffer_t output;
    size_t skip, errors;
    lxa_limit_reason_t reason;
    lxa_span_t input = {cp1252, sizeof(cp1252) - 1};
    limits.max_input_bytes = input.length;
    if (lxa_budget_init(&budget, &limits) != LXA_OK
        || lxa_buffer_init(&output, &allocator) != LXA_OK
        || lxa_buffer_bind_budget(&output, &budget) != LXA_OK
        || lxa_html_input_select(input, limits.max_input_bytes,
                                  &encoding, &skip) != LXA_HTML_INPUT_OK
        || lxa_html_input_decode(input, encoding, skip, &budget,
                                  &output, &errors, &reason) != LXA_HTML_INPUT_OK
        || errors != 0 || output.length != input.length + 3
        || reason != LXA_BUDGET_COUNT
        || memcmp(output.data + input.length - 4,
                  "\xe2\x82\xac\xc2\x81'>", 7) != 0
        || budget.used[LXA_BUDGET_INPUT] != input.length) {
        return 1;
    }
    lxa_buffer_destroy(&output);
    input = (lxa_span_t) {malformed, sizeof(malformed) - 1};
    if (lxa_budget_init(&budget, NULL) != LXA_OK
        || lxa_buffer_init(&output, &allocator) != LXA_OK
        || lxa_buffer_bind_budget(&output, &budget) != LXA_OK
        || lxa_html_input_select(input, input.length, &encoding, &skip)
           != LXA_HTML_INPUT_OK
        || lxa_html_input_decode(input, encoding, skip, &budget,
                                  &output, &errors, &reason) != LXA_HTML_INPUT_OK
        || output.length != 4 || memcmp(output.data, "a\xef\xbf\xbd", 4)
           || errors != 1) {
        return 1;
    }
    lxa_buffer_destroy(&output);
    input = (lxa_span_t) {bom, sizeof(bom)};
    if (lxa_budget_init(&budget, NULL) != LXA_OK
        || lxa_buffer_init(&output, &allocator) != LXA_OK
        || lxa_buffer_bind_budget(&output, &budget) != LXA_OK
        || lxa_html_input_select(input, input.length, &encoding, &skip)
           != LXA_HTML_INPUT_OK
        || lxa_html_input_decode(input, encoding, skip, &budget,
                                  &output, &errors, &reason) != LXA_HTML_INPUT_OK
        || output.length != 5 || memcmp(output.data, "x\xf0\x9f\x98\x80", 5)
           || errors != 0) {
        return 1;
    }
    lxa_buffer_destroy(&output);
    input = (lxa_span_t) {(const uint8_t *) "\xff", 1};
    if (lxa_budget_init(&budget, NULL) != LXA_OK
        || lxa_buffer_init(&output, &allocator) != LXA_OK
        || lxa_buffer_bind_budget(&output, &budget) != LXA_OK
        || lxa_html_input_select(input, input.length, &encoding, &skip)
           != LXA_HTML_INPUT_OK
        || lxa_html_input_decode(input, encoding, skip, &budget,
                                  &output, &errors, &reason) != LXA_HTML_INPUT_OK
        || errors != 1 || output.length != 3
        || memcmp(output.data, "\xef\xbf\xbd", 3) != 0) {
        return 1;
    }
    lxa_buffer_destroy(&output);
    input = (lxa_span_t) {bom, sizeof(bom)};
    if (lxa_html_input_select(input, input.length, &encoding, &skip)
        != LXA_HTML_INPUT_OK) {
        return 1;
    }
    limits = lxa_limits_default();
    limits.max_input_bytes = 2;
    if (lxa_budget_init(&budget, &limits) != LXA_OK
        || lxa_buffer_init(&output, &allocator) != LXA_OK
        || lxa_buffer_bind_budget(&output, &budget) != LXA_OK
        || lxa_html_input_decode(input, encoding, skip, &budget,
                                  &output, &errors, &reason) != LXA_HTML_INPUT_LIMIT
        || output.data != NULL || errors != 0
        || reason != LXA_BUDGET_INPUT
        || budget.used[LXA_BUDGET_INPUT] != 0) {
        return 1;
    }
    lxa_buffer_destroy(&output);
    limits = lxa_limits_default();
    limits.max_memory_bytes = 1;
    if (lxa_budget_init(&budget, &limits) != LXA_OK
        || lxa_buffer_init(&output, &allocator) != LXA_OK
        || lxa_buffer_bind_budget(&output, &budget) != LXA_OK
        || lxa_html_input_decode(input, encoding, skip, &budget,
                                  &output, &errors, &reason) != LXA_HTML_INPUT_LIMIT
        || output.data != NULL || errors != 0
        || reason != LXA_BUDGET_MEMORY
        || budget.used[LXA_BUDGET_MEMORY] != 0) {
        return 1;
    }
    lxa_buffer_destroy(&output);
    limits = lxa_limits_default();
    limits.initial_work_credit = 1;
    limits.work_per_input_byte = 0;
    if (lxa_budget_init(&budget, &limits) != LXA_OK
        || lxa_buffer_init(&output, &allocator) != LXA_OK
        || lxa_buffer_bind_budget(&output, &budget) != LXA_OK
        || lxa_html_input_decode(input, encoding, skip, &budget,
                                  &output, &errors, &reason) != LXA_HTML_INPUT_LIMIT
        || output.data != NULL || errors != 0
        || reason != LXA_BUDGET_WORK
        || budget.used[LXA_BUDGET_MEMORY] != 0) {
        return 1;
    }
    lxa_buffer_destroy(&output);
    {
        size_t n;
        bool success = false;
        for (n = 0; n < 8; n++) {
            fault_t fault = {0, n, 0};
            lxa_allocator_t failing = {&fault, fail_allocate, fail_resize,
                                       fail_release};
            lxa_html_input_result_t result;
            if (lxa_budget_init(&budget, NULL) != LXA_OK
                || lxa_buffer_init(&output, &failing) != LXA_OK
                || lxa_buffer_bind_budget(&output, &budget) != LXA_OK) {
                return 1;
            }
            result = lxa_html_input_decode(input, encoding, skip, &budget,
                                            &output, &errors, &reason);
            if (result != LXA_HTML_INPUT_NO_MEMORY
                && result != LXA_HTML_INPUT_OK) {
                return 1;
            }
            if (reason != LXA_BUDGET_COUNT) {
                return 1;
            }
            if ((result == LXA_HTML_INPUT_NO_MEMORY
                 && (output.data != NULL || errors != 0))
                || (result == LXA_HTML_INPUT_OK && output.length != 5)) {
                return 1;
            }
            lxa_buffer_destroy(&output);
            if (fault.live != 0 || budget.used[LXA_BUDGET_MEMORY] != 0) {
                return 1;
            }
            if (result == LXA_HTML_INPUT_OK) {
                success = true;
                break;
            }
        }
        if (!success || n < 2) {
            return 1;
        }
    }
    return 0;
}

static int
late_declaration_tests(void)
{
    static const uint8_t bytes[] = {'<', 'p', '>', 0x80};
    static const uint8_t two_errors[] = {'a', 0xff, 'b', 0xff};
    const lxa_span_t raw = {bytes, sizeof(bytes)};
    const lxa_encoding_t *encoding;
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_limits_t limits = lxa_limits_default();
    lxa_budget_t budget;
    lxa_buffer_t first, retry;
    lxa_limit_reason_t reason;
    lxa_status_t callback_status;
    diagnostics_t state = {{0}, 0, 4};
    size_t malformed;
    uint64_t credit;
    if (lxa_html_input_resolve_label(
            (lxa_span_t) {(const uint8_t *) "UTF-16LE", 8},
            &encoding) != LXA_HTML_INPUT_OK
        || encoding != lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "utf-8", 5
        })
        || lxa_html_input_resolve_label(
            (lxa_span_t) {(const uint8_t *) "x-user-defined", 14},
            &encoding) != LXA_HTML_INPUT_OK
        || encoding != lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "windows-1252", 12
        })
        || lxa_html_input_resolve_label(
            (lxa_span_t) {(const uint8_t *) "not-a-codec", 11},
            &encoding) != LXA_HTML_INPUT_UNKNOWN_ENCODING
        || encoding != NULL) {
        return 1;
    }
    limits.max_input_bytes = sizeof(bytes);
    limits.initial_work_credit = 0;
    limits.work_per_input_byte = 4;
    encoding = lxa_encoding_lookup((lxa_span_t) {
        (const uint8_t *) "utf-8", 5
    });
    if (lxa_budget_init(&budget, &limits) != LXA_OK
        || lxa_buffer_init(&first, &allocator) != LXA_OK
        || lxa_buffer_bind_budget(&first, &budget) != LXA_OK
        || lxa_buffer_init(&retry, &allocator) != LXA_OK
        || lxa_buffer_bind_budget(&retry, &budget) != LXA_OK
        || lxa_html_input_decode_prepaid_report(raw, encoding, 0,
            &budget, &retry, &malformed, &reason, NULL, NULL, NULL)
           != LXA_HTML_INPUT_BAD_ARGUMENT
        || budget.used[LXA_BUDGET_INPUT] != 0) {
        return 1;
    }
    if (lxa_html_input_decode(raw, encoding, 0, &budget, &first,
                              &malformed, &reason) != LXA_HTML_INPUT_OK
        || malformed != 1
        || budget.used[LXA_BUDGET_INPUT] != sizeof(bytes)) {
        return 1;
    }
    credit = budget.work_credit;
    lxa_buffer_destroy(&first);
    if (lxa_html_input_resolve_label(
            (lxa_span_t) {(const uint8_t *) "windows-1252", 12},
            &encoding) != LXA_HTML_INPUT_OK
        || lxa_html_input_decode_prepaid_report(raw, encoding, 0,
            &budget, &retry, &malformed, &reason, NULL, NULL, NULL)
           != LXA_HTML_INPUT_OK
        || malformed != 0 || retry.length != 6
        || memcmp(retry.data, "<p>\xe2\x82\xac", 6) != 0
        || budget.used[LXA_BUDGET_INPUT] != sizeof(bytes)
        || budget.work_credit != credit) {
        return 1;
    }
    lxa_buffer_destroy(&retry);
    if (budget.used[LXA_BUDGET_MEMORY] != 0
        || lxa_buffer_bind_budget(&retry, &budget) != LXA_OK
        || lxa_html_input_decode_prepaid_report(raw, encoding, 0,
            &budget, &retry, &malformed, &reason, NULL, NULL, NULL)
           != LXA_HTML_INPUT_LIMIT
        || reason != LXA_BUDGET_WORK || retry.data != NULL
        || retry.length != 0 || budget.work_credit != credit
        || budget.used[LXA_BUDGET_INPUT] != sizeof(bytes)
        || budget.used[LXA_BUDGET_MEMORY] != 0) {
        return 1;
    }
    lxa_buffer_destroy(&retry);
    if (lxa_budget_init(&budget, NULL) != LXA_OK
        || lxa_buffer_init(&first, &allocator) != LXA_OK
        || lxa_buffer_bind_budget(&first, &budget) != LXA_OK) {
        return 1;
    }
    encoding = lxa_encoding_lookup((lxa_span_t) {
        (const uint8_t *) "utf-8", 5
    });
    if (lxa_html_input_decode(raw, encoding, 0, &budget, &first,
                              &malformed, &reason) != LXA_HTML_INPUT_OK
        || malformed != 1 || state.count != 0
        || lxa_html_input_report_malformed(
            raw, encoding, 0, malformed, &budget, &allocator,
            diagnostic, &state, &callback_status, &reason)
           != LXA_HTML_INPUT_OK
        || state.count != 1 || state.offsets[0] != 3
        || callback_status != LXA_OK || reason != LXA_BUDGET_COUNT
        || budget.used[LXA_BUDGET_INPUT] != sizeof(bytes)) {
        return 1;
    }
    lxa_buffer_destroy(&first);
    if (budget.used[LXA_BUDGET_MEMORY] != 0) {
        return 1;
    }
    {
        static const uint8_t mixed[] = {
            '<', 'p', '>', 0x8f, 0xa2, 'Z'
        };
        lxa_span_t input = {mixed, sizeof(mixed)};
        state = (diagnostics_t) {{0}, 0, 4};
        if (lxa_budget_init(&budget, NULL) != LXA_OK
            || lxa_buffer_init(&first, &allocator) != LXA_OK
            || lxa_buffer_bind_budget(&first, &budget) != LXA_OK
            || lxa_buffer_init(&retry, &allocator) != LXA_OK
            || lxa_buffer_bind_budget(&retry, &budget) != LXA_OK
            || lxa_html_input_decode(input, encoding, 0, &budget,
                &first, &malformed, &reason) != LXA_HTML_INPUT_OK
            || malformed != 2 || state.count != 0) {
            return 1;
        }
        lxa_buffer_destroy(&first);
        encoding = lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "euc-jp", 6
        });
        if (lxa_html_input_decode_prepaid_report(input, encoding, 0,
                &budget, &retry, &malformed, &reason, NULL, NULL, NULL)
                != LXA_HTML_INPUT_OK
            || malformed != 1 || state.count != 0
            || lxa_html_input_report_malformed(input, encoding, 0,
                malformed, &budget, &allocator, diagnostic, &state,
                &callback_status, &reason) != LXA_HTML_INPUT_OK
            || state.count != 1 || state.offsets[0] != 3
            || callback_status != LXA_OK
            || budget.used[LXA_BUDGET_INPUT] != sizeof(mixed)
            || reason != LXA_BUDGET_COUNT) {
            return 1;
        }
        lxa_buffer_destroy(&retry);
        if (budget.used[LXA_BUDGET_MEMORY] != 0) {
            return 1;
        }
    }
    limits.initial_work_credit = 11;
    limits.work_per_input_byte = 0;
    state = (diagnostics_t) {{0}, 0, 4};
    encoding = lxa_encoding_lookup((lxa_span_t) {
        (const uint8_t *) "utf-8", 5
    });
    if (lxa_budget_init(&budget, &limits) != LXA_OK
        || lxa_buffer_init(&first, &allocator) != LXA_OK
        || lxa_buffer_bind_budget(&first, &budget) != LXA_OK
        || lxa_html_input_decode((lxa_span_t) {two_errors,
            sizeof(two_errors)}, encoding, 0, &budget, &first,
            &malformed, &reason) != LXA_HTML_INPUT_OK
        || malformed != 2 || budget.used[LXA_BUDGET_WORK] != 8
        || lxa_html_input_report_malformed(
            (lxa_span_t) {two_errors, sizeof(two_errors)}, encoding, 0,
            malformed, &budget, &allocator, diagnostic, &state,
            &callback_status, &reason) != LXA_HTML_INPUT_LIMIT
        || reason != LXA_BUDGET_WORK || state.count != 0
        || budget.used[LXA_BUDGET_WORK] != 8
        || budget.used[LXA_BUDGET_INPUT] != sizeof(two_errors)) {
        return 1;
    }
    lxa_buffer_destroy(&first);
    if (budget.used[LXA_BUDGET_MEMORY] != 0) {
        return 1;
    }
    return 0;
}

int
main(void)
{
    static const uint8_t bom_utf8[] = {0xef, 0xbb, 0xbf, '<'};
    static const uint8_t bom_utf16be[] = {0xfe, 0xff, 0, '<'};
    static const uint8_t bom_utf16le[] = {0xff, 0xfe, '<', 0};
    static const uint8_t bom_utf32be[] = {0, 0, 0xfe, 0xff};
    const lxa_encoding_t *encoding = NULL;
    size_t skip = 0;
    lxa_span_t bytes = {bom_utf8, sizeof(bom_utf8)};

    if (lxa_html_input_bom(bytes, &encoding, &skip) != LXA_OK
        || encoding != lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "utf-8", 5
        }) || skip != 3) {
        return 1;
    }
    bytes.data = bom_utf16be;
    bytes.length = sizeof(bom_utf16be);
    if (lxa_html_input_bom(bytes, &encoding, &skip) != LXA_OK
        || encoding != lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "utf-16be", 8
        }) || skip != 2) {
        return 1;
    }
    bytes.data = bom_utf16le;
    bytes.length = sizeof(bom_utf16le);
    if (lxa_html_input_bom(bytes, &encoding, &skip) != LXA_OK
        || encoding != lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "utf-16le", 8
        }) || skip != 2) {
        return 1;
    }
    bytes.data = bom_utf32be;
    bytes.length = sizeof(bom_utf32be);
    if (lxa_html_input_bom(bytes, &encoding, &skip) != LXA_OK
        || encoding != NULL || skip != 0) {
        return 1;
    }
    bytes.data = bom_utf8;
    bytes.length = 2; /* Incomplete UTF-8 BOM: not a recognized BOM. */
    if (lxa_html_input_bom(bytes, &encoding, &skip) != LXA_OK
        || encoding != NULL || skip != 0) {
        return 1;
    }
    bytes.data = NULL;
    bytes.length = 1;
    if (lxa_html_input_bom(bytes, &encoding, &skip) != LXA_ERROR_ARGUMENT) {
        return 1;
    }
    bytes.length = 0;
    if (lxa_html_input_bom(bytes, &encoding, &skip) != LXA_OK
        || encoding != NULL || skip != 0) {
        return 1;
    }
    {
        static const char markup[] =
            "<!-- <meta charset=bad> --><div title='><meta charset=bad'>"
            "</div><metadata charset=bad><META data-x='x' CHARSET='Windows-1252'>";
        bool found = false;
        lxa_span_t label = {NULL, 0};
        bytes.data = (const uint8_t *) markup;
        bytes.length = sizeof(markup) - 1;
        if (lxa_html_input_meta_charset(bytes, &found, &label) != LXA_OK
            || !found || label.length != strlen("Windows-1252")
            || memcmp(label.data, "Windows-1252", label.length) != 0
            || lxa_encoding_lookup(label) != lxa_encoding_lookup(
                (lxa_span_t) {(const uint8_t *) "windows-1252", 12})) {
            return 1;
        }
        bytes.data = (const uint8_t *) "<meta charset=''>";
        bytes.length = strlen((const char *) bytes.data);
        if (lxa_html_input_meta_charset(bytes, &found, &label) != LXA_OK
            || !found || label.length != 0) {
            return 1;
        }
        bytes.data = (const uint8_t *) "<meta charset='windows-1252'";
        bytes.length = strlen((const char *) bytes.data);
        if (lxa_html_input_meta_charset(bytes, &found, &label) != LXA_OK
            || found || label.data != NULL) {
            return 1;
        }
        bytes.data = (const uint8_t *) "<meta content='text/html'>";
        bytes.length = strlen((const char *) bytes.data);
        if (lxa_html_input_meta_charset(bytes, &found, &label) != LXA_OK
            || found || label.length != 0) {
            return 1;
        }
        bytes.data = (const uint8_t *)
            "<meta content='text/html; CHARSET = \"windows-1252\"' "
            "http-equiv='CONTENT-TYPE'>";
        bytes.length = strlen((const char *) bytes.data);
        if (lxa_html_input_meta_charset(bytes, &found, &label) != LXA_OK
            || !found || lxa_encoding_lookup(label)
               != lxa_encoding_lookup((lxa_span_t) {
                   (const uint8_t *) "windows-1252", 12
               })) {
            return 1;
        }
        {
            static const char content[] =
                "text/html; CHARSET = \"windows-1252\"";
            uint8_t late[1060];
            const char declaration[] = "charset=windows-1252";
            lxa_span_t value = {(const uint8_t *) content,
                                sizeof(content) - 1};
            if (lxa_html_input_content_charset(value, &found, &label)
                != LXA_OK || !found || label.data != value.data + 22
                || label.length != 12
                || memcmp(label.data, "windows-1252", 12) != 0) {
                return 1;
            }
            memset(late, ' ', sizeof(late));
            memcpy(late + 1030, declaration, sizeof(declaration) - 1);
            value = (lxa_span_t) {late, sizeof(late)};
            if (lxa_html_input_content_charset(value, &found, &label)
                != LXA_OK || !found || label.data != late + 1038
                || label.length != 12) {
                return 1;
            }
            value = (lxa_span_t) {(const uint8_t *) "charset='", 9};
            if (lxa_html_input_content_charset(value, &found, &label)
                != LXA_OK || found || label.data != NULL) {
                return 1;
            }
            value = (lxa_span_t) {NULL, 1};
            if (lxa_html_input_content_charset(value, &found, &label)
                != LXA_ERROR_ARGUMENT) {
                return 1;
            }
        }
        bytes.data = (const uint8_t *)
            "<meta content='text/html; charset=utf-8'><meta charset=iso-8859-1>";
        bytes.length = strlen((const char *) bytes.data);
        if (lxa_html_input_meta_charset(bytes, &found, &label) != LXA_OK
            || !found || lxa_encoding_lookup(label)
               != lxa_encoding_lookup((lxa_span_t) {
                   (const uint8_t *) "windows-1252", 12
               })) {
            return 1;
        }
        bytes.data = (const uint8_t *)
            "<meta http-equiv=content-type content='charset=\"utf-8'"
            "><meta charset=windows-1252>";
        bytes.length = strlen((const char *) bytes.data);
        if (lxa_html_input_meta_charset(bytes, &found, &label) != LXA_OK
            || !found || lxa_encoding_lookup(label)
               != lxa_encoding_lookup((lxa_span_t) {
                   (const uint8_t *) "windows-1252", 12
               })) {
            return 1;
        }
        bytes.data = NULL;
        bytes.length = 1;
        if (lxa_html_input_meta_charset(bytes, &found, &label)
            != LXA_ERROR_ARGUMENT) {
            return 1;
        }
        {
            uint8_t page[1060];
            const char declaration[] = "<meta charset=windows-1252>";
            memset(page, 'x', sizeof(page));
            memcpy(page + 1025, declaration, sizeof(declaration) - 1);
            bytes.data = page;
            bytes.length = sizeof(page);
            if (lxa_html_input_meta_charset(bytes, &found, &label) != LXA_OK
                || found) {
                return 1;
            }
            memcpy(page + 980, declaration, sizeof(declaration) - 1);
            if (lxa_html_input_meta_charset(bytes, &found, &label) != LXA_OK
                || !found || lxa_encoding_lookup(label)
                   != lxa_encoding_lookup((lxa_span_t) {
                       (const uint8_t *) "windows-1252", 12
                   })) {
                return 1;
            }
        }
        bytes.data = (const uint8_t *)
            "<?xml version='1.0' encoding = 'windows-1252'?>";
        bytes.length = strlen((const char *) bytes.data);
        if (lxa_html_input_xml_encoding(bytes, &found, &label) != LXA_OK
            || !found || lxa_encoding_lookup(label)
               != lxa_encoding_lookup((lxa_span_t) {
                   (const uint8_t *) "windows-1252", 12
               })) {
            return 1;
        }
        bytes.data = (const uint8_t *)
            "<?XML encoding='utf-8'?><meta charset=windows-1252>";
        bytes.length = strlen((const char *) bytes.data);
        if (lxa_html_input_xml_encoding(bytes, &found, &label) != LXA_OK
            || found) {
            return 1;
        }
        bytes.data = (const uint8_t *)
            "<?xml encoding='utf 8'?>";
        bytes.length = strlen((const char *) bytes.data);
        if (lxa_html_input_xml_encoding(bytes, &found, &label) != LXA_OK
            || found) {
            return 1;
        }
        bytes.data = (const uint8_t *)
            "<?xml encoding=utf-8?>";
        bytes.length = strlen((const char *) bytes.data);
        if (lxa_html_input_xml_encoding(bytes, &found, &label) != LXA_OK
            || found) {
            return 1;
        }
    }
    {
        static const struct {
            const char *html, *expected;
            lxa_html_input_result_t result;
        } cases[] = {
            {"", "utf-8", LXA_HTML_INPUT_OK},
            {"<meta charset=windows-1252>", "windows-1252",
             LXA_HTML_INPUT_OK},
            {"<meta http-equiv=content-type content='charset=latin1'>",
             "windows-1252", LXA_HTML_INPUT_OK},
            {"<meta charset=utf-16le>", "utf-8", LXA_HTML_INPUT_OK},
            {"<meta charset=x-user-defined>", "windows-1252",
             LXA_HTML_INPUT_OK},
            {"<?xml encoding='euc-jp'?>", "euc-jp", LXA_HTML_INPUT_OK},
            {"<meta charset=not-an-encoding>", NULL,
             LXA_HTML_INPUT_UNKNOWN_ENCODING},
            {"<?xml encoding='not-an-encoding'?>", NULL,
             LXA_HTML_INPUT_UNKNOWN_ENCODING}
        };
        size_t i;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            bytes.data = (const uint8_t *) cases[i].html;
            bytes.length = strlen(cases[i].html);
            encoding = (const lxa_encoding_t *) (uintptr_t) 1;
            skip = 99;
            if (lxa_html_input_select(bytes, 16u * 1024u * 1024u,
                                      &encoding, &skip) != cases[i].result
                || skip != 0
                || encoding != (cases[i].expected == NULL ? NULL
                    : lxa_encoding_lookup((lxa_span_t) {
                        (const uint8_t *) cases[i].expected,
                        strlen(cases[i].expected)
                    }))) {
                return 1;
            }
        }
    }
    bytes.data = bom_utf8;
    bytes.length = sizeof(bom_utf8);
    if (lxa_html_input_select(bytes, sizeof(bom_utf8) - 1,
                              &encoding, &skip) != LXA_HTML_INPUT_LIMIT
        || encoding != NULL || skip != 0) {
        return 1;
    }
    if (lxa_html_input_select(bytes, sizeof(bom_utf8),
                              &encoding, &skip) != LXA_HTML_INPUT_OK
        || encoding != lxa_encoding_lookup((lxa_span_t) {
            (const uint8_t *) "utf-8", 5
        }) || skip != 3) {
        return 1;
    }
    {
        static const uint8_t bom_overrides_meta[] =
            "\xef\xbb\xbf<meta charset=not-an-encoding>";
        bytes = (lxa_span_t) {bom_overrides_meta,
                              sizeof(bom_overrides_meta) - 1};
        if (lxa_html_input_select(bytes, bytes.length, &encoding, &skip)
            != LXA_HTML_INPUT_OK
            || encoding != lxa_encoding_lookup((lxa_span_t) {
                (const uint8_t *) "utf-8", 5
            }) || skip != 3) {
            return 1;
        }
    }
    {
        static const uint8_t xml_le[] = {'<', 0, '?', 0, 'x', 0, 0};
        bytes = (lxa_span_t) {xml_le, sizeof(xml_le)};
        if (lxa_html_input_select(bytes, bytes.length, &encoding, &skip)
            != LXA_HTML_INPUT_OK
            || encoding != lxa_encoding_lookup((lxa_span_t) {
                (const uint8_t *) "utf-16le", 8
            }) || skip != 0) {
            return 1;
        }
    }
    bytes = (lxa_span_t) {NULL, 1};
    if (lxa_html_input_select(bytes, 16u * 1024u * 1024u,
                              &encoding, &skip) != LXA_HTML_INPUT_BAD_ARGUMENT
        || encoding != NULL || skip != 0) {
        return 1;
    }
    {
        static const uint8_t alphabet[] =
            "<!-/=>?\"' \nmetaCHARSETcontenttypewindows1252\x80";
        uint8_t page[1100];
        uint32_t seed = 0x9182745u;
        size_t attempt;
        for (attempt = 0; attempt < 512; attempt++) {
            size_t i, length;
            bool found;
            lxa_span_t label;
            lxa_html_input_result_t result;
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            length = seed % (sizeof(page) + 1);
            for (i = 0; i < length; i++) {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                page[i] = alphabet[seed % (sizeof(alphabet) - 1)];
            }
            bytes = (lxa_span_t) {page, length};
            if (lxa_html_input_meta_charset(bytes, &found, &label) != LXA_OK
                || (found && (label.data == NULL
                    || label.length > length))) {
                return 1;
            }
            result = lxa_html_input_select(bytes, sizeof(page),
                                            &encoding, &skip);
            if (result != LXA_HTML_INPUT_OK
                && result != LXA_HTML_INPUT_UNKNOWN_ENCODING) {
                return 1;
            }
            if ((result == LXA_HTML_INPUT_OK && encoding == NULL)
                || (result != LXA_HTML_INPUT_OK && encoding != NULL)
                || skip != 0) {
                return 1;
            }
        }
    }
    return bx_input_boundary_tests() || decode_tests()
           || diagnostics_tests() || late_declaration_tests();
}
