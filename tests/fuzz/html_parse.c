/* Deterministic, bounded parse smoke; the seed and iteration count are fixed. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "liblexa/html.h"
#include "liblexa/serialize.h"

static lxa_write_result_t
count_output(void *context, lxa_span_t bytes)
{
    size_t *written = context;
    if (bytes.length != 0 && bytes.data == NULL)
        return (lxa_write_result_t) {0, LXA_ERROR_ARGUMENT};
    if (bytes.length > 4096 - *written)
        return (lxa_write_result_t) {0, LXA_ERROR_LIMIT};
    *written += bytes.length;
    return (lxa_write_result_t) {bytes.length, LXA_OK};
}

static uint32_t
next(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return *state = value;
}

int
main(void)
{
    static const char *const starts[] = {
        "<!doctype html><p>", "<table><tr><td>", "<template><b>",
        "<svg><foreignObject>", "<select><option>", "<script>"
    };
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_limits_t limits = lxa_limits_default();
    uint32_t state = UINT32_C(0x894412cb);
    uint8_t bytes[128];
    limits.max_input_bytes = sizeof(bytes);
    limits.max_memory_bytes = 256 * 1024;
    limits.max_nodes = 128;
    limits.max_work = 65536;
    for (unsigned int case_id = 0; case_id < 512; ++case_id) {
        lxa_html_document_t *document = NULL;
        lxa_html_result_t result = LXA_HTML_BAD_ARGUMENT;
        lxa_limit_reason_t reason;
        size_t length = next(&state) % (sizeof(bytes) + 1);
        size_t prefix = 0;
        lxa_status_t status;
        if (case_id % 3 == 0) {
            const char *start = starts[next(&state) %
                (sizeof(starts) / sizeof(starts[0]))];
            prefix = strlen(start);
            if (prefix > length) prefix = length;
            memcpy(bytes, start, prefix);
        }
        for (size_t i = prefix; i < length; ++i)
            bytes[i] = (uint8_t) next(&state);
        status = lxa_html_document_parse(&allocator, &limits,
            (lxa_span_t){bytes, length}, &result, &reason, &document);
        if ((status == LXA_OK) != (document != NULL)
            || (status == LXA_OK && result != LXA_HTML_OK)
            || (status == LXA_ERROR_UNSUPPORTED
                && result != LXA_HTML_UNSUPPORTED)
            || (status == LXA_ERROR_LIMIT && result != LXA_HTML_LIMIT)
            || (status != LXA_OK && status != LXA_ERROR_UNSUPPORTED
                && status != LXA_ERROR_LIMIT)) {
            fprintf(stderr, "case %u: status %d, result %d, reason %d\n",
                case_id, status, result, reason);
            lxa_html_document_destroy(document);
            return 1;
        }
        if (document != NULL) {
            size_t written = 0;
            status = lxa_dom_nodes_validate(lxa_html_document_nodes(document));
            if (status != LXA_OK) {
                fprintf(stderr, "case %u: invalid tree, status %d\n",
                        case_id, status);
                lxa_html_document_destroy(document);
                return 1;
            }
            status = lxa_html_serialize_document(
                lxa_html_document_nodes(document),
                lxa_html_document_root(document), count_output, &written,
                &reason);
            if (status != LXA_OK && status != LXA_ERROR_LIMIT) {
                fprintf(stderr, "case %u: serialize status %d, reason %d\n",
                        case_id, status, reason);
                lxa_html_document_destroy(document);
                return 1;
            }
        }
        lxa_html_document_destroy(document);
    }
    return 0;
}
