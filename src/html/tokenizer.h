/* Copyright (C) 2026 Paul Moses */
#ifndef LIBLEXA_HTML_TOKENIZER_H
#define LIBLEXA_HTML_TOKENIZER_H

#include "liblexa/html.h"

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

#endif
