#include "liblexa/html.h"
#include "html/tree.h"
#include "html/tokenizer_tree.h"
#include "html/input_encoding.h"
#include <string.h>

struct lxa_html_document {
    lxa_allocator_t allocator;
    lxa_budget_t budget;
    lxa_dom_nodes_t *nodes;
    lxa_dom_ref_t root, head, body;
};

static lxa_status_t
parser_status(lxa_html_result_t result, lxa_html_tokenizer_t *tokenizer)
{
    switch (result) {
    case LXA_HTML_OK: return LXA_OK;
    case LXA_HTML_OVERFLOW: return LXA_ERROR_OVERFLOW;
    case LXA_HTML_NO_MEMORY: return LXA_ERROR_NO_MEMORY;
    case LXA_HTML_LIMIT: return LXA_ERROR_LIMIT;
    case LXA_HTML_ENTROPY: return LXA_ERROR_ENTROPY;
    case LXA_HTML_UNSUPPORTED: return LXA_ERROR_UNSUPPORTED;
    case LXA_HTML_CALLBACK_FAILURE: {
        lxa_status_t status = lxa_html_tokenizer_callback_status(tokenizer);
        return status == LXA_ERROR_ARGUMENT ? LXA_ERROR_INTERNAL : status;
    }
    default: return LXA_ERROR_INTERNAL;
    }
}

static lxa_html_result_t
document_result(lxa_status_t status, lxa_html_result_t result)
{
    /* Only the caller's diagnostic callback retains CALLBACK_FAILURE.
     * Tree callbacks are implementation details, including their failures. */
    if (result == LXA_HTML_CALLBACK_FAILURE) return result;
    switch (status) {
    case LXA_OK: return LXA_HTML_OK;
    case LXA_ERROR_LIMIT: return LXA_HTML_LIMIT;
    case LXA_ERROR_NO_MEMORY: return LXA_HTML_NO_MEMORY;
    case LXA_ERROR_OVERFLOW: return LXA_HTML_OVERFLOW;
    case LXA_ERROR_ENTROPY: return LXA_HTML_ENTROPY;
    case LXA_ERROR_UNSUPPORTED: return LXA_HTML_UNSUPPORTED;
    case LXA_ERROR_ARGUMENT: return LXA_HTML_BAD_ARGUMENT;
    default: return LXA_HTML_INTERNAL;
    }
}

static lxa_status_t
input_status(lxa_html_input_result_t result)
{
    switch (result) {
    case LXA_HTML_INPUT_OK: return LXA_OK;
    case LXA_HTML_INPUT_LIMIT: return LXA_ERROR_LIMIT;
    case LXA_HTML_INPUT_NO_MEMORY: return LXA_ERROR_NO_MEMORY;
    case LXA_HTML_INPUT_OVERFLOW: return LXA_ERROR_OVERFLOW;
    case LXA_HTML_INPUT_UNKNOWN_ENCODING: return LXA_ERROR_UNSUPPORTED;
    default: return LXA_ERROR_ARGUMENT;
    }
}

typedef struct {
    lxa_budget_t *budget;
    const lxa_encoding_t *declared;
    lxa_limit_reason_t reason;
} meta_context_t;

/* Resolve parser-inserted meta declarations before later tree mutation. */
static lxa_status_t
observe_meta(void *context, const lxa_html_token_t *token)
{
    meta_context_t *meta = context;
    if (meta->declared != NULL) return LXA_OK;
    return input_status(lxa_html_input_meta_encoding(
        token->attributes, token->attribute_count, meta->budget,
        &meta->declared, &meta->reason));
}

void
lxa_html_document_destroy(lxa_html_document_t *document)
{
    lxa_allocator_t allocator;
    if (document == NULL) return;
    allocator = document->allocator;
    lxa_dom_nodes_destroy(document->nodes);
    lxa_budget_release(&document->budget, LXA_BUDGET_MEMORY,
                       sizeof(*document));
    lxa_free(&allocator, document);
}

lxa_status_t
lxa_html_document_parse_report(const lxa_allocator_t *allocator,
    const lxa_limits_t *limits, lxa_span_t input,
    lxa_html_input_error_callback_t on_malformed, void *context,
    lxa_html_result_t *parser_result, lxa_limit_reason_t *reason,
    lxa_html_document_t **out)
{
    lxa_html_document_t *document = NULL;
    lxa_html_tree_t tree;
    lxa_html_tokenizer_t *tokenizer = NULL;
    lxa_buffer_t decoded;
    const lxa_encoding_t *encoding;
    size_t prefix, malformed;
    lxa_html_input_result_t selected;
    lxa_html_result_t result = LXA_HTML_BAD_ARGUMENT;
    lxa_status_t status;
    lxa_status_t callback_status = LXA_OK;
    unsigned int attempt;
    bool utf16;
    if (out != NULL) *out = NULL;
    if (reason != NULL) *reason = LXA_BUDGET_COUNT;
    if (parser_result != NULL) *parser_result = result;
    if (out == NULL || allocator == NULL
        || allocator->allocate == NULL || allocator->resize == NULL
        || allocator->release == NULL
        || (input.data == NULL && input.length != 0)) {
        return LXA_ERROR_ARGUMENT;
    }
    selected = lxa_html_input_select(input,
        limits == NULL ? lxa_limits_default().max_input_bytes
                       : limits->max_input_bytes,
        &encoding, &prefix);
    if (selected != LXA_HTML_INPUT_OK) {
        if (selected == LXA_HTML_INPUT_LIMIT && reason != NULL)
            *reason = LXA_BUDGET_INPUT;
        status = input_status(selected);
        goto report;
    }
    /* The encoding-change algorithm never replaces a UTF-16 decoder. */
    utf16 = encoding == lxa_encoding_lookup(
        (lxa_span_t){(const uint8_t *) "utf-16le", 8})
        || encoding == lxa_encoding_lookup(
            (lxa_span_t){(const uint8_t *) "utf-16be", 8});
    result = LXA_HTML_INTERNAL;
    status = lxa_alloc(allocator, sizeof(*document), (void **) &document);
    if (status != LXA_OK) goto report;
    document->allocator = *allocator;
    document->nodes = NULL;
    status = lxa_budget_init(&document->budget, limits);
    if (status != LXA_OK) {
        lxa_free(allocator, document);
        goto report;
    }
    status = lxa_budget_charge(&document->budget, LXA_BUDGET_MEMORY,
                               sizeof(*document), reason);
    if (status != LXA_OK) {
        lxa_free(allocator, document);
        goto report;
    }
    for (attempt = 0; attempt < 2; ++attempt) {
        bool retry = false;
        meta_context_t meta = {
            .budget = &document->budget, .reason = LXA_BUDGET_COUNT
        };
        status = lxa_buffer_init(&decoded, allocator);
        if (status != LXA_OK) break;
        status = lxa_buffer_bind_budget(&decoded, &document->budget);
        if (status != LXA_OK) {
            lxa_buffer_destroy(&decoded);
            break;
        }
        selected = attempt == 0
            ? lxa_html_input_decode_report(input, encoding, prefix,
                &document->budget, &decoded, &malformed, reason,
                NULL, NULL, NULL)
            : lxa_html_input_decode_prepaid_report(input, encoding, prefix,
                &document->budget, &decoded, &malformed, reason,
                NULL, NULL, NULL);
        status = input_status(selected);
        if (status != LXA_OK) {
            lxa_buffer_destroy(&decoded);
            break;
        }
        if (document->nodes == NULL)
            status = lxa_dom_nodes_create_with_budget(allocator,
                &document->budget, reason, &document->nodes);
        if (status != LXA_OK) {
            lxa_buffer_destroy(&decoded);
            break;
        }
        status = lxa_html_tree_init(&tree, document->nodes);
        if (status != LXA_OK) {
            if (status == LXA_ERROR_LIMIT && reason != NULL)
                *reason = lxa_dom_nodes_limit_reason(document->nodes);
            lxa_buffer_destroy(&decoded);
            break;
        }
        if (prefix == 0) {
            tree.on_meta = observe_meta;
            tree.meta_context = &meta;
        }
        status = lxa_html_tokenizer_create_with_budget(allocator,
            lxa_html_tree_on_token, NULL, &tree,
            &document->budget, &tokenizer);
        if (status == LXA_OK) {
            result = lxa_html_tree_tokenizer_feed_prepaid(tokenizer,
                (lxa_span_t){decoded.data, decoded.length});
            if (result == LXA_HTML_OK)
                result = lxa_html_tree_tokenizer_finish(tokenizer);
            status = parser_status(result, tokenizer);
            if (status == LXA_ERROR_LIMIT && reason != NULL)
                *reason = meta.reason != LXA_BUDGET_COUNT ? meta.reason
                        : result == LXA_HTML_LIMIT
                          ? lxa_html_tokenizer_limit_reason(tokenizer)
                          : lxa_dom_nodes_limit_reason(document->nodes);
            if (result == LXA_HTML_CALLBACK_FAILURE)
                result = document_result(status, LXA_HTML_INTERNAL);
        }
        else if (status == LXA_ERROR_LIMIT && reason != NULL)
            *reason = LXA_BUDGET_MEMORY;
        if (status == LXA_OK && !utf16 && meta.declared != NULL
            && meta.declared != encoding) {
            if (attempt == 0) {
                encoding = meta.declared;
                retry = true;
            }
            else {
                result = LXA_HTML_UNSUPPORTED;
                status = LXA_ERROR_UNSUPPORTED;
            }
        }
        if (status == LXA_OK && !retry && on_malformed != NULL) {
            selected = lxa_html_input_report_malformed(input, encoding,
                prefix, malformed, &document->budget, allocator,
                on_malformed, context, &callback_status, reason);
            status = selected == LXA_HTML_INPUT_CALLBACK_FAILURE
                     ? callback_status : input_status(selected);
            if (selected == LXA_HTML_INPUT_CALLBACK_FAILURE)
                result = LXA_HTML_CALLBACK_FAILURE;
        }
        if (status == LXA_OK && !retry) {
            document->root = tree.document;
            document->head = tree.head;
            document->body = tree.body;
        }
        lxa_html_tokenizer_destroy(tokenizer);
        tokenizer = NULL;
        lxa_html_tree_destroy(&tree);
        lxa_buffer_destroy(&decoded);
        if (!retry || status != LXA_OK) break;
        /* Reuse the store's charged slot/string high-water without resetting
         * the shared INPUT or WORK budget; no tentative handle escaped. */
        status = lxa_dom_nodes_reset(document->nodes);
        if (status == LXA_ERROR_LIMIT && reason != NULL)
            *reason = lxa_dom_nodes_limit_reason(document->nodes);
        if (status != LXA_OK) break;
    }
    if (status == LXA_OK)
        *out = document;
    else
        lxa_html_document_destroy(document);
report:
    if (status == LXA_ERROR_ARGUMENT && result != LXA_HTML_BAD_ARGUMENT
        && result != LXA_HTML_CALLBACK_FAILURE)
        status = LXA_ERROR_INTERNAL;
    if (parser_result != NULL)
        *parser_result = document_result(status, result);
    return status;
}

lxa_status_t
lxa_html_document_parse(const lxa_allocator_t *allocator,
    const lxa_limits_t *limits, lxa_span_t input,
    lxa_html_result_t *parser_result, lxa_limit_reason_t *reason,
    lxa_html_document_t **out)
{
    return lxa_html_document_parse_report(allocator, limits, input,
        NULL, NULL, parser_result, reason, out);
}

lxa_dom_nodes_t *
lxa_html_document_nodes(lxa_html_document_t *document)
{
    return document == NULL ? NULL : document->nodes;
}

lxa_dom_ref_t
lxa_html_document_root(const lxa_html_document_t *document)
{
    return document == NULL ? (lxa_dom_ref_t){0} : document->root;
}

lxa_dom_ref_t
lxa_html_document_head(const lxa_html_document_t *document)
{
    return document == NULL ? (lxa_dom_ref_t){0} : document->head;
}

lxa_dom_ref_t
lxa_html_document_body(const lxa_html_document_t *document)
{
    return document == NULL ? (lxa_dom_ref_t){0} : document->body;
}
