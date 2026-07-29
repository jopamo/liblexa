#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "html/tokenizer.h"
#include "html/input_encoding.h"

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
        abort(); \
    } \
} while (0)

typedef struct {
    lxa_html_token_kind_t kind[16];
    char bytes[16][96];
    size_t lengths[16];
    char pi_data[16][96];
    char system_ids[16][96], public_ids[16][96];
    bool has_system_id[16], has_public_id[16];
    char attribute_names[16][4][64], attribute_values[16][4][96];
    size_t attribute_count[16];
    bool self_closing[16];
    bool force_quirks[16];
    size_t count;
    lxa_html_token_kind_t fail_on;
    size_t errors;
    bool error_fail;
    bool allow_mode_failure;
    bool foreign_feedback;
    bool foreign_active;
    size_t error_offset;
    const char *error_code;
    const char *first_error_code;
} fixture_t;

static lxa_span_t
span(const char *text)
{
    lxa_span_t result = {(const uint8_t *) text, strlen(text)};
    return result;
}

static lxa_status_t
on_token(void *context, lxa_html_tokenizer_t *tokenizer,
         const lxa_html_token_t *token)
{
    fixture_t *f = context;
    lxa_span_t data = token->kind == LXA_HTML_TEXT
                      || token->kind == LXA_HTML_COMMENT
                      ? token->data : token->name;
    size_t i = f->count++;
    CHECK(i < 16 && data.length < sizeof(f->bytes[i]));
    CHECK(token->attribute_count <= 4);
    CHECK((token->attribute_count != 0) == (token->attributes != NULL));
    CHECK(token->kind == LXA_HTML_START_TAG || token->attribute_count == 0);
    f->attribute_count[i] = token->attribute_count;
    for (size_t j = 0; j < token->attribute_count; j++) {
        lxa_span_t name = token->attributes[j].name;
        lxa_span_t value = token->attributes[j].value;
        CHECK(name.length < sizeof(f->attribute_names[i][j])
              && value.length < sizeof(f->attribute_values[i][j]));
        if (name.length != 0) {
            memcpy(f->attribute_names[i][j], name.data, name.length);
        }
        if (value.length != 0) {
            memcpy(f->attribute_values[i][j], value.data, value.length);
        }
        f->attribute_names[i][j][name.length] = 0;
        f->attribute_values[i][j][value.length] = 0;
    }
    f->kind[i] = token->kind;
    if (data.length != 0) {
        memcpy(f->bytes[i], data.data, data.length);
    }
    f->bytes[i][data.length] = 0;
    f->lengths[i] = data.length;
    CHECK(token->kind == LXA_HTML_DOCTYPE
          || (!token->has_system_id && !token->has_public_id));
    f->has_public_id[i] = token->has_public_id;
    if (token->has_public_id) {
        CHECK(token->public_id.length < sizeof(f->public_ids[i]));
        if (token->public_id.length != 0) {
            memcpy(f->public_ids[i], token->public_id.data,
                   token->public_id.length);
        }
        f->public_ids[i][token->public_id.length] = 0;
    }
    f->has_system_id[i] = token->has_system_id;
    if (token->has_system_id) {
        CHECK(token->system_id.length < sizeof(f->system_ids[i]));
        if (token->system_id.length != 0) {
            memcpy(f->system_ids[i], token->system_id.data,
                   token->system_id.length);
        }
        f->system_ids[i][token->system_id.length] = 0;
    }
    if (token->kind == LXA_HTML_PROCESSING_INSTRUCTION) {
        CHECK(token->data.length < sizeof(f->pi_data[i]));
        if (token->data.length != 0) {
            memcpy(f->pi_data[i], token->data.data, token->data.length);
        }
        f->pi_data[i][token->data.length] = 0;
    }
    f->self_closing[i] = token->self_closing;
    f->force_quirks[i] = token->force_quirks;
    if (token->kind == LXA_HTML_START_TAG) {
        CHECK(lxa_html_tokenizer_feed(tokenizer, span("")) == LXA_HTML_BAD_ARGUMENT);
        CHECK(lxa_html_tokenizer_set_text_mode(tokenizer, LXA_HTML_DATA,
                                                span("")) == LXA_HTML_OK);
        if (token->name.length == 6
            && memcmp(token->name.data, "script", 6) == 0) {
            lxa_html_result_t mode = lxa_html_tokenizer_set_text_mode(
                tokenizer, LXA_HTML_SCRIPT_DATA, token->name);
            if (mode == LXA_HTML_NO_MEMORY && f->allow_mode_failure) {
                return LXA_ERROR_NO_MEMORY;
            }
            CHECK(mode == LXA_HTML_OK);
        }
        if (token->name.length == 9
            && memcmp(token->name.data, "plaintext", 9) == 0) {
            CHECK(lxa_html_tokenizer_set_text_mode(tokenizer,
                      LXA_HTML_PLAINTEXT, span("")) == LXA_HTML_OK);
        }
        if (token->name.length == 5
            && memcmp(token->name.data, "title", 5) == 0) {
            CHECK(lxa_html_tokenizer_set_text_mode(tokenizer, LXA_HTML_RCDATA,
                                                    token->name) == LXA_HTML_OK);
        }
        if (token->name.length == 5
            && memcmp(token->name.data, "style", 5) == 0) {
            lxa_html_result_t mode = lxa_html_tokenizer_set_text_mode(
                tokenizer, LXA_HTML_RAWTEXT, token->name);
            if (mode == LXA_HTML_NO_MEMORY && f->allow_mode_failure) {
                return LXA_ERROR_NO_MEMORY;
            }
            CHECK(mode == LXA_HTML_OK);
        }
        CHECK(lxa_html_tokenizer_allow_cdata(tokenizer, true) == LXA_HTML_OK);
        CHECK(lxa_html_tokenizer_allow_cdata(tokenizer, false) == LXA_HTML_OK);
    }
    if (f->foreign_feedback
        && (token->kind == LXA_HTML_START_TAG
            || token->kind == LXA_HTML_END_TAG)) {
        if ((token->name.length == 4
             && memcmp(token->name.data, "math", 4) == 0)
            || (token->name.length == 3
                && memcmp(token->name.data, "svg", 3) == 0)) {
            f->foreign_active = token->kind == LXA_HTML_START_TAG;
        }
        CHECK(lxa_html_tokenizer_allow_cdata(tokenizer, f->foreign_active)
              == LXA_HTML_OK);
    }
    return token->kind == f->fail_on ? LXA_ERROR_NO_MEMORY : LXA_OK;
}

static lxa_status_t
on_error(void *context, const char *code, size_t offset)
{
    fixture_t *f = context;
    CHECK(strcmp(code, "invalid-utf-8") == 0
          || strcmp(code, "unexpected-null-character") == 0
          || strcmp(code, "null-character-reference") == 0
          || strcmp(code, "surrogate-character-reference") == 0
          || strcmp(code, "character-reference-outside-unicode-range") == 0
          || strcmp(code, "control-character-reference") == 0
          || strcmp(code, "noncharacter-character-reference") == 0
          || strcmp(code, "missing-semicolon-after-character-reference") == 0
          || strcmp(code, "unknown-named-character-reference") == 0
          || strcmp(code, "absence-of-digits-in-numeric-character-reference") == 0
          || strcmp(code, "eof-in-comment") == 0
          || strcmp(code, "abrupt-closing-of-empty-comment") == 0
          || strcmp(code, "incorrectly-closed-comment") == 0
          || strcmp(code, "nested-comment") == 0
          || strcmp(code, "control-character-in-input-stream") == 0
          || strcmp(code, "noncharacter-in-input-stream") == 0
          || strcmp(code, "invalid-first-character-of-tag-name") == 0
          || strcmp(code, "missing-end-tag-name") == 0
          || strcmp(code, "eof-before-tag-name") == 0
          || strcmp(code, "incorrectly-opened-comment") == 0
          || strcmp(code, "eof-in-tag") == 0
          || strcmp(code, "end-tag-with-trailing-solidus") == 0
          || strcmp(code, "end-tag-with-attributes") == 0
          || strcmp(code, "duplicate-attribute") == 0
          || strcmp(code, "unexpected-character-in-attribute-name") == 0
          || strcmp(code, "unexpected-character-in-unquoted-attribute-value") == 0
          || strcmp(code, "unexpected-equals-sign-before-attribute-name") == 0
          || strcmp(code, "missing-attribute-value") == 0
          || strcmp(code, "missing-whitespace-between-attributes") == 0
          || strcmp(code, "unexpected-solidus-in-tag") == 0
          || strcmp(code, "eof-in-processing-instruction") == 0
          || strcmp(code,
               "invalid-first-character-of-processing-instruction-target") == 0
          || strcmp(code, "invalid-processing-instruction-target") == 0
          || strcmp(code, "disallowed-processing-instruction-target") == 0
          || strcmp(code, "eof-in-doctype") == 0
          || strcmp(code, "eof-in-script-html-comment-like-text") == 0
          || strcmp(code, "eof-in-cdata") == 0
          || strcmp(code, "cdata-in-html-content") == 0
          || strcmp(code, "missing-doctype-name") == 0
          || strcmp(code, "missing-whitespace-after-doctype-system-keyword") == 0
          || strcmp(code, "missing-whitespace-after-doctype-public-keyword") == 0
          || strcmp(code, "missing-whitespace-between-doctype-public-and-system-identifiers") == 0
          || strcmp(code, "missing-doctype-public-identifier") == 0
          || strcmp(code, "missing-quote-before-doctype-public-identifier") == 0
          || strcmp(code, "abrupt-doctype-public-identifier") == 0
          || strcmp(code, "missing-doctype-system-identifier") == 0
          || strcmp(code, "missing-quote-before-doctype-system-identifier") == 0
          || strcmp(code, "abrupt-doctype-system-identifier") == 0
          || strcmp(code, "unexpected-character-after-doctype-system-identifier") == 0
          || strcmp(code, "invalid-character-sequence-after-doctype-name") == 0
          || strcmp(code, "missing-whitespace-before-doctype-name") == 0);
    if (f->errors == 0) {
        f->first_error_code = code;
    }
    f->error_code = code;
    f->error_offset = offset;
    f->errors++;
    return f->error_fail ? LXA_ERROR_NO_MEMORY : LXA_OK;
}

static lxa_status_t
on_raw_malformed(void *context, size_t offset)
{
    size_t *at = context;
    if (*at != SIZE_MAX) return LXA_ERROR_ARGUMENT;
    *at = offset;
    return LXA_OK;
}

typedef struct {
    bool fail_growth;
    size_t releases;
} allocation_t;

static void *
allocate(void *context, size_t size)
{
    (void) context;
    return malloc(size);
}

static void *
resize(void *context, void *ptr, size_t size)
{
    allocation_t *state = context;
    return state->fail_growth ? NULL : realloc(ptr, size);
}

static void
release(void *context, void *ptr)
{
    allocation_t *state = context;
    state->releases++;
    free(ptr);
}

typedef struct {
    size_t calls, fail_at, live;
    bool failed;
} fault_t;

static bool
should_fail(fault_t *state)
{
    if (state->calls++ == state->fail_at) {
        state->failed = true;
        return true;
    }
    return false;
}

static void *
fault_allocate(void *context, size_t size)
{
    fault_t *state = context;
    void *ptr;
    if (should_fail(state)) {
        return NULL;
    }
    ptr = malloc(size);
    CHECK(ptr != NULL);
    state->live++;
    return ptr;
}

static void *
fault_resize(void *context, void *ptr, size_t size)
{
    fault_t *state = context;
    void *resized;
    bool was_null = ptr == NULL;
    if (should_fail(state)) {
        return NULL;
    }
    resized = realloc(ptr, size);
    CHECK(resized != NULL);
    if (was_null) {
        state->live++;
    }
    return resized;
}

static void
fault_release(void *context, void *ptr)
{
    fault_t *state = context;
    CHECK(ptr != NULL && state->live != 0);
    state->live--;
    free(ptr);
}

static void
check_allocation_failures(void)
{
    size_t n;
    bool completed = false;
    for (n = 0; n < 32; n++) {
        fault_t state = {0};
        lxa_allocator_t allocator = {&state, fault_allocate, fault_resize,
                                     fault_release};
        lxa_html_tokenizer_t *tkz = NULL;
        fixture_t f = {0};
        lxa_status_t created;
        lxa_html_result_t result;
        state.fail_at = n;
        f.fail_on = LXA_HTML_PROCESSING_INSTRUCTION;
        f.allow_mode_failure = true;
        created = lxa_html_tokenizer_create(&allocator, on_token, on_error,
                                             &f, &tkz);
        if (created != LXA_OK) {
            CHECK(created == LXA_ERROR_NO_MEMORY && state.failed
                   && tkz == NULL && state.live == 0);
            continue;
        }
        result = lxa_html_tokenizer_feed(tkz,
                   span("<?!><?long_target:><!DOCTYPE html PUBLIC 'p' 'x'>"
                        "<style>x</style>"
                        "<script><!--<script>x</script>--></script>"
                        "<!--co-->x&#65;<A href='&#x80;&amp;'"
                        " HREF='discard&amp;' src='last'/>\xe2"));
        if (result == LXA_HTML_OK) {
            result = lxa_html_tokenizer_finish(tkz);
        }
        if (state.failed) {
            CHECK((result == LXA_HTML_NO_MEMORY
                   || (result == LXA_HTML_CALLBACK_FAILURE
                       && lxa_html_tokenizer_callback_status(tkz)
                          == LXA_ERROR_NO_MEMORY))
                   && lxa_html_tokenizer_finish(tkz) == result
                   && (result == LXA_HTML_CALLBACK_FAILURE
                       || lxa_html_tokenizer_callback_status(tkz) == LXA_OK));
            lxa_html_tokenizer_reset(tkz);
            CHECK(lxa_html_tokenizer_feed(tkz, span("ok")) == LXA_HTML_OK);
            CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
        }
        else {
            CHECK(result == LXA_HTML_OK && f.count == 14);
            completed = true;
        }
        lxa_html_tokenizer_destroy(tkz);
        CHECK(state.live == 0);
        if (completed) {
            break;
        }
    }
    CHECK(completed && n >= 3);
}

static void
on_large_attributes_check(const lxa_html_token_t *token, size_t expected)
{
    CHECK(token->attribute_count == expected);
    for (size_t i = 0; i < expected; i++) {
        char name[16];
        int length = snprintf(name, sizeof(name), "a%05zu", i ^ 32);
        CHECK(length > 0 && (size_t) length < sizeof(name)
              && token->attributes[i].name.length == (size_t) length
              && memcmp(token->attributes[i].name.data, name, (size_t) length) == 0
              && token->attributes[i].value.length == 1
              && token->attributes[i].value.data[0] == 'x');
    }
}

static lxa_status_t
on_large_attributes(void *context, lxa_html_tokenizer_t *tokenizer,
                    const lxa_html_token_t *token)
{
    fixture_t *f = context;
    (void) tokenizer;
    if (token->kind == LXA_HTML_START_TAG) {
        CHECK(f->count < 2);
        on_large_attributes_check(token, f->attribute_count[f->count]);
    }
    else {
        CHECK(token->kind == LXA_HTML_EOF);
    }
    f->count++;
    return LXA_OK;
}

static void
check_large_attributes(void)
{
    char input[2048];
    size_t used = 0, prefix = 0, n;
    bool completed = false;
    memcpy(input, "<a", 2);
    used = 2;
    for (size_t i = 0; i < 64; i++) {
        int length = snprintf(input + used, sizeof(input) - used,
                              " a%05zu=x", i ^ 32);
        CHECK(length > 0 && (size_t) length < sizeof(input) - used);
        used += (size_t) length;
        if (i == 31) prefix = used;
        if (i == 15 || i == 31 || i == 63) {
            const char *duplicate = i == 31 ? " A00063=y" : " A00032=y";
            size_t length = strlen(duplicate);
            CHECK(length < sizeof(input) - used);
            memcpy(input + used, duplicate, length);
            used += length;
        }
    }
    {
        const char suffix[] = "><a a00032=x>";
        CHECK(sizeof(suffix) <= sizeof(input) - used);
        memcpy(input + used, suffix, sizeof(suffix));
        used += sizeof(suffix) - 1;
    }
    for (n = 0; n < 256; n++) {
        fault_t state = {.fail_at = n};
        lxa_allocator_t allocator = {&state, fault_allocate, fault_resize,
                                     fault_release};
        lxa_budget_t budget;
        lxa_html_tokenizer_t *tkz = NULL;
        fixture_t f = {0};
        lxa_status_t status;
        lxa_html_result_t result;
        f.attribute_count[0] = 64;
        f.attribute_count[1] = 1;
        CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
        status = lxa_html_tokenizer_create_with_budget(&allocator,
            on_large_attributes, on_error, &f, &budget, &tkz);
        if (status != LXA_OK) {
            CHECK(status == LXA_ERROR_NO_MEMORY && state.failed
                  && tkz == NULL && state.live == 0
                  && budget.used[LXA_BUDGET_MEMORY] == 0);
            continue;
        }
        result = LXA_HTML_OK;
        for (size_t offset = 0; result == LXA_HTML_OK && offset < used;) {
            size_t length = used - offset < 7 ? used - offset : 7;
            result = lxa_html_tokenizer_feed(tkz,
                (lxa_span_t) {(const uint8_t *) input + offset, length});
            offset += length;
        }
        if (result == LXA_HTML_OK) {
            result = lxa_html_tokenizer_finish(tkz);
        }
        if (state.failed) {
            CHECK(result == LXA_HTML_NO_MEMORY
                  && lxa_html_tokenizer_finish(tkz) == result);
            lxa_html_tokenizer_reset(tkz);
            memset(&f, 0, sizeof(f));
            f.attribute_count[0] = 1;
            CHECK(lxa_html_tokenizer_feed(tkz, span("<a a00032=x>"))
                  == LXA_HTML_OK);
            CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK && f.count == 2);
        }
        else {
            CHECK(result == LXA_HTML_OK && f.count == 3 && f.errors == 3);
            completed = true;
        }
        lxa_html_tokenizer_destroy(tkz);
        CHECK(state.live == 0 && budget.used[LXA_BUDGET_MEMORY] == 0);
        if (completed) break;
    }
    CHECK(completed && n >= 64);
    {
        lxa_allocator_t allocator = lxa_allocator_default();
        lxa_budget_t budget;
        lxa_html_tokenizer_t *tkz = NULL;
        fixture_t f = {0};
        CHECK(lxa_budget_init(&budget, NULL) == LXA_OK);
        CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
            on_large_attributes, on_error, &f, &budget, &tkz) == LXA_OK);
        CHECK(lxa_html_tokenizer_feed(tkz,
            (lxa_span_t) {(const uint8_t *) input, prefix}) == LXA_HTML_OK);
        budget.limits.max_memory_bytes = budget.used[LXA_BUDGET_MEMORY];
        CHECK(lxa_html_tokenizer_feed(tkz, span(" a00000=")) == LXA_HTML_LIMIT
              && lxa_html_tokenizer_limit_reason(tkz) == LXA_BUDGET_MEMORY);
        lxa_html_tokenizer_destroy(tkz);
        CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    }
    {
        char input[160];
        lxa_allocator_t allocator = lxa_allocator_default();
        lxa_limits_t limits = lxa_limits_default();
        lxa_budget_t budget;
        lxa_html_tokenizer_t *tkz = NULL;
        fixture_t f = {0};
        int length = snprintf(input, sizeof(input), "<a n%050d=x n%050d=y>", 0, 0);
        CHECK(length > 0 && (size_t) length < sizeof(input));
        limits.max_work = 170;
        CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
            on_large_attributes, on_error, &f, &budget, &tkz) == LXA_OK);
        CHECK(lxa_html_tokenizer_feed(tkz,
            (lxa_span_t) {(const uint8_t *) input, (size_t) length})
            == LXA_HTML_LIMIT
            && lxa_html_tokenizer_limit_reason(tkz) == LXA_BUDGET_WORK);
        lxa_html_tokenizer_destroy(tkz);
        CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    }
}

static void
check_token_sizes(void)
{
    static const struct {
        const char *input;
        size_t length, maximum;
        lxa_html_result_t expected;
    } cases[] = {
        {"<AbC>", sizeof("<AbC>") - 1, 3, LXA_HTML_OK},
        {"<AbCD>", sizeof("<AbCD>") - 1, 3, LXA_HTML_LIMIT},
        {"<a\xc3\xa9>", sizeof("<a\xc3\xa9>") - 1, 3, LXA_HTML_OK},
        {"<a\xc3\xa9x>", sizeof("<a\xc3\xa9x>") - 1, 3, LXA_HTML_LIMIT},
        {"<a\0>", sizeof("<a\0>") - 1, 3, LXA_HTML_LIMIT},
        {"<!DOCTYPE abc>", sizeof("<!DOCTYPE abc>") - 1, 3, LXA_HTML_OK},
        {"<!DOCTYPE abcd>", sizeof("<!DOCTYPE abcd>") - 1, 3, LXA_HTML_LIMIT},
        {"<?abc>", sizeof("<?abc>") - 1, 3, LXA_HTML_OK},
        {"<?abcd>", sizeof("<?abcd>") - 1, 3, LXA_HTML_LIMIT},
        {"<?abc xyz>", sizeof("<?abc xyz>") - 1, 6, LXA_HTML_OK},
        {"<?abc xyz>", sizeof("<?abc xyz>") - 1, 5, LXA_HTML_LIMIT},
        {"<?abc x?x>", sizeof("<?abc x?x>") - 1, 5, LXA_HTML_LIMIT},
        {"<?abc \xc3\xa9>", sizeof("<?abc \xc3\xa9>") - 1, 4, LXA_HTML_LIMIT},
        {"<?abc \0>", sizeof("<?abc \0>") - 1, 3, LXA_HTML_LIMIT},
        {"<?abc \xe2", sizeof("<?abc \xe2") - 1, 5, LXA_HTML_LIMIT}
    };
    lxa_allocator_t allocator = lxa_allocator_default();
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        for (size_t chunked = 0; chunked < 2; chunked++) {
            lxa_limits_t limits = lxa_limits_default();
            lxa_budget_t budget;
            lxa_html_tokenizer_t *tkz = NULL;
            fixture_t f = {0};
            lxa_html_result_t result = LXA_HTML_OK;
            f.fail_on = (lxa_html_token_kind_t) 255;
            limits.max_token_bytes = cases[i].maximum;
            CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
            CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
                on_error, &f, &budget, &tkz) == LXA_OK);
            for (size_t offset = 0;
                 result == LXA_HTML_OK && offset < cases[i].length;) {
                size_t length = chunked ? 1 : cases[i].length;
                result = lxa_html_tokenizer_feed(tkz, (lxa_span_t) {
                    (const uint8_t *) cases[i].input + offset, length
                });
                offset += length;
            }
            if (result == LXA_HTML_OK) {
                result = lxa_html_tokenizer_finish(tkz);
            }
            CHECK(result == cases[i].expected);
            if (result == LXA_HTML_LIMIT) {
                CHECK(lxa_html_tokenizer_limit_reason(tkz)
                      == LXA_BUDGET_TOKEN_BYTES
                      && lxa_html_tokenizer_finish(tkz) == LXA_HTML_LIMIT);
                lxa_html_tokenizer_reset(tkz);
                CHECK(lxa_html_tokenizer_feed(tkz, span("ok")) == LXA_HTML_OK
                      && lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
            }
            lxa_html_tokenizer_destroy(tkz);
            CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
        }
    }
}

static void
check_budget(void)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_limits_t limits = lxa_limits_default();
    lxa_budget_t budget;
    lxa_html_tokenizer_t *tkz = NULL;
    fixture_t f = {0};
    f.fail_on = LXA_HTML_PROCESSING_INSTRUCTION;

    limits.max_input_bytes = 2;
    CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("ab")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("cd")) == LXA_HTML_LIMIT);
    CHECK(lxa_html_tokenizer_limit_reason(tkz) == LXA_BUDGET_INPUT
          && budget.used[LXA_BUDGET_INPUT] == 2
          && lxa_html_tokenizer_finish(tkz) == LXA_HTML_LIMIT);
    lxa_html_tokenizer_destroy(tkz);
    CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("abcd")) == LXA_HTML_LIMIT);
    CHECK(lxa_html_tokenizer_limit_reason(tkz) == LXA_BUDGET_INPUT
          && budget.used[LXA_BUDGET_INPUT] == 2
          && budget.used[LXA_BUDGET_WORK] == 2);
    lxa_html_tokenizer_destroy(tkz);

    limits.max_input_bytes = 10;
    limits.initial_work_credit = 2;
    limits.work_per_input_byte = 1;
    limits.max_work = 2;
    CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("xy")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("z")) == LXA_HTML_LIMIT);
    CHECK(lxa_html_tokenizer_limit_reason(tkz) == LXA_BUDGET_WORK
          && budget.used[LXA_BUDGET_INPUT] == 3
          && budget.used[LXA_BUDGET_WORK] == 2);
    lxa_html_tokenizer_destroy(tkz);
    CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("xyz")) == LXA_HTML_LIMIT);
    CHECK(lxa_html_tokenizer_limit_reason(tkz) == LXA_BUDGET_WORK
          && budget.used[LXA_BUDGET_INPUT] == 3
          && budget.used[LXA_BUDGET_WORK] == 2);
    lxa_html_tokenizer_destroy(tkz);

    limits = lxa_limits_default();
    limits.max_attributes_per_element = 1;
    CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("<a x=1 y=2>"))
          == LXA_HTML_LIMIT);
    CHECK(lxa_html_tokenizer_limit_reason(tkz)
          == LXA_BUDGET_ATTRS_PER_ELEMENT);
    lxa_html_tokenizer_destroy(tkz);
    CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);

    limits.max_attributes_per_element = 2;
    CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("<a x=1 X=2 x=3>"))
          == LXA_HTML_LIMIT);
    CHECK(lxa_html_tokenizer_limit_reason(tkz)
          == LXA_BUDGET_ATTRS_PER_ELEMENT);
    lxa_html_tokenizer_destroy(tkz);
    CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);

    limits = lxa_limits_default();
    limits.max_token_bytes = 6;
    CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_OK);
    CHECK(lxa_html_tokenizer_feed(tkz,
          span("<!DOCTYPE html PUBLIC 'a' 'bc'>")) == LXA_HTML_LIMIT);
    CHECK(lxa_html_tokenizer_limit_reason(tkz) == LXA_BUDGET_TOKEN_BYTES);
    lxa_html_tokenizer_destroy(tkz);
    CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);

    limits = lxa_limits_default();
    limits.max_name_bytes = 2;
    CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("<a xyz=1>"))
          == LXA_HTML_LIMIT);
    CHECK(lxa_html_tokenizer_limit_reason(tkz) == LXA_BUDGET_NAME_BYTES);
    lxa_html_tokenizer_destroy(tkz);
    CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);

    limits = lxa_limits_default();
    limits.initial_work_credit = 3;
    limits.work_per_input_byte = 0;
    limits.max_work = 3;
    CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("&amp;")) == LXA_HTML_LIMIT);
    CHECK(lxa_html_tokenizer_limit_reason(tkz) == LXA_BUDGET_WORK
          && budget.used[LXA_BUDGET_WORK] == 3);
    lxa_html_tokenizer_destroy(tkz);

    {
        static const char html[] = "<a x='abcdefgh'>";
        size_t split, j;
        limits = lxa_limits_default();
        limits.max_work = limits.initial_work_credit = 14;
        limits.work_per_input_byte = 0;
        for (split = 0; split < 2; split++) {
            CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
            CHECK(lxa_html_tokenizer_create_with_budget(&allocator,
                  on_token, on_error, &f, &budget, &tkz) == LXA_OK);
            if (split == 0) {
                CHECK(lxa_html_tokenizer_feed(tkz, span(html))
                      == LXA_HTML_LIMIT);
            }
            else {
                for (j = 0; j < sizeof(html) - 1; j++) {
                    lxa_span_t byte = {(const uint8_t *) html + j, 1};
                    if (lxa_html_tokenizer_feed(tkz, byte)
                        == LXA_HTML_LIMIT) break;
                }
                CHECK(j < sizeof(html) - 1);
            }
            CHECK(lxa_html_tokenizer_limit_reason(tkz) == LXA_BUDGET_WORK
                  && budget.used[LXA_BUDGET_WORK] == 14
                  && budget.used[LXA_BUDGET_INPUT] == 11);
            lxa_html_tokenizer_destroy(tkz);
            CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
        }
    }

    {
        static const char html[] = "a\r\n<!--b-->&amp;<A/>x";
        const size_t length = sizeof(html) - 1;
        uint64_t expected_work = 0;
        size_t split;
        limits.max_input_bytes = length;
        limits.initial_work_credit = 100;
        limits.work_per_input_byte = 1;
        limits.max_work = 100;
        for (split = 0; split <= length; split++) {
            lxa_span_t first = {(const uint8_t *) html, split};
            lxa_span_t second = {(const uint8_t *) html + split,
                                 length - split};
            CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
            CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
                  on_error, &f, &budget, &tkz) == LXA_OK);
            CHECK(lxa_html_tokenizer_feed(tkz, first) == LXA_HTML_OK);
            CHECK(lxa_html_tokenizer_feed(tkz, second) == LXA_HTML_OK);
            CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
            CHECK(budget.used[LXA_BUDGET_INPUT] == length);
            if (split == 0) {
                expected_work = budget.used[LXA_BUDGET_WORK];
            }
            CHECK(budget.used[LXA_BUDGET_WORK] == expected_work);
            lxa_html_tokenizer_destroy(tkz);
            tkz = NULL;
            f.count = 0;
        }
    }
    limits = lxa_limits_default();
    limits.max_token_bytes = 3;
    CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("abc")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("d")) == LXA_HTML_LIMIT);
    CHECK(lxa_html_tokenizer_limit_reason(tkz) == LXA_BUDGET_TOKEN_BYTES
          && lxa_html_tokenizer_finish(tkz) == LXA_HTML_LIMIT);
    lxa_html_tokenizer_reset(tkz);
    CHECK(lxa_html_tokenizer_limit_reason(tkz) == LXA_BUDGET_COUNT);
    CHECK(lxa_html_tokenizer_feed(tkz, span("ok")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    lxa_html_tokenizer_destroy(tkz);
    tkz = NULL;
    f.count = 0;

    limits = lxa_limits_default();
    limits.max_name_bytes = 3;
    CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("<Ab")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("cd>")) == LXA_HTML_LIMIT);
    CHECK(lxa_html_tokenizer_limit_reason(tkz) == LXA_BUDGET_NAME_BYTES
          && lxa_html_tokenizer_finish(tkz) == LXA_HTML_LIMIT);
    lxa_html_tokenizer_reset(tkz);
    CHECK(lxa_html_tokenizer_feed(tkz, span("<Abc>")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK
          && f.kind[0] == LXA_HTML_START_TAG
          && strcmp(f.bytes[0], "abc") == 0);
    lxa_html_tokenizer_destroy(tkz);

    limits = lxa_limits_default();
    CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_OK);
    limits.max_memory_bytes = budget.used[LXA_BUDGET_MEMORY] + 16;
    lxa_html_tokenizer_destroy(tkz);
    CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("ab")) == LXA_HTML_OK
          && budget.used[LXA_BUDGET_MEMORY] == limits.max_memory_bytes);
    lxa_html_tokenizer_reset(tkz);
    CHECK(budget.used[LXA_BUDGET_INPUT] == 2
          && budget.used[LXA_BUDGET_MEMORY] == limits.max_memory_bytes);
    CHECK(lxa_html_tokenizer_feed(tkz, span("<B>")) == LXA_HTML_LIMIT);
    CHECK(lxa_html_tokenizer_limit_reason(tkz) == LXA_BUDGET_MEMORY
          && lxa_html_tokenizer_finish(tkz) == LXA_HTML_LIMIT
          && budget.used[LXA_BUDGET_MEMORY] == limits.max_memory_bytes);
    lxa_html_tokenizer_destroy(tkz);
    CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);

    limits.max_memory_bytes = 0;
    CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    tkz = NULL;
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_ERROR_LIMIT
          && tkz == NULL && budget.used[LXA_BUDGET_MEMORY] == 0);

    {
        allocation_t failing_state = {0};
        lxa_allocator_t failing_allocator = {
            &failing_state, allocate, resize, release
        };
        uint64_t retained;
        limits = lxa_limits_default();
        CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CHECK(lxa_html_tokenizer_create_with_budget(
              &failing_allocator, on_token, on_error, &f, &budget,
              &tkz) == LXA_OK);
        retained = budget.used[LXA_BUDGET_MEMORY];
        failing_state.fail_growth = true;
        CHECK(lxa_html_tokenizer_feed(tkz, span("x")) == LXA_HTML_NO_MEMORY);
        CHECK(budget.used[LXA_BUDGET_MEMORY] == retained);
        lxa_html_tokenizer_destroy(tkz);
        CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    }
}

typedef struct {
    lxa_html_token_kind_t kind[16];
    char bytes[16][192];
    size_t lengths[16];
    char pi_data[16][96];
    char system_ids[16][96], public_ids[16][96];
    bool has_system_id[16], has_public_id[16];
    char attribute_names[16][4][64], attribute_values[16][4][96];
    size_t attribute_count[16];
    bool self_closing[16];
    bool force_quirks[16];
    size_t count, errors, error_offset;
    bool nul_error;
} snapshot_t;

static snapshot_t
snapshot(const fixture_t *f)
{
    snapshot_t result = {0};
    size_t i;
    result.errors = f->errors;
    result.error_offset = f->error_offset;
    result.nul_error = f->error_code != NULL
                       && strcmp(f->error_code,
                                 "unexpected-null-character") == 0;
    for (i = 0; i < f->count; i++) {
        size_t j = result.count;
        if (j != 0 && f->kind[i] == LXA_HTML_TEXT
            && result.kind[j - 1] == LXA_HTML_TEXT) {
            j--;
        }
        else {
            CHECK(j < 16);
            result.kind[j] = f->kind[i];
            result.self_closing[j] = f->self_closing[i];
            result.force_quirks[j] = f->force_quirks[i];
            result.has_system_id[j] = f->has_system_id[i];
            result.has_public_id[j] = f->has_public_id[i];
            memcpy(result.public_ids[j], f->public_ids[i],
                   sizeof(result.public_ids[j]));
            memcpy(result.system_ids[j], f->system_ids[i],
                   sizeof(result.system_ids[j]));
            result.attribute_count[j] = f->attribute_count[i];
            memcpy(result.attribute_names[j], f->attribute_names[i],
                   sizeof(result.attribute_names[j]));
            memcpy(result.attribute_values[j], f->attribute_values[i],
                   sizeof(result.attribute_values[j]));
            result.count++;
        }
        CHECK(f->lengths[i] < sizeof(result.bytes[j]) - result.lengths[j]);
        memcpy(result.bytes[j] + result.lengths[j], f->bytes[i],
               f->lengths[i]);
        result.lengths[j] += f->lengths[i];
        result.bytes[j][result.lengths[j]] = 0;
        if (f->kind[i] == LXA_HTML_PROCESSING_INSTRUCTION) {
            memcpy(result.pi_data[j], f->pi_data[i],
                   sizeof(result.pi_data[j]));
        }
    }
    return result;
}

static void
check_partition_mode(lxa_html_tokenizer_t *tkz, fixture_t *f,
                     const char *input, size_t length, bool allow_cdata,
                     bool foreign_feedback)
{
    lxa_span_t whole = {(const uint8_t *) input, length};
    snapshot_t expected, actual;
    size_t split, attempt;
    uint32_t seed = 0x1782ec49;

    lxa_html_tokenizer_reset(tkz);
    memset(f, 0, sizeof(*f));
    f->fail_on = (lxa_html_token_kind_t) 255;
    f->foreign_feedback = foreign_feedback;
    CHECK(lxa_html_tokenizer_allow_cdata(tkz, allow_cdata) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, whole) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    expected = snapshot(f);

    for (split = 0; split <= length; split++) {
        lxa_span_t first = {whole.data, split};
        lxa_span_t second = {whole.data + split, length - split};
        lxa_html_tokenizer_reset(tkz);
        memset(f, 0, sizeof(*f));
        f->fail_on = (lxa_html_token_kind_t) 255;
        f->foreign_feedback = foreign_feedback;
        CHECK(lxa_html_tokenizer_allow_cdata(tkz, allow_cdata) == LXA_HTML_OK);
        CHECK(lxa_html_tokenizer_feed(tkz, first) == LXA_HTML_OK);
        CHECK(lxa_html_tokenizer_feed(tkz, second) == LXA_HTML_OK);
        CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
        actual = snapshot(f);
        CHECK(memcmp(&expected, &actual, sizeof(expected)) == 0);
    }

    for (attempt = 0; attempt < 32; attempt++) {
        size_t pos = 0;
        lxa_html_tokenizer_reset(tkz);
        memset(f, 0, sizeof(*f));
        f->fail_on = (lxa_html_token_kind_t) 255;
        f->foreign_feedback = foreign_feedback;
        CHECK(lxa_html_tokenizer_allow_cdata(tkz, allow_cdata) == LXA_HTML_OK);
        while (pos < length) {
            size_t size;
            lxa_span_t part;
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            size = 1 + seed % 7;
            if (size > length - pos) {
                size = length - pos;
            }
            part.data = whole.data + pos;
            part.length = size;
            CHECK(lxa_html_tokenizer_feed(tkz, part) == LXA_HTML_OK);
            pos += size;
        }
        CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
        actual = snapshot(f);
        CHECK(memcmp(&expected, &actual, sizeof(expected)) == 0);
    }
}

static void
check_partition(lxa_html_tokenizer_t *tkz, fixture_t *f,
                const char *input, size_t length)
{
    check_partition_mode(tkz, f, input, length, false, false);
}

static void
check_prepaid_input(void)
{
    lxa_limits_t limits = lxa_limits_default();
    lxa_budget_t budget;
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_html_tokenizer_t *tkz = NULL;
    fixture_t f = {0};
    static const uint8_t legacy[] = "<meta charset=windows-1252>\x80";
    lxa_span_t raw = {legacy, sizeof(legacy) - 1};
    lxa_buffer_t decoded;
    const lxa_encoding_t *encoding;
    size_t skip, malformed;
    uint64_t credit;
    f.fail_on = LXA_HTML_PROCESSING_INSTRUCTION;
    limits.max_input_bytes = raw.length;
    CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    CHECK(lxa_buffer_init(&decoded, &allocator) == LXA_OK
          && lxa_buffer_bind_budget(&decoded, &budget) == LXA_OK);
    CHECK(lxa_html_input_select(raw, raw.length, &encoding, &skip)
          == LXA_HTML_INPUT_OK);
    CHECK(lxa_html_input_decode(raw, encoding, skip, &budget,
                                 &decoded, &malformed, NULL) == LXA_HTML_INPUT_OK
          && malformed == 0 && decoded.length == raw.length + 2);
    credit = budget.work_credit;
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_OK);
    CHECK(lxa_html_tokenizer_feed_prepaid(
          tkz, (lxa_span_t) {decoded.data, decoded.length}) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK
          && f.count == 3 && f.kind[1] == LXA_HTML_TEXT
          && strcmp(f.bytes[1], "\xe2\x82\xac") == 0);
    CHECK(budget.used[LXA_BUDGET_INPUT] == raw.length
          && budget.work_credit == credit);
    lxa_html_tokenizer_destroy(tkz);
    lxa_buffer_destroy(&decoded);
    CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    {
        /* HTML input uses UTF-8 by default. The decoder reports the raw
         * malformed byte once; prepaid tokenization sees only U+FFFD. */
        static const uint8_t page[] =
            "<template><table>x\xe2<tr><td>q</td></tr></table></template>";
        size_t raw_error_at = SIZE_MAX;
        lxa_status_t callback_status = LXA_OK;
        lxa_limit_reason_t reason = LXA_BUDGET_COUNT;
        raw = (lxa_span_t) {page, sizeof(page) - 1};
        limits = lxa_limits_default();
        limits.max_input_bytes = raw.length;
        CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CHECK(lxa_buffer_init(&decoded, &allocator) == LXA_OK
              && lxa_buffer_bind_budget(&decoded, &budget) == LXA_OK);
        CHECK(lxa_html_input_select(raw, limits.max_input_bytes,
                                    &encoding, &skip) == LXA_HTML_INPUT_OK
              && encoding == lxa_encoding_lookup(span("utf-8"))
              && skip == 0);
        CHECK(lxa_html_input_decode_report(raw, encoding, skip, &budget,
              &decoded, &malformed, &reason, on_raw_malformed, &raw_error_at,
              &callback_status) == LXA_HTML_INPUT_OK
              && malformed == 1 && reason == LXA_BUDGET_COUNT
              && callback_status == LXA_OK
              && raw_error_at ==
                 (size_t) ((const uint8_t *) memchr(page, 0xe2, raw.length)
                           - page));
        credit = budget.work_credit;
        memset(&f, 0, sizeof(f));
        f.fail_on = LXA_HTML_PROCESSING_INSTRUCTION;
        CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
              on_error, &f, &budget, &tkz) == LXA_OK);
        CHECK(lxa_html_tokenizer_feed_prepaid(tkz,
              (lxa_span_t) {decoded.data, decoded.length}) == LXA_HTML_OK
              && lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
        CHECK(f.count == 11 && f.kind[2] == LXA_HTML_TEXT
              && strcmp(f.bytes[2], "x\xef\xbf\xbd") == 0 && f.errors == 0
              && budget.used[LXA_BUDGET_INPUT] == raw.length
              && budget.work_credit == credit);
        lxa_html_tokenizer_destroy(tkz);
        lxa_buffer_destroy(&decoded);
        CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    }
    limits = lxa_limits_default();
    limits.max_input_bytes = 1;
    limits.initial_work_credit = 0;
    limits.work_per_input_byte = 1;
    CHECK(lxa_budget_init(&budget, &limits) == LXA_OK
          && lxa_budget_accept_input(&budget, 1, NULL) == LXA_OK);
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_OK);
    CHECK(lxa_html_tokenizer_feed_prepaid(tkz, span("ab")) == LXA_HTML_LIMIT
          && lxa_html_tokenizer_limit_reason(tkz) == LXA_BUDGET_WORK
          && budget.used[LXA_BUDGET_INPUT] == 1
          && budget.work_credit == 1
          && lxa_html_tokenizer_finish(tkz) == LXA_HTML_LIMIT);
    lxa_html_tokenizer_destroy(tkz);
    CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    f.count = 0;
    limits.max_input_bytes = 2;
    limits.initial_work_credit = 16;
    limits.work_per_input_byte = 1;
    CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    CHECK(lxa_budget_accept_input(&budget, 1, NULL) == LXA_OK);
    credit = budget.work_credit;
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_OK);
    CHECK(lxa_html_tokenizer_feed_prepaid(tkz, span("\xe2\x82\xac"))
          == LXA_HTML_OK);
    CHECK(budget.used[LXA_BUDGET_INPUT] == 1
          && budget.work_credit == credit
          && budget.used[LXA_BUDGET_WORK] >= 3);
    CHECK(lxa_html_tokenizer_feed(tkz, span("x")) == LXA_HTML_BAD_ARGUMENT);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 2 && strcmp(f.bytes[0], "\xe2\x82\xac") == 0);
    lxa_html_tokenizer_reset(tkz);
    f.count = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("x")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_feed_prepaid(tkz, span("y"))
          == LXA_HTML_BAD_ARGUMENT);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(budget.used[LXA_BUDGET_INPUT] == 2
          && f.count == 2 && strcmp(f.bytes[0], "x") == 0);
    lxa_html_tokenizer_destroy(tkz);
    CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_OK);
    lxa_html_tokenizer_destroy(tkz);
    CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
    CHECK(lxa_html_tokenizer_create_with_budget(&allocator, on_token,
          on_error, &f, &budget, &tkz) == LXA_OK);
    CHECK(lxa_html_tokenizer_feed_prepaid(tkz, span("x"))
          == LXA_HTML_BAD_ARGUMENT);
    lxa_html_tokenizer_destroy(tkz);
}

int
main(void)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_html_tokenizer_t *tkz = NULL;
    fixture_t f = {0};
    char chunk[] = "he";
    static const char nul_text[] = {'a', '\0', 'b'};
    static const char partition_html[] =
        "a\r\n<!--x--y-\xc3\xa9\r\nz-->b&amp;&#38;<A\r\n/>q\xe2z";
    allocation_t state = {0};
    lxa_allocator_t failing = {&state, allocate, resize, release};
    lxa_span_t invalid = {NULL, 1};

    f.fail_on = LXA_HTML_PROCESSING_INSTRUCTION;
    CHECK(lxa_html_tokenizer_create(&allocator, NULL, on_error, &f,
                                     &tkz) == LXA_ERROR_ARGUMENT);
    CHECK(tkz == NULL);
    CHECK(lxa_html_tokenizer_create(&allocator, on_token, on_error, &f,
                                     &tkz) == LXA_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("<Di")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("v>")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span(chunk)) == LXA_HTML_OK);
    chunk[0] = 'X'; /* No chunk may be retained across feed calls. */
    CHECK(lxa_html_tokenizer_feed(tkz, span("llo<br /")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("></DIV>")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 5 && f.errors == 0);
    CHECK(f.kind[0] == LXA_HTML_START_TAG && strcmp(f.bytes[0], "div") == 0);
    CHECK(f.kind[1] == LXA_HTML_TEXT && strcmp(f.bytes[1], "hello") == 0);
    CHECK(f.kind[2] == LXA_HTML_START_TAG && f.self_closing[2]
           && strcmp(f.bytes[2], "br") == 0);
    CHECK(f.kind[3] == LXA_HTML_END_TAG && strcmp(f.bytes[3], "div") == 0);
    CHECK(f.kind[4] == LXA_HTML_EOF);
    CHECK(lxa_html_tokenizer_feed(tkz, span("ignored")) == LXA_HTML_BAD_ARGUMENT);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_BAD_ARGUMENT);

    {
        char pending_attr[] = "<a x='&amp";
        char pending_script[] = "<script>a</scr";
        lxa_html_tokenizer_reset(tkz);
        memset(&f, 0, sizeof(f));
        f.fail_on = (lxa_html_token_kind_t) 255;
        CHECK(lxa_html_tokenizer_feed(tkz, span(pending_attr)) == LXA_HTML_OK);
        memset(pending_attr, 'X', sizeof(pending_attr) - 1);
        CHECK(lxa_html_tokenizer_feed(tkz, (lxa_span_t) {NULL, 0})
              == LXA_HTML_OK);
        CHECK(lxa_html_tokenizer_feed(tkz, span(";'>")) == LXA_HTML_OK);
        CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
        CHECK(f.count == 2 && f.kind[0] == LXA_HTML_START_TAG
              && f.attribute_count[0] == 1
              && strcmp(f.attribute_values[0][0], "&") == 0
              && f.errors == 0);
        lxa_html_tokenizer_reset(tkz);
        memset(&f, 0, sizeof(f));
        f.fail_on = (lxa_html_token_kind_t) 255;
        CHECK(lxa_html_tokenizer_feed(tkz, span(pending_script)) == LXA_HTML_OK);
        memset(pending_script, 'X', sizeof(pending_script) - 1);
        CHECK(lxa_html_tokenizer_feed(tkz, (lxa_span_t) {NULL, 0})
              == LXA_HTML_OK);
        CHECK(lxa_html_tokenizer_feed(tkz, span("ipt>b")) == LXA_HTML_OK);
        CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
        CHECK(f.count == 5 && f.kind[1] == LXA_HTML_TEXT
              && strcmp(f.bytes[1], "a") == 0
              && f.kind[2] == LXA_HTML_END_TAG
              && strcmp(f.bytes[2], "script") == 0
              && f.kind[3] == LXA_HTML_TEXT
              && strcmp(f.bytes[3], "b") == 0 && f.errors == 0);
    }

    lxa_html_tokenizer_reset(tkz);
    f.count = 0;
    {
        const char *html = "<a>ok</a>";
        size_t i;
        for (i = 0; html[i] != 0; i++) {
            lxa_span_t byte = {(const uint8_t *) html + i, 1};
            CHECK(lxa_html_tokenizer_feed(tkz, byte) == LXA_HTML_OK);
        }
    }
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 4 && f.kind[0] == LXA_HTML_START_TAG
           && f.kind[1] == LXA_HTML_TEXT && f.kind[2] == LXA_HTML_END_TAG
           && f.kind[3] == LXA_HTML_EOF
           && strcmp(f.bytes[1], "ok") == 0);

    lxa_html_tokenizer_reset(tkz);
    f.count = 0;
    {
        const char *html = "a<!--x--y-\xc3\xa9\r\nz-->b";
        size_t i;
        for (i = 0; html[i] != 0; i++) {
            lxa_span_t byte = {(const uint8_t *) html + i, 1};
            CHECK(lxa_html_tokenizer_feed(tkz, byte) == LXA_HTML_OK);
        }
    }
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 4 && f.kind[0] == LXA_HTML_TEXT
           && f.kind[1] == LXA_HTML_COMMENT
           && strcmp(f.bytes[1], "x--y-\xc3\xa9\nz") == 0
           && f.kind[2] == LXA_HTML_TEXT
           && strcmp(f.bytes[2], "b") == 0 && f.kind[3] == LXA_HTML_EOF);
    lxa_html_tokenizer_reset(tkz);
    f.count = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("<!---->")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
           && strcmp(f.bytes[0], "") == 0);
    lxa_html_tokenizer_reset(tkz);
    f.count = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("<!--\xe2"))
           == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("-->")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 2 && strcmp(f.bytes[0], "\xef\xbf\xbd") == 0
           && f.error_offset == 4);
    f.errors = 0;
    lxa_html_tokenizer_reset(tkz);
    f.count = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("<!--unfinished"))
           == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 2 && f.errors == 1
          && strcmp(f.bytes[0], "unfinished") == 0);
    lxa_html_tokenizer_reset(tkz);
    CHECK(lxa_html_tokenizer_feed(tkz, span("<!--x<y-->")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    lxa_html_tokenizer_reset(tkz);
    f.fail_on = LXA_HTML_COMMENT;
    CHECK(lxa_html_tokenizer_feed(tkz, span("<!--x-->"))
           == LXA_HTML_CALLBACK_FAILURE);
    CHECK(lxa_html_tokenizer_callback_status(tkz) == LXA_ERROR_NO_MEMORY);
    f.fail_on = LXA_HTML_PROCESSING_INSTRUCTION;

    lxa_html_tokenizer_reset(tkz);
    f.count = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("foo")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("bar")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_TEXT
           && strcmp(f.bytes[0], "foobar") == 0
           && f.kind[1] == LXA_HTML_EOF);

    lxa_html_tokenizer_reset(tkz);
    f.count = f.errors = 0;
    {
        const char *html = "a&#38;&#X1f600;&#00065;z";
        size_t i;
        for (i = 0; html[i] != 0; i++) {
            lxa_span_t byte = {(const uint8_t *) html + i, 1};
            CHECK(lxa_html_tokenizer_feed(tkz, byte) == LXA_HTML_OK);
        }
    }
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_TEXT
           && strcmp(f.bytes[0], "a&\xf0\x9f\x98\x80" "Az") == 0
           && f.errors == 0);
    lxa_html_tokenizer_reset(tkz);
    f.count = 0;
    {
        const char *html = "a&amp;&NotEqualTilde;"
                           "&CounterClockwiseContourIntegral;z";
        size_t i;
        for (i = 0; html[i] != 0; i++) {
            lxa_span_t byte = {(const uint8_t *) html + i, 1};
            CHECK(lxa_html_tokenizer_feed(tkz, byte) == LXA_HTML_OK);
        }
    }
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_TEXT
           && strcmp(f.bytes[0], "a&\xe2\x89\x82\xcc\xb8\xe2\x88\xb3z") == 0);
    lxa_html_tokenizer_reset(tkz);
    f.count = f.errors = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("&notAnEntity;"))
           == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 2 && f.errors == 1
          && strcmp(f.bytes[0], "\xc2\xac" "AnEntity;") == 0);
    lxa_html_tokenizer_reset(tkz);
    f.count = f.errors = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("&amp")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 2 && f.errors == 1 && strcmp(f.bytes[0], "&") == 0);
    lxa_html_tokenizer_reset(tkz);
    f.count = f.errors = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("&unknown;"))
           == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 2 && f.errors == 1
          && strcmp(f.bytes[0], "&unknown;") == 0);
    lxa_html_tokenizer_reset(tkz);
    f.count = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("a&")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK
          && strcmp(f.bytes[0], "a&") == 0);
    lxa_html_tokenizer_reset(tkz);
    f.count = f.errors = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("&#65")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 2 && f.errors == 1
          && strcmp(f.bytes[0], "A") == 0
          && strcmp(f.error_code,
                    "missing-semicolon-after-character-reference") == 0);

    lxa_html_tokenizer_reset(tkz);
    f.count = f.errors = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("<a")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 1 && f.kind[0] == LXA_HTML_EOF && f.errors == 1
          && strcmp(f.error_code, "eof-in-tag") == 0);
    CHECK(lxa_html_tokenizer_feed(tkz, span(">")) == LXA_HTML_BAD_ARGUMENT);
    lxa_html_tokenizer_reset(tkz);
    CHECK(lxa_html_tokenizer_feed(tkz, span("a&b")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    lxa_html_tokenizer_reset(tkz);
    f.count = f.errors = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("<a x>")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK
          && f.count == 2 && f.attribute_count[0] == 1
          && strcmp(f.attribute_names[0][0], "x") == 0
          && strcmp(f.attribute_values[0][0], "") == 0);
    lxa_html_tokenizer_reset(tkz);
    f.count = f.errors = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("\xc3")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("\xa9")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_TEXT
           && strcmp(f.bytes[0], "\xc3\xa9") == 0 && f.errors == 0);
    lxa_html_tokenizer_reset(tkz);
    f.count = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("\xe2")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 2 && strcmp(f.bytes[0], "\xef\xbf\xbd") == 0
           && f.errors == 1 && f.error_offset == 0);
    lxa_html_tokenizer_reset(tkz);
    f.count = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("xy\xe2")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("z")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 2 && strcmp(f.bytes[0], "xy\xef\xbf\xbdz") == 0
           && f.errors == 2 && f.error_offset == 2);
    lxa_html_tokenizer_reset(tkz);
    f.count = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("\xe2\xc3\xa9"))
           == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 2 && strcmp(f.bytes[0], "\xef\xbf\xbd\xc3\xa9") == 0
           && f.errors == 3 && f.error_offset == 0);
    lxa_html_tokenizer_reset(tkz);
    f.count = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("\xef\xbf\xbd")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 2 && strcmp(f.bytes[0], "\xef\xbf\xbd") == 0
           && f.errors == 3);
    lxa_html_tokenizer_reset(tkz);
    f.error_fail = true;
    CHECK(lxa_html_tokenizer_feed(tkz, span("\x80"))
           == LXA_HTML_CALLBACK_FAILURE);
    CHECK(lxa_html_tokenizer_callback_status(tkz) == LXA_ERROR_NO_MEMORY);
    f.error_fail = false;
    lxa_html_tokenizer_reset(tkz);
    f.count = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("a\r")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("\nb\rc")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 2 && strcmp(f.bytes[0], "a\nb\nc") == 0);
    lxa_html_tokenizer_reset(tkz);
    f.count = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("<A\r")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_feed(tkz, span("\n>")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_START_TAG
           && strcmp(f.bytes[0], "a") == 0);
    {
        static const char *inputs[] = {
            "<a x=\"a\r\nb\rc&#13;d\">", "<a x='a\r\nb\rc&#13;d'>"
        };
        for (size_t j = 0; j < sizeof(inputs) / sizeof(inputs[0]); j++) {
            check_partition(tkz, &f, inputs[j], strlen(inputs[j]));
            CHECK(f.count == 2 && f.kind[0] == LXA_HTML_START_TAG
                  && f.attribute_count[0] == 1 && f.errors == 1
                  && strcmp(f.error_code, "control-character-reference") == 0
                  && strcmp(f.attribute_values[0][0], "a\nb\nc\rd") == 0);
        }
    }
    lxa_html_tokenizer_reset(tkz);
    f.count = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("a\r<b>\r</b>\r"))
           == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.count == 6 && strcmp(f.bytes[0], "a\n") == 0
           && f.kind[1] == LXA_HTML_START_TAG
           && strcmp(f.bytes[2], "\n") == 0
           && f.kind[3] == LXA_HTML_END_TAG
           && strcmp(f.bytes[4], "\n") == 0
           && f.kind[5] == LXA_HTML_EOF);
    lxa_html_tokenizer_reset(tkz);
    CHECK(lxa_html_tokenizer_feed(tkz, invalid) == LXA_HTML_BAD_ARGUMENT);
    CHECK(lxa_html_tokenizer_feed(tkz, span("")) == LXA_HTML_OK);

    f.fail_on = LXA_HTML_START_TAG;
    CHECK(lxa_html_tokenizer_feed(tkz, span("<a>"))
           == LXA_HTML_CALLBACK_FAILURE);
    CHECK(lxa_html_tokenizer_callback_status(tkz) == LXA_ERROR_NO_MEMORY);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_CALLBACK_FAILURE);
    lxa_html_tokenizer_reset(tkz);
    f.fail_on = LXA_HTML_TEXT;
    CHECK(lxa_html_tokenizer_feed(tkz, span("x")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_CALLBACK_FAILURE);
    CHECK(lxa_html_tokenizer_callback_status(tkz) == LXA_ERROR_NO_MEMORY);
    CHECK(lxa_html_tokenizer_feed(tkz, span("y")) == LXA_HTML_CALLBACK_FAILURE);
    lxa_html_tokenizer_reset(tkz);
    f.fail_on = LXA_HTML_PROCESSING_INSTRUCTION;
    CHECK(lxa_html_tokenizer_callback_status(tkz) == LXA_OK);

    check_partition(tkz, &f, partition_html, sizeof(partition_html) - 1);
    check_partition(tkz, &f, "<title>x&amp;<b></TiTlE><i>y</i>",
                    sizeof("<title>x&amp;<b></TiTlE><i>y</i>") - 1);
    CHECK(f.count == 7 && f.kind[0] == LXA_HTML_START_TAG
          && f.kind[1] == LXA_HTML_TEXT
          && strcmp(f.bytes[1], "x&<b>") == 0
          && f.kind[2] == LXA_HTML_END_TAG
          && strcmp(f.bytes[2], "title") == 0 && f.errors == 0);
    check_partition(tkz, &f, "<style>x&amp;<i></stYle>z",
                    sizeof("<style>x&amp;<i></stYle>z") - 1);
    CHECK(f.count == 5 && f.kind[1] == LXA_HTML_TEXT
          && strcmp(f.bytes[1], "x&amp;<i>") == 0
          && f.kind[2] == LXA_HTML_END_TAG
          && strcmp(f.bytes[2], "style") == 0
          && strcmp(f.bytes[3], "z") == 0 && f.errors == 0);
    /* html5lib-tests/tokenizer/escapeFlag.test: bogus comment-like text
     * remains literal in RAWTEXT until an appropriate end tag. */
    check_partition(tkz, &f, "<style>foo<!--</style>--></style>",
                    sizeof("<style>foo<!--</style>--></style>") - 1);
    CHECK(f.count == 6 && f.kind[1] == LXA_HTML_TEXT
          && strcmp(f.bytes[1], "foo<!--") == 0
          && f.kind[2] == LXA_HTML_END_TAG
          && strcmp(f.bytes[3], "-->") == 0
          && f.kind[4] == LXA_HTML_END_TAG
          && f.errors == 0);
    check_partition(tkz, &f, "<title> &amp; <!-- &amp; --> &amp; </title>",
                    sizeof("<title> &amp; <!-- &amp; --> &amp; </title>") - 1);
    CHECK(f.count == 4 && f.kind[1] == LXA_HTML_TEXT
          && strcmp(f.bytes[1], " & <!-- & --> & ") == 0
          && f.errors == 0);
    check_partition(tkz, &f, "<plaintext>x&amp;<b>\r\n\xe2",
                    sizeof("<plaintext>x&amp;<b>\r\n\xe2") - 1);
    CHECK(f.count == 3 && f.kind[0] == LXA_HTML_START_TAG
          && strcmp(f.bytes[0], "plaintext") == 0
          && f.kind[1] == LXA_HTML_TEXT
          && strcmp(f.bytes[1], "x&amp;<b>\n\xef\xbf\xbd") == 0
          && f.errors == 1 && strcmp(f.error_code, "invalid-utf-8") == 0);
    check_partition(tkz, &f, "<script>a&amp;<div></scriptX>b</ScRiPt>z",
                    sizeof("<script>a&amp;<div></scriptX>b</ScRiPt>z") - 1);
    CHECK(f.count == 5 && f.kind[1] == LXA_HTML_TEXT
          && strcmp(f.bytes[1], "a&amp;<div></scriptX>b") == 0
          && f.kind[2] == LXA_HTML_END_TAG
          && strcmp(f.bytes[2], "script") == 0
          && strcmp(f.bytes[3], "z") == 0 && f.errors == 0);
    check_partition(tkz, &f, "<script>a</scr",
                    sizeof("<script>a</scr") - 1);
    CHECK(f.count == 3 && strcmp(f.bytes[1], "a</scr") == 0);
    check_partition(tkz, &f, "<script>a\r\n\xe2</script>z",
                    sizeof("<script>a\r\n\xe2</script>z") - 1);
    CHECK(f.count == 5 && strcmp(f.bytes[1], "a\n\xef\xbf\xbd") == 0
          && f.errors == 1 && strcmp(f.error_code, "invalid-utf-8") == 0);
    check_partition(tkz, &f, "<script><!-- ignored <?> -->end</script>",
                    sizeof("<script><!-- ignored <?> -->end</script>") - 1);
    CHECK(f.count == 4 && strcmp(f.bytes[1],
                                 "<!-- ignored <?> -->end") == 0
          && f.errors == 0);
    check_partition(tkz, &f, "<script><!-- a</ScRiPt>z",
                    sizeof("<script><!-- a</ScRiPt>z") - 1);
    CHECK(f.count == 5 && strcmp(f.bytes[1], "<!-- a") == 0
          && f.kind[2] == LXA_HTML_END_TAG
          && strcmp(f.bytes[2], "script") == 0 && f.errors == 0);
    check_partition(tkz, &f, "<script><!--x\xe2",
                    sizeof("<script><!--x\xe2") - 1);
    CHECK(f.count == 3 && strcmp(f.bytes[1], "<!--x\xef\xbf\xbd") == 0
          && f.errors == 2
          && strcmp(f.error_code, "eof-in-script-html-comment-like-text") == 0);
    check_partition(tkz, &f,
          "<script><!-- <ScRiPt>ignored </script> -->tail</script>",
          sizeof("<script><!-- <ScRiPt>ignored </script> -->tail</script>") - 1);
    CHECK(f.count == 4 && strcmp(f.bytes[1],
          "<!-- <ScRiPt>ignored </script> -->tail") == 0
          && f.kind[2] == LXA_HTML_END_TAG && f.errors == 0);
    check_partition(tkz, &f, "<script><!-- <tag> -->x</script>",
                    sizeof("<script><!-- <tag> -->x</script>") - 1);
    CHECK(f.count == 4 && strcmp(f.bytes[1], "<!-- <tag> -->x") == 0
          && f.errors == 0);
    check_partition(tkz, &f, "<script><!--<script>z",
                    sizeof("<script><!--<script>z") - 1);
    CHECK(f.count == 3 && strcmp(f.bytes[1], "<!--<script>z") == 0
          && f.errors == 1
          && strcmp(f.error_code, "eof-in-script-html-comment-like-text") == 0);
    /* html5lib-tests/domjs.test: '<' is not a double-escape delimiter. */
    check_partition(tkz, &f, "<script><!--<script</script>-->",
                    sizeof("<script><!--<script</script>-->") - 1);
    CHECK(f.count == 5 && strcmp(f.bytes[1], "<!--<script") == 0
          && f.kind[2] == LXA_HTML_END_TAG
          && strcmp(f.bytes[2], "script") == 0
          && strcmp(f.bytes[3], "-->") == 0 && f.errors == 0);
    check_partition(tkz, &f, "<script><!--<tag>-->x",
                    sizeof("<script><!--<tag>-->x") - 1);
    CHECK(f.count == 3 && strcmp(f.bytes[1], "<!--<tag>-->x") == 0
          && f.errors == 0);
    check_partition(tkz, &f, "<script><!-",
                    sizeof("<script><!-") - 1);
    CHECK(f.count == 3 && strcmp(f.bytes[1], "<!-") == 0 && f.errors == 0);
    check_partition(tkz, &f, "<title>a</titleX>b</tit",
                    sizeof("<title>a</titleX>b</tit") - 1);
    CHECK(f.count == 3 && f.kind[1] == LXA_HTML_TEXT
          && strcmp(f.bytes[1], "a</titleX>b</tit") == 0);
    check_partition(tkz, &f, "<title>a\r\n\xc3</TiTlE>z",
                    sizeof("<title>a\r\n\xc3</TiTlE>z") - 1);
    CHECK(f.count == 5 && f.errors == 1
          && strcmp(f.bytes[1], "a\n\xef\xbf\xbd") == 0
          && strcmp(f.bytes[3], "z") == 0
          && f.error_offset == 10
          && strcmp(f.error_code, "invalid-utf-8") == 0);
    check_partition(tkz, &f, "<title>a</title\r\n>z",
                    sizeof("<title>a</title\r\n>z") - 1);
    CHECK(f.count == 5 && f.errors == 0
          && strcmp(f.bytes[1], "a") == 0
          && f.kind[2] == LXA_HTML_END_TAG
          && strcmp(f.bytes[3], "z") == 0);
    check_partition(tkz, &f, "<a HREF='/a' src=b disabled x=\"\">",
                    sizeof("<a HREF='/a' src=b disabled x=\"\">") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_START_TAG
          && f.attribute_count[0] == 4
          && strcmp(f.attribute_names[0][0], "href") == 0
          && strcmp(f.attribute_values[0][0], "/a") == 0
          && strcmp(f.attribute_names[0][1], "src") == 0
          && strcmp(f.attribute_values[0][1], "b") == 0
          && strcmp(f.attribute_names[0][2], "disabled") == 0
          && strcmp(f.attribute_values[0][2], "") == 0
          && strcmp(f.attribute_names[0][3], "x") == 0
          && strcmp(f.attribute_values[0][3], "") == 0
          && f.errors == 0);
    check_partition(tkz, &f, "<a href=x HREF='y'>",
                    sizeof("<a href=x HREF='y'>") - 1);
    CHECK(f.count == 2 && f.attribute_count[0] == 1 && f.errors == 1
          && strcmp(f.attribute_names[0][0], "href") == 0
          && strcmp(f.attribute_values[0][0], "x") == 0
          && strcmp(f.error_code, "duplicate-attribute") == 0);
    check_partition(tkz, &f, "<a src=1 href=x HREF='bad&bogus;' q=ok>",
                    sizeof("<a src=1 href=x HREF='bad&bogus;' q=ok>") - 1);
    CHECK(f.count == 2 && f.attribute_count[0] == 3 && f.errors == 2
          && strcmp(f.first_error_code, "duplicate-attribute") == 0
          && strcmp(f.error_code, "unknown-named-character-reference") == 0
          && strcmp(f.attribute_names[0][0], "src") == 0
          && strcmp(f.attribute_values[0][0], "1") == 0
          && strcmp(f.attribute_names[0][1], "href") == 0
          && strcmp(f.attribute_values[0][1], "x") == 0
          && strcmp(f.attribute_names[0][2], "q") == 0
          && strcmp(f.attribute_values[0][2], "ok") == 0);
    check_partition(tkz, &f, "<a \0=x \xef\xbf\xbd=y href=z>",
                    sizeof("<a \0=x \xef\xbf\xbd=y href=z>") - 1);
    CHECK(f.count == 2 && f.attribute_count[0] == 2 && f.errors == 2
          && strcmp(f.first_error_code, "unexpected-null-character") == 0
          && strcmp(f.error_code, "duplicate-attribute") == 0
          && strcmp(f.attribute_names[0][0], "\xef\xbf\xbd") == 0
          && strcmp(f.attribute_values[0][0], "x") == 0
          && strcmp(f.attribute_names[0][1], "href") == 0
          && strcmp(f.attribute_values[0][1], "z") == 0);
    check_partition(tkz, &f, "<a x=1 X", sizeof("<a x=1 X") - 1);
    CHECK(f.count == 1 && f.kind[0] == LXA_HTML_EOF && f.errors == 2
          && strcmp(f.first_error_code, "duplicate-attribute") == 0
          && strcmp(f.error_code, "eof-in-tag") == 0
          && f.error_offset == sizeof("<a x=1 X") - 1);
    check_partition(tkz, &f, "<a \xc3\xa9='\xc3\xa9' data-x=\xe2\x82\xac>",
                    sizeof("<a \xc3\xa9='\xc3\xa9' data-x=\xe2\x82\xac>") - 1);
    CHECK(f.count == 2 && f.attribute_count[0] == 2 && f.errors == 0
          && strcmp(f.attribute_names[0][0], "\xc3\xa9") == 0
          && strcmp(f.attribute_values[0][0], "\xc3\xa9") == 0
          && strcmp(f.attribute_values[0][1], "\xe2\x82\xac") == 0);
    check_partition(tkz, &f, "<a x='\xe2'>",
                    sizeof("<a x='\xe2'>") - 1);
    CHECK(f.count == 2 && f.attribute_count[0] == 1 && f.errors == 1
          && f.error_offset == 6
          && strcmp(f.error_code, "invalid-utf-8") == 0
          && strcmp(f.attribute_values[0][0], "\xef\xbf\xbd") == 0);
    check_partition(tkz, &f, "<a \xc3=x>",
                    sizeof("<a \xc3=x>") - 1);
    CHECK(f.count == 2 && f.attribute_count[0] == 1 && f.errors == 1
          && f.error_offset == 3
          && strcmp(f.attribute_names[0][0], "\xef\xbf\xbd") == 0);
    check_partition(tkz, &f, "<a href='x&amp;y&#x80;' title=\"&notit; &copy?\">",
                    sizeof("<a href='x&amp;y&#x80;' title=\"&notit; &copy?\">") - 1);
    CHECK(f.count == 2 && f.attribute_count[0] == 2 && f.errors == 2
          && strcmp(f.attribute_values[0][0], "x&y\xe2\x82\xac") == 0
          && strcmp(f.attribute_values[0][1],
                    "&notit; \xc2\xa9?") == 0);
    check_partition(tkz, &f, "<a x=&not= y=&amp z='&'>",
                    sizeof("<a x=&not= y=&amp z='&'>") - 1);
    CHECK(f.count == 2 && f.attribute_count[0] == 3
          && strcmp(f.attribute_values[0][0], "&not=") == 0
          && strcmp(f.attribute_values[0][1], "&") == 0
          && strcmp(f.attribute_values[0][2], "&") == 0
          && f.errors == 2);
    check_partition(tkz, &f, "<a x='&", sizeof("<a x='&") - 1);
    CHECK(f.count == 1 && f.kind[0] == LXA_HTML_EOF
          && f.errors == 1 && strcmp(f.error_code, "eof-in-tag") == 0);
    check_partition(tkz, &f, "<a x='y'z=v />", sizeof("<a x='y'z=v />") - 1);
    CHECK(f.count == 2 && f.attribute_count[0] == 2 && f.self_closing[0]
          && f.errors == 1
          && strcmp(f.error_code,
                    "missing-whitespace-between-attributes") == 0);
    check_partition(tkz, &f, "<a x=>", sizeof("<a x=>") - 1);
    CHECK(f.count == 2 && f.attribute_count[0] == 1 && f.errors == 1
          && strcmp(f.error_code, "missing-attribute-value") == 0);
    check_partition(tkz, &f, "</a x=y>", sizeof("</a x=y>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_END_TAG && f.errors == 1
          && strcmp(f.error_code, "end-tag-with-attributes") == 0);
    check_partition(tkz, &f, "<a x='unterminated",
                    sizeof("<a x='unterminated") - 1);
    CHECK(f.count == 1 && f.kind[0] == LXA_HTML_EOF && f.errors == 1
          && strcmp(f.error_code, "eof-in-tag") == 0);
    check_partition(tkz, &f, "&1a;", sizeof("&1a;") - 1);
    CHECK(f.count == 2 && strcmp(f.bytes[0], "&1a;") == 0
          && f.errors == 1 && f.error_offset == 3
          && strcmp(f.error_code, "unknown-named-character-reference") == 0);
    check_partition(tkz, &f, "<title>&0;</title>",
                    sizeof("<title>&0;</title>") - 1);
    CHECK(f.count == 4 && strcmp(f.bytes[1], "&0;") == 0
          && f.errors == 1 && f.error_offset == 9
          && strcmp(f.error_code, "unknown-named-character-reference") == 0);
    check_partition(tkz, &f, "<a x='&1;' y=&9;>",
                    sizeof("<a x='&1;' y=&9;>") - 1);
    CHECK(f.count == 2 && f.attribute_count[0] == 2 && f.errors == 2
          && strcmp(f.attribute_values[0][0], "&1;") == 0
          && strcmp(f.attribute_values[0][1], "&9;") == 0
          && strcmp(f.error_code, "unknown-named-character-reference") == 0);
    check_partition(tkz, &f, "&12 &3", sizeof("&12 &3") - 1);
    CHECK(f.count == 2 && strcmp(f.bytes[0], "&12 &3") == 0 && f.errors == 0);
    {
        static const char long_digits[] =
            "&1234567890123456789012345678901234567890;";
        check_partition(tkz, &f, long_digits, sizeof(long_digits) - 1);
        CHECK(f.count == 2 && strcmp(f.bytes[0], long_digits) == 0
              && f.errors == 1 && f.error_offset == sizeof(long_digits) - 2
              && strcmp(f.error_code, "unknown-named-character-reference") == 0);
    }
    {
        static const char named_refs[] =
            "a&notit; &amp &bogus; &NotEqualTilde;"
            "&CounterClockwiseContourIntegral;";
        check_partition(tkz, &f, named_refs, sizeof(named_refs) - 1);
        CHECK(f.count == 2 && f.errors == 3
              && strcmp(f.bytes[0],
                        "a\xc2\xac" "it; & &bogus; "
                        "\xe2\x89\x82\xcc\xb8\xe2\x88\xb3") == 0
              && strcmp(f.error_code, "unknown-named-character-reference") == 0);
    }
    {
        static const char long_name[] =
            "&abcdefghijklmnopqrstuvwxyzabcdefghijklmno;";
        check_partition(tkz, &f, long_name, sizeof(long_name) - 1);
        CHECK(f.count == 2 && strcmp(f.bytes[0], long_name) == 0
              && f.errors == 1 && f.error_offset == sizeof(long_name) - 2
              && strcmp(f.error_code, "unknown-named-character-reference") == 0);
        check_partition(tkz, &f, long_name, sizeof(long_name) - 2);
        CHECK(f.count == 2
              && memcmp(f.bytes[0], long_name, sizeof(long_name) - 2) == 0
              && f.bytes[0][sizeof(long_name) - 2] == '\0'
              && f.errors == 0);
        check_partition(tkz, &f, "<a x='&abcdefghijklmnopqrstuvwxyzabcdefghijklmno;'>",
                        sizeof("<a x='&abcdefghijklmnopqrstuvwxyzabcdefghijklmno;'>") - 1);
        CHECK(f.count == 2 && f.attribute_count[0] == 1
              && strcmp(f.attribute_values[0][0], long_name) == 0
              && f.errors == 1
              && strcmp(f.error_code, "unknown-named-character-reference") == 0);
    }
    check_partition(tkz, &f, "&notabcdefghijklmnopqrstuvwxyzabcdefghijklmno;",
                    sizeof("&notabcdefghijklmnopqrstuvwxyzabcdefghijklmno;") - 1);
    CHECK(f.count == 2 && strcmp(f.bytes[0],
          "\xc2\xac" "abcdefghijklmnopqrstuvwxyzabcdefghijklmno;") == 0
          && f.errors == 1 && f.error_offset == 4
          && strcmp(f.error_code,
                    "missing-semicolon-after-character-reference") == 0);
    {
        static const char *formats[] = {
            "<a x=\"%s\">", "<a x='%s'>", "<a x=%s>"
        };
        static const size_t suffixes[] = {27, 28, 29, 40};
        char literal[64], input[96];
        /* WHATWG named character reference state: an attribute's legacy
         * match followed by alphanumerics returns to the value state. */
        for (size_t n = 0; n < sizeof(suffixes) / sizeof(suffixes[0]); n++) {
            memcpy(literal, "&not", 4);
            memset(literal + 4, 'a', suffixes[n]);
            literal[4 + suffixes[n]] = ';';
            literal[5 + suffixes[n]] = '\0';
            for (size_t j = 0; j < sizeof(formats) / sizeof(formats[0]); j++) {
                int length = snprintf(input, sizeof(input), formats[j], literal);
                CHECK(length > 0 && (size_t) length < sizeof(input));
                check_partition(tkz, &f, input, (size_t) length);
                CHECK(f.count == 2 && f.kind[0] == LXA_HTML_START_TAG
                      && f.attribute_count[0] == 1 && f.errors == 0
                      && strcmp(f.attribute_values[0][0], literal) == 0);
            }
        }
    }
    check_partition(tkz, &f, "&noti;", sizeof("&noti;") - 1);
    CHECK(f.count == 2 && strcmp(f.bytes[0], "\xc2\xac" "i;") == 0
          && f.errors == 1 && f.error_offset == 4
          && strcmp(f.error_code,
                    "missing-semicolon-after-character-reference") == 0);
    /* html5lib entities.test: a legacy name before '=' stays literal in
     * an attribute; unquoted syntax still reports its separate '=' error. */
    check_partition(tkz, &f, "<h a=\"&not=\">",
                    sizeof("<h a=\"&not=\">") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_START_TAG
          && f.attribute_count[0] == 1
          && strcmp(f.attribute_values[0][0], "&not=") == 0
          && f.errors == 0);
    check_partition(tkz, &f, "<h a=&not=>",
                    sizeof("<h a=&not=>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_START_TAG
          && f.attribute_count[0] == 1
          && strcmp(f.attribute_values[0][0], "&not=") == 0
          && f.errors == 1
          && strcmp(f.error_code,
                    "unexpected-character-in-unquoted-attribute-value") == 0);
    check_partition(tkz, &f, "a& b&&<br>", sizeof("a& b&&<br>") - 1);
    CHECK(f.count == 3 && strcmp(f.bytes[0], "a& b&&") == 0
          && f.kind[1] == LXA_HTML_START_TAG && f.kind[2] == LXA_HTML_EOF);
    check_partition(tkz, &f, "x<1&foo;", sizeof("x<1&foo;") - 1);
    CHECK(f.count == 3 && f.errors == 2
          && strcmp(f.bytes[0], "x") == 0
          && strcmp(f.bytes[1], "<1&foo;") == 0
          && f.error_offset == 7
          && strcmp(f.error_code, "unknown-named-character-reference") == 0);
    check_partition(tkz, &f, "<\xc3\xa9>", sizeof("<\xc3\xa9>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_TEXT
          && strcmp(f.bytes[0], "<\xc3\xa9>") == 0
          && f.errors == 1 && f.error_offset == 1
          && strcmp(f.error_code, "invalid-first-character-of-tag-name") == 0);
    check_partition(tkz, &f, "a</>b", sizeof("a</>b") - 1);
    CHECK(f.count == 3 && f.errors == 1
          && strcmp(f.bytes[0], "a") == 0
          && strcmp(f.bytes[1], "b") == 0 && f.error_offset == 3
          && strcmp(f.error_code, "missing-end-tag-name") == 0);
    check_partition(tkz, &f, "</7&x>\r\nz", sizeof("</7&x>\r\nz") - 1);
    CHECK(f.count == 3 && f.kind[0] == LXA_HTML_COMMENT
          && strcmp(f.bytes[0], "7&x") == 0
          && strcmp(f.bytes[1], "\nz") == 0
          && f.errors == 1 && f.error_offset == 2
          && strcmp(f.error_code, "invalid-first-character-of-tag-name") == 0);
    {
        static const char bogus[] = {'<', '/', '1', '\0', '\r', '\n',
                                     '\xc2', '\x85'};
        check_partition(tkz, &f, bogus, sizeof(bogus));
        CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
              && strcmp(f.bytes[0], "1\xef\xbf\xbd\n\xc2\x85") == 0
              && f.errors == 3 && f.error_offset == 6
              && strcmp(f.error_code, "control-character-in-input-stream") == 0);
    }
    {
        const char *bogus_utf8[] = {"</1\xe2>", "</1\xe2"};
        size_t j;
        for (j = 0; j < sizeof(bogus_utf8) / sizeof(bogus_utf8[0]); j++) {
            check_partition(tkz, &f, bogus_utf8[j], strlen(bogus_utf8[j]));
            CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
                  && strcmp(f.bytes[0], "1\xef\xbf\xbd") == 0
                  && f.errors == 2 && f.error_offset == 3
                  && strcmp(f.error_code, "invalid-utf-8") == 0);
        }
    }
    {
        const char *bad_declarations[] = {
            "<!x>", "<!-x>", "<!", "<!-", "<!DOC>", "<!DoC",
            "<![CDAXx>", "<![CDA"
        };
        const char *comments[] = {
            "x", "-x", "", "-", "DOC", "DoC", "[CDAXx", "[CDA"
        };
        size_t j;
        for (j = 0; j < sizeof(bad_declarations)
                        / sizeof(bad_declarations[0]); j++) {
            check_partition(tkz, &f, bad_declarations[j],
                            strlen(bad_declarations[j]));
            CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
                  && strcmp(f.bytes[0], comments[j]) == 0
                  && f.errors == 1
                  && f.error_offset == 2
                  && strcmp(f.error_code, "incorrectly-opened-comment") == 0);
        }
    }
    {
        const char *control_declarations[] = {
            "<!DO\x01>", "<![CD\x7f>", "<!-\x01>"
        };
        size_t j;
        for (j = 0; j < sizeof(control_declarations)
                        / sizeof(control_declarations[0]); j++) {
            check_partition(tkz, &f, control_declarations[j],
                            strlen(control_declarations[j]));
            CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
                  && f.errors == 2
                  && strcmp(f.first_error_code,
                            "incorrectly-opened-comment") == 0
                  && strcmp(f.error_code,
                            "control-character-in-input-stream") == 0
                  && f.error_offset == (j == 2 ? 3 : j == 0 ? 4 : 5));
        }
    }
    {
        const char *empty_doctype[] = {
            "<!DOCTYPE>", "<!dOcTyPe \t>", "<!DOCTYPE", "<!DOCTYPE "
        };
        size_t j;
        for (j = 0; j < sizeof(empty_doctype) / sizeof(empty_doctype[0]); j++) {
            check_partition(tkz, &f, empty_doctype[j],
                            strlen(empty_doctype[j]));
            CHECK(f.count == 2 && f.kind[0] == LXA_HTML_DOCTYPE
                  && strcmp(f.bytes[0], "") == 0 && f.force_quirks[0]
                  && f.errors == 1
                  && f.error_offset == strlen(empty_doctype[j])
                     - (j < 2 ? 1 : 0)
                  && strcmp(f.error_code, j < 2 ? "missing-doctype-name"
                                                : "eof-in-doctype") == 0);
        }
    }
    check_partition(tkz, &f, "<!Doctype hTmL>",
                    sizeof("<!Doctype hTmL>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_DOCTYPE
          && strcmp(f.bytes[0], "html") == 0
          && !f.force_quirks[0] && f.errors == 0);
    /* html5lib test2.test: PUBLIC at EOF, before and inside its identifier. */
    check_partition(tkz, &f, "<!DOCTYPE html PUBLIC",
                    sizeof("<!DOCTYPE html PUBLIC") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_DOCTYPE
          && strcmp(f.bytes[0], "html") == 0 && f.force_quirks[0]
          && !f.has_public_id[0] && f.errors == 1
          && strcmp(f.error_code, "eof-in-doctype") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html PUBLIC 'x",
                    sizeof("<!DOCTYPE html PUBLIC 'x") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_DOCTYPE
          && strcmp(f.bytes[0], "html") == 0 && f.force_quirks[0]
          && f.has_public_id[0] && strcmp(f.public_ids[0], "x") == 0
          && f.errors == 1
          && strcmp(f.error_code, "eof-in-doctype") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html unexpected>tail",
                    sizeof("<!DOCTYPE html unexpected>tail") - 1);
    CHECK(f.count == 3 && f.kind[0] == LXA_HTML_DOCTYPE
          && strcmp(f.bytes[0], "html") == 0 && f.force_quirks[0]
          && strcmp(f.bytes[1], "tail") == 0
          && f.errors == 1 && f.error_offset == 15
          && strcmp(f.error_code,
                    "invalid-character-sequence-after-doctype-name") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html x\0", sizeof("<!DOCTYPE html x\0") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_DOCTYPE
          && strcmp(f.bytes[0], "html") == 0 && f.force_quirks[0]
          && f.errors == 2 && f.error_offset == 16
          && strcmp(f.error_code, "unexpected-null-character") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html x",
                    sizeof("<!DOCTYPE html x") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_DOCTYPE
          && strcmp(f.bytes[0], "html") == 0 && f.force_quirks[0]
          && f.errors == 1);
    check_partition(tkz, &f, "<!DOCTYPE html prelude>tail",
                    sizeof("<!DOCTYPE html prelude>tail") - 1);
    CHECK(f.count == 3 && f.kind[0] == LXA_HTML_DOCTYPE
          && strcmp(f.bytes[0], "html") == 0 && f.force_quirks[0]
          && strcmp(f.bytes[1], "tail") == 0
          && f.errors == 1 && f.error_offset == 15
          && strcmp(f.error_code,
                    "invalid-character-sequence-after-doctype-name") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html PuBl",
                    sizeof("<!DOCTYPE html PuBl") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_DOCTYPE
          && f.force_quirks[0] && f.errors == 1 && f.error_offset == 15
          && strcmp(f.error_code,
                    "invalid-character-sequence-after-doctype-name") == 0);
    check_partition(tkz, &f,
          "<!DOCTYPE html PuBlIc \"-//W3C//DTD HTML 4.01//EN\" 'about:legacy-compat'>",
          sizeof("<!DOCTYPE html PuBlIc \"-//W3C//DTD HTML 4.01//EN\" 'about:legacy-compat'>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_DOCTYPE
          && f.has_public_id[0] && f.has_system_id[0]
          && strcmp(f.public_ids[0], "-//W3C//DTD HTML 4.01//EN") == 0
          && strcmp(f.system_ids[0], "about:legacy-compat") == 0
          && !f.force_quirks[0] && f.errors == 0);
    check_partition(tkz, &f, "<!DOCTYPE html PUBLIC ''>",
                    sizeof("<!DOCTYPE html PUBLIC ''>") - 1);
    CHECK(f.count == 2 && f.has_public_id[0] && !f.has_system_id[0]
          && strcmp(f.public_ids[0], "") == 0
          && !f.force_quirks[0] && f.errors == 0);
    check_partition(tkz, &f, "<!DOCTYPE html PUBLIC\"a\"\"b\">",
                    sizeof("<!DOCTYPE html PUBLIC\"a\"\"b\">") - 1);
    CHECK(f.count == 2 && f.has_public_id[0] && f.has_system_id[0]
          && strcmp(f.public_ids[0], "a") == 0
          && strcmp(f.system_ids[0], "b") == 0
          && !f.force_quirks[0] && f.errors == 2
          && strcmp(f.error_code,
                    "missing-whitespace-between-doctype-public-and-system-identifiers") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html PUBLIC>",
                    sizeof("<!DOCTYPE html PUBLIC>") - 1);
    CHECK(f.count == 2 && !f.has_public_id[0] && f.force_quirks[0]
          && f.errors == 1
          && strcmp(f.error_code, "missing-doctype-public-identifier") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html PUBLIC x>",
                    sizeof("<!DOCTYPE html PUBLIC x>") - 1);
    CHECK(f.count == 2 && !f.has_public_id[0] && f.force_quirks[0]
          && f.errors == 1
          && strcmp(f.error_code,
                    "missing-quote-before-doctype-public-identifier") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html PUBLIC 'a>",
                    sizeof("<!DOCTYPE html PUBLIC 'a>") - 1);
    CHECK(f.count == 2 && f.has_public_id[0] && f.force_quirks[0]
          && strcmp(f.public_ids[0], "a") == 0 && f.errors == 1
          && strcmp(f.error_code, "abrupt-doctype-public-identifier") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html PUBLIC 'a\xe2",
                    sizeof("<!DOCTYPE html PUBLIC 'a\xe2") - 1);
    CHECK(f.count == 2 && f.has_public_id[0] && f.force_quirks[0]
          && strcmp(f.public_ids[0], "a\xef\xbf\xbd") == 0
          && f.errors == 2 && strcmp(f.error_code, "eof-in-doctype") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html PUBLIC 'a' garbage>",
                    sizeof("<!DOCTYPE html PUBLIC 'a' garbage>") - 1);
    CHECK(f.count == 2 && f.has_public_id[0] && !f.has_system_id[0]
          && f.force_quirks[0] && f.errors == 1
          && strcmp(f.error_code,
                    "missing-quote-before-doctype-system-identifier") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html SyStEm 'about:legacy-compat'>",
                    sizeof("<!DOCTYPE html SyStEm 'about:legacy-compat'>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_DOCTYPE
          && strcmp(f.bytes[0], "html") == 0 && f.has_system_id[0]
          && strcmp(f.system_ids[0], "about:legacy-compat") == 0
          && !f.force_quirks[0] && f.errors == 0);
    check_partition(tkz, &f, "<!DOCTYPE html SYSTEM\"\">",
                    sizeof("<!DOCTYPE html SYSTEM\"\">") - 1);
    CHECK(f.count == 2 && f.has_system_id[0]
          && strcmp(f.system_ids[0], "") == 0 && !f.force_quirks[0]
          && f.errors == 1
          && strcmp(f.error_code,
                    "missing-whitespace-after-doctype-system-keyword") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html SYSTEM>",
                    sizeof("<!DOCTYPE html SYSTEM>") - 1);
    CHECK(f.count == 2 && !f.has_system_id[0] && f.force_quirks[0]
          && f.errors == 1
          && strcmp(f.error_code, "missing-doctype-system-identifier") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html SYSTEM 'a\xc3'>",
                    sizeof("<!DOCTYPE html SYSTEM 'a\xc3'>") - 1);
    CHECK(f.count == 2 && f.has_system_id[0]
          && strcmp(f.system_ids[0], "a\xef\xbf\xbd") == 0
          && !f.force_quirks[0] && f.errors == 1
          && strcmp(f.error_code, "invalid-utf-8") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html SYSTEM 'a>",
                    sizeof("<!DOCTYPE html SYSTEM 'a>") - 1);
    CHECK(f.count == 2 && f.has_system_id[0]
          && strcmp(f.system_ids[0], "a") == 0 && f.force_quirks[0]
          && f.errors == 1
          && strcmp(f.error_code, "abrupt-doctype-system-identifier") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html SYSTEM 'a'",
                    sizeof("<!DOCTYPE html SYSTEM 'a'") - 1);
    CHECK(f.count == 2 && f.has_system_id[0]
          && strcmp(f.system_ids[0], "a") == 0 && f.force_quirks[0]
          && f.errors == 1 && strcmp(f.error_code, "eof-in-doctype") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html SYSTEM 'a' x>",
                    sizeof("<!DOCTYPE html SYSTEM 'a' x>") - 1);
    CHECK(f.count == 2 && f.has_system_id[0]
          && strcmp(f.system_ids[0], "a") == 0 && !f.force_quirks[0]
          && f.errors == 1
          && strcmp(f.error_code,
                    "unexpected-character-after-doctype-system-identifier") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html x\xc3\xa9>",
                    sizeof("<!DOCTYPE html x\xc3\xa9>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_DOCTYPE
          && strcmp(f.bytes[0], "html") == 0 && f.force_quirks[0]
          && f.errors == 1);
    check_partition(tkz, &f, "<!DOCTYPE html x\x01\x7f>",
                    sizeof("<!DOCTYPE html x\x01\x7f>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_DOCTYPE
          && f.force_quirks[0] && f.errors == 3
          && f.error_offset == sizeof("<!DOCTYPE html x\x01") - 1
          && strcmp(f.error_code, "control-character-in-input-stream") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html \v>",
                    sizeof("<!DOCTYPE html \v>") - 1);
    CHECK(f.count == 2 && f.errors == 2
          && strcmp(f.first_error_code, "control-character-in-input-stream") == 0
          && strcmp(f.error_code,
                    "invalid-character-sequence-after-doctype-name") == 0);
    check_partition(tkz, &f, "<\v", sizeof("<\v") - 1);
    CHECK(f.errors == 2 && strcmp(f.first_error_code,
                                   "control-character-in-input-stream") == 0
          && strcmp(f.error_code,
                    "invalid-first-character-of-tag-name") == 0);
    check_partition(tkz, &f, "x\xe2\v", sizeof("x\xe2\v") - 1);
    CHECK(f.errors == 2 && strcmp(f.first_error_code, "invalid-utf-8") == 0
          && strcmp(f.error_code, "control-character-in-input-stream") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html SYSTEM 'a' x\x01>",
                    sizeof("<!DOCTYPE html SYSTEM 'a' x\x01>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_DOCTYPE
          && !f.force_quirks[0] && f.errors == 2
          && strcmp(f.system_ids[0], "a") == 0
          && strcmp(f.error_code, "control-character-in-input-stream") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html x\xe2>",
                    sizeof("<!DOCTYPE html x\xe2>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_DOCTYPE
          && strcmp(f.bytes[0], "html") == 0 && f.force_quirks[0]
          && f.errors == 2 && f.error_offset == 16
          && strcmp(f.error_code, "invalid-utf-8") == 0);
    check_partition(tkz, &f, "<!DOCTYPE html x\xe2",
                    sizeof("<!DOCTYPE html x\xe2") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_DOCTYPE
          && strcmp(f.bytes[0], "html") == 0 && f.force_quirks[0]
          && f.errors == 2 && f.error_offset == 16
          && strcmp(f.error_code, "invalid-utf-8") == 0);
    check_partition(tkz, &f, "<!DOCTYPEfoo>",
                    sizeof("<!DOCTYPEfoo>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_DOCTYPE
          && strcmp(f.bytes[0], "foo") == 0
          && !f.force_quirks[0] && f.errors == 1 && f.error_offset == 9
          && strcmp(f.error_code,
                    "missing-whitespace-before-doctype-name") == 0);
    {
        static const char doctype_name[] = {
            '<', '!', 'D', 'O', 'C', 'T', 'Y', 'P', 'E', ' ', '\0', '>'
        };
        check_partition(tkz, &f, doctype_name, sizeof(doctype_name));
        CHECK(f.count == 2 && f.kind[0] == LXA_HTML_DOCTYPE
              && strcmp(f.bytes[0], "\xef\xbf\xbd") == 0
              && !f.force_quirks[0] && f.errors == 1 && f.error_offset == 10
              && strcmp(f.error_code, "unexpected-null-character") == 0);
    }
    check_partition(tkz, &f, "<!DOCTYPE a\xc3\xa9>",
                    sizeof("<!DOCTYPE a\xc3\xa9>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_DOCTYPE
          && strcmp(f.bytes[0], "a\xc3\xa9") == 0
          && !f.force_quirks[0] && f.errors == 0);
    check_partition(tkz, &f, "<!DOCTYPE a\xe2",
                    sizeof("<!DOCTYPE a\xe2") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_DOCTYPE
          && strcmp(f.bytes[0], "a\xef\xbf\xbd") == 0
          && f.force_quirks[0] && f.errors == 2
          && f.error_offset == sizeof("<!DOCTYPE a\xe2") - 1
          && strcmp(f.error_code, "eof-in-doctype") == 0);
    check_partition(tkz, &f, "<!DOCTYPX>",
                    sizeof("<!DOCTYPX>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
          && strcmp(f.bytes[0], "DOCTYPX") == 0
          && f.errors == 1 && f.error_offset == 2
          && strcmp(f.error_code, "incorrectly-opened-comment") == 0);
    check_partition(tkz, &f, "<!DoC", sizeof("<!DoC") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
          && strcmp(f.bytes[0], "DoC") == 0 && f.errors == 1
          && f.error_offset == 2
          && strcmp(f.error_code, "incorrectly-opened-comment") == 0);
    check_partition(tkz, &f, "<?!x>", sizeof("<?!x>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
          && strcmp(f.bytes[0], "?!x") == 0 && f.errors == 1
          && f.error_offset == 2
          && strcmp(f.error_code,
              "invalid-first-character-of-processing-instruction-target") == 0);
    check_partition(tkz, &f, "a<?", sizeof("a<?") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_TEXT
          && strcmp(f.bytes[0], "a") == 0 && f.kind[1] == LXA_HTML_EOF
          && f.errors == 1 && f.error_offset == 3
          && strcmp(f.error_code, "eof-in-processing-instruction") == 0);
    lxa_html_tokenizer_reset(tkz);
    CHECK(lxa_html_tokenizer_feed(tkz, span("<?name"))
          == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK
          && strcmp(f.error_code, "eof-in-processing-instruction") == 0);
    check_partition(tkz, &f, "<?Ab-9_:\xc2\x85>",
                    sizeof("<?Ab-9_:\xc2\x85>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
          && strcmp(f.bytes[0], "?Ab-9_:\xc2\x85") == 0
          && f.errors == 2 && f.error_offset == 8
          && strcmp(f.error_code, "control-character-in-input-stream") == 0);
    check_partition(tkz, &f, "<?XmL-StYleShEet foo?>",
                    sizeof("<?XmL-StYleShEet foo?>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
          && strcmp(f.bytes[0], "?XmL-StYleShEet foo?") == 0
          && f.errors == 1 && f.error_offset == 16
          && strcmp(f.error_code,
                    "disallowed-processing-instruction-target") == 0);
    check_partition(tkz, &f, "<?XML>", sizeof("<?XML>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
          && strcmp(f.bytes[0], "?XML") == 0 && f.errors == 1
          && f.error_offset == 5);
    check_partition(tkz, &f, "<?a:", sizeof("<?a:") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
          && strcmp(f.bytes[0], "?a:") == 0 && f.errors == 1
          && strcmp(f.error_code, "invalid-processing-instruction-target") == 0);
    {
        static const char pi[] = "<?Ab-9_ x?" "?>tail";
        check_partition(tkz, &f, pi, sizeof(pi) - 1);
    }
    CHECK(f.count == 3 && f.kind[0] == LXA_HTML_PROCESSING_INSTRUCTION
          && strcmp(f.bytes[0], "Ab-9_") == 0
          && strcmp(f.pi_data[0], "x?") == 0
          && f.kind[1] == LXA_HTML_TEXT
          && strcmp(f.bytes[1], "tail") == 0 && f.errors == 0);
    check_partition(tkz, &f, "<?a\r\nq?>", sizeof("<?a\r\nq?>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_PROCESSING_INSTRUCTION
          && strcmp(f.bytes[0], "a") == 0
          && strcmp(f.pi_data[0], "q") == 0 && f.errors == 0);
    {
        static const char pi_data[] = {'<', '?', 'a', ' ', '\0',
                                       '\xc2', '\x85', '>', 'z'};
        check_partition(tkz, &f, pi_data, sizeof(pi_data));
        CHECK(f.count == 3 && f.kind[0] == LXA_HTML_PROCESSING_INSTRUCTION
              && strcmp(f.bytes[0], "a") == 0
              && memcmp(f.pi_data[0], "\0\xc2\x85", 3) == 0
              && f.errors == 1 && f.error_offset == 5
              && strcmp(f.error_code, "control-character-in-input-stream") == 0);
    }
    check_partition(tkz, &f, "<?a \xe2", sizeof("<?a \xe2") - 1);
    CHECK(f.count == 1 && f.kind[0] == LXA_HTML_EOF
          && f.errors == 2 && f.error_offset == 5
          && strcmp(f.error_code, "eof-in-processing-instruction") == 0);
    lxa_html_tokenizer_reset(tkz);
    f.fail_on = LXA_HTML_PROCESSING_INSTRUCTION;
    CHECK(lxa_html_tokenizer_feed(tkz, span("<?ok?>"))
          == LXA_HTML_CALLBACK_FAILURE);
    CHECK(lxa_html_tokenizer_callback_status(tkz) == LXA_ERROR_NO_MEMORY);
    {
        const char *eof_tag[] = {"a<", "a</"};
        size_t j;
        for (j = 0; j < sizeof(eof_tag) / sizeof(eof_tag[0]); j++) {
            check_partition(tkz, &f, eof_tag[j], strlen(eof_tag[j]));
            CHECK(f.count == 3 && f.errors == 1
                  && strcmp(f.bytes[0], "a") == 0
                  && strcmp(f.bytes[1], eof_tag[j] + 1) == 0
                  && f.error_offset == strlen(eof_tag[j])
                  && strcmp(f.error_code, "eof-before-tag-name") == 0);
        }
    }
    check_partition(tkz, &f, "</a><", sizeof("</a><") - 1);
    CHECK(f.count == 3 && f.kind[0] == LXA_HTML_END_TAG
          && strcmp(f.bytes[1], "<") == 0 && f.errors == 1
          && f.error_offset == sizeof("</a><") - 1);
    {
        const char *end_solidus[] = {"</a/>", "</a />"};
        size_t j;
        for (j = 0; j < sizeof(end_solidus) / sizeof(end_solidus[0]); j++) {
            check_partition(tkz, &f, end_solidus[j],
                            strlen(end_solidus[j]));
            CHECK(f.count == 2 && f.kind[0] == LXA_HTML_END_TAG
                  && strcmp(f.bytes[0], "a") == 0 && f.self_closing[0]
                  && f.errors == 1
                  && f.error_offset == strlen(end_solidus[j]) - 1
                  && strcmp(f.error_code, "end-tag-with-trailing-solidus") == 0);
        }
    }
    check_partition(tkz, &f, "<A_B.c=Q></A_B.c=Q>",
                    sizeof("<A_B.c=Q></A_B.c=Q>") - 1);
    CHECK(f.count == 3 && f.kind[0] == LXA_HTML_START_TAG
          && f.kind[1] == LXA_HTML_END_TAG
          && strcmp(f.bytes[0], "a_b.c=q") == 0
          && strcmp(f.bytes[1], "a_b.c=q") == 0 && f.errors == 0);
    {
        static const char name_controls[] = {'<', 'A', '\0', '\x01',
                                             '\x7f', '>'};
        check_partition(tkz, &f, name_controls, sizeof(name_controls));
        CHECK(f.count == 2 && f.kind[0] == LXA_HTML_START_TAG
              && strcmp(f.bytes[0], "a\xef\xbf\xbd\x01\x7f") == 0
              && f.errors == 3 && f.error_offset == 4
              && strcmp(f.error_code, "control-character-in-input-stream") == 0);
    }
    check_partition(tkz, &f, "<A\xc3\xa9\xef\xb7\x90>",
                    sizeof("<A\xc3\xa9\xef\xb7\x90>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_START_TAG
          && strcmp(f.bytes[0], "a\xc3\xa9\xef\xb7\x90") == 0
          && f.errors == 1 && f.error_offset == 4
          && strcmp(f.error_code, "noncharacter-in-input-stream") == 0);
    check_partition(tkz, &f, "<a\xe2>", sizeof("<a\xe2>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_START_TAG
          && strcmp(f.bytes[0], "a\xef\xbf\xbd") == 0
          && f.errors == 1 && f.error_offset == 2
          && strcmp(f.error_code, "invalid-utf-8") == 0);
    check_partition(tkz, &f, "<a\xe2", sizeof("<a\xe2") - 1);
    CHECK(f.count == 1 && f.kind[0] == LXA_HTML_EOF
          && f.errors == 2 && f.error_offset == 3
          && strcmp(f.error_code, "eof-in-tag") == 0);
    {
        const char *unfinished[] = {"x<a", "x<a ", "x<a/", "x</a"};
        size_t j;
        for (j = 0; j < sizeof(unfinished) / sizeof(unfinished[0]); j++) {
            check_partition(tkz, &f, unfinished[j], strlen(unfinished[j]));
            CHECK(f.count == 2 && f.kind[0] == LXA_HTML_TEXT
                  && strcmp(f.bytes[0], "x") == 0
                  && f.kind[1] == LXA_HTML_EOF && f.errors == 1
                  && f.error_offset == strlen(unfinished[j])
                  && strcmp(f.error_code, "eof-in-tag") == 0);
        }
    }
    check_partition(tkz, &f, nul_text, sizeof(nul_text));
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_TEXT
          && f.lengths[0] == sizeof(nul_text)
          && memcmp(f.bytes[0], nul_text, sizeof(nul_text)) == 0
          && f.errors == 1 && f.error_offset == 1
          && strcmp(f.error_code, "unexpected-null-character") == 0);
    {
        static const char controls[] = {'a', '\x01', '\x7f', 'b'};
        check_partition(tkz, &f, controls, sizeof(controls));
        CHECK(f.count == 2 && f.kind[0] == LXA_HTML_TEXT
              && memcmp(f.bytes[0], controls, sizeof(controls)) == 0
              && f.errors == 2 && f.error_offset == 2
              && strcmp(f.error_code, "control-character-in-input-stream") == 0);
    }
    {
        static const char unicode_controls[] = "a\xc2\x85"
            "\xef\xb7\x90\xf4\x8f\xbf\xbf" "b";
        check_partition(tkz, &f, unicode_controls,
                        sizeof(unicode_controls) - 1);
        CHECK(f.count == 2 && f.kind[0] == LXA_HTML_TEXT
              && strcmp(f.bytes[0], unicode_controls) == 0
              && f.errors == 3 && f.error_offset == 6
              && strcmp(f.error_code, "noncharacter-in-input-stream") == 0);
    }
    {
        static const char bad_utf8_nul[] = {'\xe2', '\0'};
        check_partition(tkz, &f, bad_utf8_nul, sizeof(bad_utf8_nul));
        CHECK(f.count == 2
              && f.lengths[0] == 4
              && memcmp(f.bytes[0], "\xef\xbf\xbd\0", 4) == 0
              && f.errors == 2 && f.error_offset == 1
              && strcmp(f.error_code, "unexpected-null-character") == 0);
    }
    {
        static const char invalid_refs[] =
            "&#0;&#xD800;&#999999999999999999999999999999;";
        check_partition(tkz, &f, invalid_refs, sizeof(invalid_refs) - 1);
        CHECK(f.count == 2 && f.errors == 3
              && strcmp(f.bytes[0],
                        "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd") == 0
              && f.error_offset == sizeof(invalid_refs) - 1
              && strcmp(f.error_code,
                        "character-reference-outside-unicode-range") == 0);
    }
    {
        static const char control_refs[] = "&#128;&#129;&#13;&#xFDD0;";
        check_partition(tkz, &f, control_refs, sizeof(control_refs) - 1);
        CHECK(f.count == 2 && f.errors == 4
              && strcmp(f.bytes[0], "\xe2\x82\xac\xc2\x81\r\xef\xb7\x90") == 0
              && f.error_offset == sizeof(control_refs) - 1
              && strcmp(f.error_code,
                        "noncharacter-character-reference") == 0);
    }
    {
        static const char unclosed_refs[] = "&#65x &#x20ac &#0";
        check_partition(tkz, &f, unclosed_refs, sizeof(unclosed_refs) - 1);
        CHECK(f.count == 2 && f.errors == 4
              && strcmp(f.bytes[0], "Ax \xe2\x82\xac \xef\xbf\xbd") == 0
              && f.error_offset == sizeof(unclosed_refs) - 1
              && strcmp(f.error_code, "null-character-reference") == 0);
    }
    {
        static const char no_digits[] = "x&#;y&#X;z&#x";
        check_partition(tkz, &f, no_digits, sizeof(no_digits) - 1);
        CHECK(f.count == 2 && f.errors == 3
              && strcmp(f.bytes[0], no_digits) == 0
              && strcmp(f.error_code,
                        "absence-of-digits-in-numeric-character-reference") == 0);
    }
    check_partition(tkz, &f, "&#", 2);
    CHECK(f.count == 2 && f.errors == 1 && strcmp(f.bytes[0], "&#") == 0);
    {
        const char *comments[] = {"<!--co", "<!--co-", "<!--co--", "<!--co---"};
        size_t j;
        for (j = 0; j < sizeof(comments) / sizeof(comments[0]); j++) {
            check_partition(tkz, &f, comments[j], strlen(comments[j]));
            CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
                  && strcmp(f.bytes[0], j == 3 ? "co-" : "co") == 0
                  && f.errors == 1 && f.error_offset == strlen(comments[j])
                  && strcmp(f.error_code, "eof-in-comment") == 0);
        }
    }
    {
        static const char incomplete_utf8_comment[] = "<!--co\xe2";
        check_partition(tkz, &f, incomplete_utf8_comment,
                        sizeof(incomplete_utf8_comment) - 1);
        CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
              && strcmp(f.bytes[0], "co\xef\xbf\xbd") == 0
              && f.errors == 2 && f.error_offset
                 == sizeof(incomplete_utf8_comment) - 1
              && strcmp(f.error_code, "eof-in-comment") == 0);
    }
    check_partition(tkz, &f, "<!--x--!>", sizeof("<!--x--!>") - 1);
    CHECK(f.count == 2 && f.errors == 1
          && strcmp(f.bytes[0], "x") == 0
          && strcmp(f.error_code, "incorrectly-closed-comment") == 0);
    check_partition(tkz, &f, "<!--x--!a-->", sizeof("<!--x--!a-->") - 1);
    CHECK(f.count == 2 && f.errors == 0
          && strcmp(f.bytes[0], "x--!a") == 0);
    {
        const char *abrupt[] = {"<!-->", "<!--->"};
        size_t j;
        for (j = 0; j < sizeof(abrupt) / sizeof(abrupt[0]); j++) {
            check_partition(tkz, &f, abrupt[j], strlen(abrupt[j]));
            CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
                  && f.errors == 1 && f.error_offset == strlen(abrupt[j]) - 1
                  && strcmp(f.bytes[0], "") == 0
                  && strcmp(f.error_code,
                            "abrupt-closing-of-empty-comment") == 0);
        }
    }
    check_partition(tkz, &f, "<!---x-->", sizeof("<!---x-->") - 1);
    CHECK(f.count == 2 && f.errors == 0
          && strcmp(f.bytes[0], "-x") == 0);
    check_partition(tkz, &f, "<!--x<!--y-->", sizeof("<!--x<!--y-->") - 1);
    CHECK(f.count == 2 && f.errors == 1
          && strcmp(f.bytes[0], "x<!--y") == 0
          && strcmp(f.error_code, "nested-comment") == 0);
    check_partition(tkz, &f, "<!--x<!-->", sizeof("<!--x<!-->") - 1);
    CHECK(f.count == 2 && f.errors == 0
          && strcmp(f.bytes[0], "x<!") == 0);
    check_partition(tkz, &f, "<!--x<!--", sizeof("<!--x<!--") - 1);
    CHECK(f.count == 2 && f.errors == 1
          && strcmp(f.bytes[0], "x<!") == 0
          && strcmp(f.error_code, "eof-in-comment") == 0);
    {
        /* WHATWG HTML: table text in a template stays tokenizer DATA; the
         * tree builder alone decides where its resulting text belongs. */
        static const char template_table[] =
            "<template><table> x&amp;y<tr><td>z</td></tr></table></template>";
        snapshot_t tokens;
        check_partition(tkz, &f, template_table,
                        sizeof(template_table) - 1);
        tokens = snapshot(&f);
        CHECK(tokens.count == 11 && tokens.kind[2] == LXA_HTML_TEXT
              && strcmp(tokens.bytes[2], " x&y") == 0
              && tokens.kind[10] == LXA_HTML_EOF && tokens.errors == 0);
    }
    {
        /* Pinned WHATWG HTML CDATA foreign-content example, followed by
         * the same opener in HTML content (bogus comment + parse error). */
        static const char math_cdata[] =
            "<math><ms><![CDATA[x<y]]></ms></math><![CDATA[x]]>";
        snapshot_t tokens;
        check_partition_mode(tkz, &f, math_cdata, sizeof(math_cdata) - 1,
                             false, true);
        tokens = snapshot(&f);
        CHECK(tokens.count == 7 && tokens.kind[2] == LXA_HTML_TEXT
              && strcmp(tokens.bytes[2], "x<y") == 0
              && tokens.kind[5] == LXA_HTML_COMMENT
              && strcmp(tokens.bytes[5], "[CDATA[x]]") == 0
              && tokens.kind[6] == LXA_HTML_EOF && tokens.errors == 1
              && strcmp(f.error_code, "cdata-in-html-content") == 0);
    }
    /* html5lib pendingSpecChanges.test: end marker followed by a space. */
    check_partition(tkz, &f, "<!---- >", sizeof("<!---- >") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
          && strcmp(f.bytes[0], "-- >") == 0
          && f.errors == 1 && f.error_offset == sizeof("<!---- >") - 1
          && strcmp(f.error_code, "eof-in-comment") == 0);
    check_partition(tkz, &f, "<![CDATA[a&b]]>",
                    sizeof("<![CDATA[a&b]]>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
          && strcmp(f.bytes[0], "[CDATA[a&b]]") == 0
          && f.errors == 1
          && strcmp(f.error_code, "cdata-in-html-content") == 0);
    check_partition_mode(tkz, &f, "<![CDATA[a&b]]]><i",
                         sizeof("<![CDATA[a&b]]]><i") - 1, true, false);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_TEXT
          && strcmp(f.bytes[0], "a&b]") == 0 && f.errors == 1
          && strcmp(f.error_code, "eof-in-tag") == 0);
    check_partition_mode(tkz, &f, "<![CDATA[x\xe2]",
                         sizeof("<![CDATA[x\xe2]") - 1, true, false);
    CHECK(f.count == 2 && strcmp(f.bytes[0], "x\xef\xbf\xbd]") == 0
          && f.errors == 2 && strcmp(f.error_code, "eof-in-cdata") == 0);
    check_partition(tkz, &f, "<![CDAXx>",
                    sizeof("<![CDAXx>") - 1);
    CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
          && strcmp(f.bytes[0], "[CDAXx") == 0 && f.errors == 1
          && strcmp(f.error_code, "incorrectly-opened-comment") == 0);
    {
        static const char nul_cdata[] = {
            '<', '!', '[', 'C', 'D', 'A', 'T', 'A', '[', 'x', 0, 'y',
            ']', ']', '>'
        };
        lxa_span_t data = {(const uint8_t *) nul_cdata, sizeof(nul_cdata)};
        lxa_html_tokenizer_reset(tkz);
        memset(&f, 0, sizeof(f));
        f.fail_on = (lxa_html_token_kind_t) 255;
        CHECK(lxa_html_tokenizer_allow_cdata(tkz, true) == LXA_HTML_OK);
        CHECK(lxa_html_tokenizer_feed(tkz, data) == LXA_HTML_OK);
        CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
        CHECK(f.count == 2 && f.kind[0] == LXA_HTML_TEXT
              && memcmp(f.bytes[0], "x\0y", 3) == 0 && f.errors == 0);
    }
    check_partition(tkz, &f, "<!---", sizeof("<!---") - 1);
    CHECK(f.count == 2 && f.errors == 1
          && strcmp(f.bytes[0], "") == 0
          && strcmp(f.error_code, "eof-in-comment") == 0);
    {
        static const char comment_nul[] = {'<', '!', '-', '-', 'x', '\0',
                                           'y', '-', '-', '>'};
        check_partition(tkz, &f, comment_nul, sizeof(comment_nul));
        CHECK(f.count == 2 && f.errors == 1 && f.error_offset == 5
              && strcmp(f.bytes[0], "x\xef\xbf\xbd" "y") == 0
              && strcmp(f.error_code, "unexpected-null-character") == 0);
    }
    {
        static const char comment_controls[] = {'<', '!', '-', '-', 'x',
                                                '\x01', '\x7f', '-', '-', '>'};
        check_partition(tkz, &f, comment_controls, sizeof(comment_controls));
        CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
              && memcmp(f.bytes[0], "x\x01\x7f", 3) == 0
              && f.errors == 2 && f.error_offset == 6
              && strcmp(f.error_code, "control-character-in-input-stream") == 0);
    }
    {
        static const char comment_unicode[] = "<!--\xc2\x85\xef\xb7\x90-->";
        check_partition(tkz, &f, comment_unicode,
                        sizeof(comment_unicode) - 1);
        CHECK(f.count == 2 && f.kind[0] == LXA_HTML_COMMENT
              && strcmp(f.bytes[0], "\xc2\x85\xef\xb7\x90") == 0
              && f.errors == 2 && f.error_offset == 6
              && strcmp(f.error_code, "noncharacter-in-input-stream") == 0);
    }
    lxa_html_tokenizer_reset(tkz);
    f.count = 0;
    f.error_fail = true;
    CHECK(lxa_html_tokenizer_feed(tkz, span("<!--co")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_CALLBACK_FAILURE
          && f.count == 0
          && lxa_html_tokenizer_callback_status(tkz) == LXA_ERROR_NO_MEMORY);
    f.error_fail = false;
    lxa_html_tokenizer_reset(tkz);
    f.count = f.errors = 0;
    f.error_fail = true;
    CHECK(lxa_html_tokenizer_feed(tkz, span("<a x=1 X")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_CALLBACK_FAILURE
          && f.count == 0 && f.errors == 1
          && strcmp(f.error_code, "duplicate-attribute") == 0
          && lxa_html_tokenizer_callback_status(tkz) == LXA_ERROR_NO_MEMORY
          && lxa_html_tokenizer_finish(tkz) == LXA_HTML_CALLBACK_FAILURE);
    f.error_fail = false;
    lxa_html_tokenizer_reset(tkz);
    CHECK(lxa_html_tokenizer_feed(tkz, span("<a y=1>")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(f.attribute_count[0] == 1
          && strcmp(f.attribute_names[0][0], "y") == 0);
    lxa_html_tokenizer_reset(tkz);
    f.error_fail = true;
    CHECK(lxa_html_tokenizer_feed(tkz, span("&#0;"))
          == LXA_HTML_CALLBACK_FAILURE);
    CHECK(lxa_html_tokenizer_callback_status(tkz) == LXA_ERROR_NO_MEMORY);
    f.error_fail = false;
    lxa_html_tokenizer_reset(tkz);
    f.count = f.errors = 0;
    CHECK(lxa_html_tokenizer_feed(tkz, span("&#;")) == LXA_HTML_OK);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_OK);
    CHECK(strcmp(f.bytes[0], "&#;") == 0
          && strcmp(f.error_code,
                    "absence-of-digits-in-numeric-character-reference") == 0);
    lxa_html_tokenizer_reset(tkz);
    f.error_fail = true;
    CHECK(lxa_html_tokenizer_feed(tkz, (lxa_span_t) {
          (const uint8_t *) nul_text, sizeof(nul_text)})
          == LXA_HTML_CALLBACK_FAILURE);
    CHECK(lxa_html_tokenizer_callback_status(tkz) == LXA_ERROR_NO_MEMORY
          && lxa_html_tokenizer_finish(tkz) == LXA_HTML_CALLBACK_FAILURE);
    f.error_fail = false;
    lxa_html_tokenizer_reset(tkz);

    lxa_html_tokenizer_destroy(tkz);

    check_allocation_failures();
    check_large_attributes();
    check_token_sizes();
    check_budget();
    check_prepaid_input();

    CHECK(lxa_html_tokenizer_create(&failing, on_token, on_error, &f,
                                     &tkz) == LXA_OK);
    state.fail_growth = true;
    CHECK(lxa_html_tokenizer_feed(tkz, span("x")) == LXA_HTML_NO_MEMORY);
    CHECK(lxa_html_tokenizer_finish(tkz) == LXA_HTML_NO_MEMORY);
    lxa_html_tokenizer_destroy(tkz);
    CHECK(state.releases == 3);
    return 0;
}
