/* Copyright (C) 2026 Paul Moses */
#ifndef LIBLEXA_HTML_H
#define LIBLEXA_HTML_H

#include <stdbool.h>
#include "liblexa/base.h"
#include "liblexa/encoding.h"
#include "liblexa/dom.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Tree construction must not inspect tokenizer state. */
typedef struct lxa_html_tokenizer lxa_html_tokenizer_t;

typedef enum {
    LXA_HTML_START_TAG,
    LXA_HTML_END_TAG,
    LXA_HTML_TEXT,
    LXA_HTML_COMMENT,
    LXA_HTML_DOCTYPE,
    LXA_HTML_EOF,
    LXA_HTML_PROCESSING_INSTRUCTION
} lxa_html_token_kind_t;

typedef struct {
    lxa_span_t name;             /* ASCII-lowercased in HTML input */
    lxa_span_t value;            /* character references already expanded */
} lxa_html_attribute_t;

typedef struct {
    lxa_html_token_kind_t kind;
    lxa_span_t name;             /* tag or doctype name; PI target if applicable */
    lxa_span_t data;             /* text, comment, or PI data */
    const lxa_html_attribute_t *attributes; /* start tags only, source order */
    size_t attribute_count;
    lxa_span_t public_id;        /* doctype only */
    lxa_span_t system_id;        /* doctype only */
    bool has_public_id;          /* distinguishes absent from empty */
    bool has_system_id;
    bool force_quirks;
    bool self_closing;           /* trailing solidus; tree acknowledges it */
} lxa_html_token_t;

/* Callback views (including attributes and decoded text) are owned by the
 * tokenizer and valid only until the callback returns. Copy bytes needed in
 * the DOM before returning. Empty spans may have NULL data. No token points
 * into an input chunk after feed returns. Text is decoded (entities and
 * replacement characters). DATA U+0000 is diagnosed and delivered literally
 * so the tree can apply its context-specific null handling.
 * Duplicate attributes are delivered in source order; the tree
 * applies its required duplicate-attribute policy.
 */
typedef lxa_status_t (*lxa_html_token_callback_t)(
    void *context, lxa_html_tokenizer_t *tokenizer,
    const lxa_html_token_t *token);

/* WHATWG tokenizer error code spelling, e.g. "duplicate-attribute";
 * the byte-decoding boundary uses "invalid-utf-8" for malformed input.
 * Diagnostics are recoverable and delivered in input order; offset is the
 * zero-based byte offset in the concatenated input stream (EOF may equal
 * total bytes). No callback means no diagnostics need be retained. A
 * non-OK return stops parsing.
 * Fatal errors return from feed/finish and stop token emission.
 */
typedef lxa_status_t (*lxa_html_error_callback_t)(void *context,
                                                   const char *code,
                                                   size_t offset);

typedef enum {
    LXA_HTML_DATA,
    LXA_HTML_RCDATA,
    LXA_HTML_RAWTEXT,
    LXA_HTML_SCRIPT_DATA,
    LXA_HTML_PLAINTEXT
} lxa_html_text_mode_t;

/* Parser results are separate from core allocation statuses. Unsupported
 * input is a candidate-implementation limit, not an HTML parse error.
 * CALLBACK_FAILURE retains the callback's exact lxa_status_t for inspection.
 */
typedef enum {
    LXA_HTML_OK = 0,
    LXA_HTML_BAD_ARGUMENT,
    LXA_HTML_UNSUPPORTED,
    LXA_HTML_OVERFLOW,
    LXA_HTML_NO_MEMORY,
    LXA_HTML_CALLBACK_FAILURE,
    LXA_HTML_LIMIT
} lxa_html_result_t;

/*
 * create copies allocator callbacks; its context must outlive destroy.
 * A successful create starts in DATA mode, ready for feed. The tokenizer
 * owns all pending bytes and decoded output, not the caller's chunks.
 * The caller must keep each chunk live until feed returns; NULL is permitted
 * only with length zero. Feed can emit zero or more tokens. Incomplete
 * UTF-8, CRLF, tags, and references remain pending across chunks and are
 * resolved at finish (EOF). Ill-formed UTF-8 is replaced with U+FFFD and
 * reported through on_error when provided. Character references are decoded
 * before token delivery, in text and attribute values alike. Chunking cannot
 * change the token stream.
 *
 * finish emits one EOF token after flushing pending input. Further feed or
 * finish calls are invalid until reset. A fatal error (including a non-OK
 * token callback return, OOM or overflow) is returned by feed/finish and
 * latched: no further callbacks run until reset. reset discards pending
 * bytes, errors and mode and restores DATA; it retains reusable capacity
 * and its MEMORY charges.
 * destroy frees that capacity. Neither reset nor destroy may run inside a
 * callback. feed/finish cannot be called recursively from a callback;
 * set_text_mode and allow_cdata may be. The tokenizer is confined to one
 * thread at a time.
 *
 * The candidate accepts UTF-8 DATA, tags with an ASCII first character,
 * attributes and character references. It recovers malformed comment
 * syntax, bogus comments, DOCTYPEs with quoted public/system identifiers,
 * and processing instructions with permitted ASCII targets. RCDATA, RAWTEXT,
 * unescaped/escaped/double-escaped script data, and PLAINTEXT are selected by
 * tree feedback; CDATA is selected only in foreign content. Invalid numeric
 * references recover as U+FFFD; control references use the spec's override
 * table. Input beyond configured token/name limits returns LIMIT. Remaining
 * candidate grammar gaps return UNSUPPORTED, never a recovered parse error.
 * Do not promote this candidate until the consumed grammar and tree are qualified.
 * This candidate charges accepted input, scanner work and all retained
 * tokenizer/codec objects and text/name capacity to the shared budget; it
 * checks token/name sizes before append. Diagnostics are callback-only.
 */
lxa_status_t lxa_html_tokenizer_create(
    const lxa_allocator_t *allocator, lxa_html_token_callback_t on_token,
    lxa_html_error_callback_t on_error, void *context,
    lxa_html_tokenizer_t **out);
/* budget is borrowed and must outlive the tokenizer. NULL selects an owned
 * default budget; reset restores only that owned budget. A shared budget
 * belongs to the caller and must be reset only after all its consumers end
 * and retained storage is released.
 */
lxa_status_t lxa_html_tokenizer_create_with_budget(
    const lxa_allocator_t *allocator, lxa_html_token_callback_t on_token,
    lxa_html_error_callback_t on_error, void *context,
    lxa_budget_t *budget, lxa_html_tokenizer_t **out);
void lxa_html_tokenizer_destroy(lxa_html_tokenizer_t *tokenizer);
void lxa_html_tokenizer_reset(lxa_html_tokenizer_t *tokenizer);
lxa_html_result_t lxa_html_tokenizer_feed(lxa_html_tokenizer_t *tokenizer,
                                           lxa_span_t input);
/* Internal complete-buffer decoder handoff. The caller already accepted the
 * bounded raw bytes into this same external budget and owns the decoded
 * UTF-8 until feed returns. Charge tokenizer work, but do not accept decoded
 * bytes as fresh input or grant a second work credit. Do not mix the two
 * feed methods within a parse session. No independent input may be supplied
 * through this entry point; reset ends the session.
 */
lxa_html_result_t lxa_html_tokenizer_feed_prepaid(
    lxa_html_tokenizer_t *tokenizer, lxa_span_t decoded_utf8);
lxa_html_result_t lxa_html_tokenizer_finish(lxa_html_tokenizer_t *tokenizer);
/* LXA_OK unless the last feed/finish failed in a callback. */
lxa_status_t lxa_html_tokenizer_callback_status(
    const lxa_html_tokenizer_t *tokenizer);
/* LXA_BUDGET_COUNT unless the last fatal result was LXA_HTML_LIMIT. */
lxa_limit_reason_t lxa_html_tokenizer_limit_reason(
    const lxa_html_tokenizer_t *tokenizer);

/*
 * Tree feedback can be set before input and from on_token, taking effect
 * after that token. RCDATA/RAWTEXT/SCRIPT_DATA take an ASCII alphabetic appropriate
 * end tag, copied on a successful call;
 * pass an empty span for DATA/PLAINTEXT. RCDATA/RAWTEXT/SCRIPT_DATA use
 * the ASCII-lowercased appropriate end tag. allow_cdata is the tree's
 * adjusted-current-node namespace decision for subsequent input.
 * Invalid calls leave the previous settings unchanged and return an error.
 * These methods cannot be used from on_error or after finish/fatal failure.
 */
lxa_html_result_t lxa_html_tokenizer_set_text_mode(
    lxa_html_tokenizer_t *tokenizer, lxa_html_text_mode_t mode,
    lxa_span_t appropriate_end_tag);
lxa_html_result_t lxa_html_tokenizer_allow_cdata(
    lxa_html_tokenizer_t *tokenizer, bool allow);

/* Complete-buffer HTML input boundary, before any in-document declaration.
 * A NULL encoding and zero prefix mean no recognized BOM. The caller still
 * owns the bytes and checks its input-size limit before decoding.
 */
lxa_status_t lxa_html_input_bom(lxa_span_t bytes,
                                const lxa_encoding_t **encoding,
                                size_t *prefix_length);

/* Prescan only the first 1024 bytes for <meta charset> or a pragma-qualified
 * <meta content>. The returned label borrows the input; found distinguishes
 * an empty label from no declaration.
 */
lxa_status_t lxa_html_input_meta_charset(lxa_span_t bytes,
                                         bool *found, lxa_span_t *label);

/* Extract the same borrowed label from a meta content attribute used by
 * the bounded prescan. The caller must first confirm a real meta element
 * with http-equiv=content-type, then resolve the label with
 * lxa_html_input_resolve_label. For late attributes, charge scanning work
 * to the document budget; this helper neither owns nor retains content.
 * found distinguishes a missing label from an empty one.
 */
lxa_status_t lxa_html_input_content_charset(lxa_span_t content,
                                             bool *found, lxa_span_t *label);

/* WHATWG HTML's case-sensitive <?xml ... encoding="..."> fallback is
 * considered only when the meta prescan found nothing. The borrowed label
 * is not validated as an available encoding here.
 */
lxa_status_t lxa_html_input_xml_encoding(lxa_span_t bytes,
                                         bool *found, lxa_span_t *label);

typedef enum {
    LXA_HTML_INPUT_OK,
    LXA_HTML_INPUT_BAD_ARGUMENT,
    LXA_HTML_INPUT_LIMIT,
    LXA_HTML_INPUT_UNKNOWN_ENCODING,
    LXA_HTML_INPUT_NO_MEMORY,
    LXA_HTML_INPUT_OVERFLOW,
    LXA_HTML_INPUT_CALLBACK_FAILURE
} lxa_html_input_result_t;

/* Resolve a tokenizer-confirmed meta label with the same HTML remapping as
 * prescan selection. The caller handles BOM precedence and chooses whether
 * this declaration applies; an unknown explicit label fails. */
lxa_html_input_result_t lxa_html_input_resolve_label(
    lxa_span_t label, const lxa_encoding_t **encoding);

/* Select a decoder before publishing any tree state. The caller provides its
 * complete-input size limit. A BOM
 * overrides declarations; otherwise the first prescanned declaration wins,
 * then the XML fallback, then UTF-8. Unknown explicit labels fail rather
 * than changing the default silently. On failure encoding is NULL and
 * prefix_length is zero. A positive prefix is stripped only for a BOM.
 * Decode with lxa_html_input_decode_report before prepaid tokenization.
 */
lxa_html_input_result_t lxa_html_input_select(
    lxa_span_t bytes, size_t max_input_bytes,
    const lxa_encoding_t **encoding, size_t *prefix_length);

/* Decode a selected complete input into caller-owned UTF-8. output must be
 * initialized, empty, and bound to budget. The call accepts raw input once
 * and charges decoding work; a downstream tokenizer using this same budget
 * must not accept decoded bytes as new input. On failure output stays empty.
 * malformed counts replacement events; it is published only on success.
 * No callbacks run during decoding. The caller keeps the budget alive until
 * output is destroyed. reason is optional; it identifies the failed INPUT,
 * WORK or MEMORY charge on LIMIT and is COUNT otherwise.
 */
lxa_html_input_result_t lxa_html_input_decode(
    lxa_span_t bytes, const lxa_encoding_t *encoding, size_t prefix_length,
    lxa_budget_t *budget, lxa_buffer_t *output, size_t *malformed,
    lxa_limit_reason_t *reason);

/* Optional raw-byte malformed sequence positions. Only the malformed path
 * incurs a second, budgeted decoder pass. Callbacks receive offsets into the
 * original input, including the BOM. Their non-OK status returns
 * CALLBACK_FAILURE without publication; prior callbacks cannot be undone.
 * callback_status gets the exact return, or OK when no callback fails.
 * Neither callback nor allocator may reenter
 * this decode operation. output is still published only on complete success.
 */
typedef lxa_status_t (*lxa_html_input_error_callback_t)(void *context,
                                                         size_t offset);
lxa_html_input_result_t lxa_html_input_decode_report(
    lxa_span_t bytes, const lxa_encoding_t *encoding, size_t prefix_length,
    lxa_budget_t *budget, lxa_buffer_t *output, size_t *malformed,
    lxa_limit_reason_t *reason, lxa_html_input_error_callback_t on_error,
    void *context, lxa_status_t *callback_status);

/* Re-decode a complete input already accepted by this budget, e.g. after a
 * tokenizer-confirmed late meta declaration. INPUT must equal bytes.length.
 * WORK/MEMORY are charged anew, without minting another input work credit.
 * Destroy the prior decoded buffer before retrying to bound peak memory.
 * Callbacks on a tentative decode should be deferred until the final
 * encoding is known; only the final pass should use on_error. */
lxa_html_input_result_t lxa_html_input_decode_prepaid_report(
    lxa_span_t bytes, const lxa_encoding_t *encoding, size_t prefix_length,
    lxa_budget_t *budget, lxa_buffer_t *output, size_t *malformed,
    lxa_limit_reason_t *reason, lxa_html_input_error_callback_t on_error,
    void *context, lxa_status_t *callback_status);

/* Emit raw-byte malformed diagnostics only after the final decoded tree is
 * accepted. The caller passes malformed from the final no-callback decode;
 * this second pass charges WORK and temporary MEMORY, not INPUT. On failure
 * the caller discards the candidate tree. Callback effects cannot be undone.
 */
lxa_html_input_result_t lxa_html_input_report_malformed(
    lxa_span_t bytes, const lxa_encoding_t *encoding, size_t prefix_length,
    size_t malformed, lxa_budget_t *budget, const lxa_allocator_t *allocator,
    lxa_html_input_error_callback_t on_error, void *context,
    lxa_status_t *callback_status, lxa_limit_reason_t *reason);

/* Private consumer candidate. A successful parse owns
 * one ID store; all returned references and borrowed spans expire at destroy.
 * The allocator context must remain alive until then. The complete input is
 * borrowed only during this call. The configured input limit applies to raw
 * complete input before BOM/declaration selection; unsupported explicit encodings
 * fail instead of falling back to UTF-8. Decoded UTF-8 is parser-owned until
 * tokenization finishes. Scripting mode is disabled. A failed parse publishes
 * no document.
 *
 * parser_result == UNSUPPORTED identifies candidate grammar gaps;
 * BAD_ARGUMENT identifies invalid arguments or an unknown encoding.
 * status distinguishes LIMIT/NO_MEMORY/OVERFLOW; reason identifies the
 * limited resource when known. Outputs may be NULL.
 */
typedef struct lxa_html_document lxa_html_document_t;

lxa_status_t lxa_html_document_parse(const lxa_allocator_t *allocator,
    const lxa_limits_t *limits, lxa_span_t input,
    lxa_html_result_t *parser_result, lxa_limit_reason_t *reason,
    lxa_html_document_t **out);
/* Optional diagnostic callback reports original raw-input byte offsets,
 * including a BOM. It runs after the final tree parse, before publication;
 * a non-OK return is propagated unchanged and destroys all parse state.
 * Previous callbacks cannot be undone. The callback must not reenter the
 * parser. Other results and ownership match lxa_html_document_parse.
 */
lxa_status_t lxa_html_document_parse_report(const lxa_allocator_t *allocator,
    const lxa_limits_t *limits, lxa_span_t input,
    lxa_html_input_error_callback_t on_malformed, void *context,
    lxa_html_result_t *parser_result, lxa_limit_reason_t *reason,
    lxa_html_document_t **out);
void lxa_html_document_destroy(lxa_html_document_t *document);

/* The store is owned by document. Use its checked traversal, name, attribute,
 * text and serialization operations; callers never own or reset it.
 * Missing head/body is represented by a zero handle.
 */
lxa_dom_nodes_t *lxa_html_document_nodes(lxa_html_document_t *document);
lxa_dom_ref_t lxa_html_document_root(const lxa_html_document_t *document);
lxa_dom_ref_t lxa_html_document_head(const lxa_html_document_t *document);
lxa_dom_ref_t lxa_html_document_body(const lxa_html_document_t *document);

#ifdef __cplusplus
}
#endif

#endif /* LIBLEXA_HTML_H */
