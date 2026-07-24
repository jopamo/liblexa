/* Copyright (C) 2026 Paul Moses */
#ifndef LIBLEXA_CSS_SCAN_H
#define LIBLEXA_CSS_SCAN_H

#include "liblexa/css.h"

/* Already decoded and CSS-preprocessed scalar with its original byte range.
 * Adjacent source ranges may include folded CRLF (one scalar, two bytes).
 * The caller owns the units; the result owns all cooked text. This bounded
 * one-shot entry point is a scanner slice.
 */
typedef struct {
    uint32_t value;
    size_t offset;
    size_t length;
} lxa_css_unit_t;

/* Cursor validates immutable scalar input once. Each batch appends at most
 * capacity complete tokens (including EOF) to caller-owned tokens. OUTPUT_FULL
 * leaves the cursor at the next token; the caller may inspect committed tokens
 * but text views expire on a later append. On a syntax/allocator failure,
 * previously emitted tokens remain; the cursor closes and cannot be retried.
 * The caller must not mutate/release the units until scanning ends. No pointer
 * into the units is retained by the cursor. Single writer, no concurrent reads.
 */
typedef struct {
    size_t position;
    size_t count;
    size_t source_length;
    lxa_budget_t local_budget;
    lxa_budget_t *budget;
    lxa_budget_kind_t limit_reason;
    bool closed;
} lxa_css_scan_cursor_t;

lxa_css_scan_result_t lxa_css_scan_cursor_init(lxa_css_scan_cursor_t *cursor,
    const lxa_css_unit_t *input, size_t count, size_t source_length);
/* Borrow a shared budget for item-size checks and later scanner accounting.
 * It must outlive the cursor and cannot be mutated concurrently. Passing NULL
 * uses the cursor's own bounded default profile.
 */
lxa_css_scan_result_t lxa_css_scan_cursor_init_budget(
    lxa_css_scan_cursor_t *cursor, const lxa_css_unit_t *input,
    size_t count, size_t source_length, lxa_budget_t *budget);
lxa_css_scan_result_t lxa_css_scan_cursor_batch(lxa_css_scan_cursor_t *cursor,
    const lxa_css_unit_t *input, lxa_css_tokens_t *tokens, size_t capacity,
    size_t *produced);

/* Input must be ordered, nonoverlapping, normalized Unicode scalar units.
 * source_length locates EOF in original bytes. At most 1 Mi units per call.
 * Tokens must be initialized and exclusively owned by the caller. Every
 * failure leaves the token list empty, including an allocation failure.
 */
lxa_css_scan_result_t lxa_css_scan_units(const lxa_css_unit_t *input,
    size_t count, size_t source_length, lxa_css_tokens_t *tokens);

#endif
