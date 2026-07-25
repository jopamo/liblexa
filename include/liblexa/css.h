/* Copyright (C) 2026 Paul Moses */
#ifndef LIBLEXA_CSS_H
#define LIBLEXA_CSS_H

#include <stdbool.h>
#include "liblexa/base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CSS Syntax token identity. See docs/css-api.md for the librsvg contract. */
typedef enum {
    LXA_CSS_IDENT,
    LXA_CSS_FUNCTION,
    LXA_CSS_AT_KEYWORD,
    LXA_CSS_HASH,
    LXA_CSS_STRING,
    LXA_CSS_BAD_STRING,
    LXA_CSS_URL,
    LXA_CSS_BAD_URL,
    LXA_CSS_DELIM,
    LXA_CSS_NUMBER,
    LXA_CSS_PERCENTAGE,
    LXA_CSS_DIMENSION,
    LXA_CSS_WHITESPACE,
    LXA_CSS_CDO,
    LXA_CSS_CDC,
    LXA_CSS_COLON,
    LXA_CSS_SEMICOLON,
    LXA_CSS_COMMA,
    LXA_CSS_LEFT_SQUARE,
    LXA_CSS_RIGHT_SQUARE,
    LXA_CSS_LEFT_PAREN,
    LXA_CSS_RIGHT_PAREN,
    LXA_CSS_LEFT_BRACE,
    LXA_CSS_RIGHT_BRACE,
    LXA_CSS_EOF,
    LXA_CSS_TOKEN_COUNT
} lxa_css_token_kind_t;

/*
 * Source offset/length refer to original input bytes, before CSS input
 * preprocessing. Text is the cooked UTF-8 spelling (or dimension unit)
 * stored in the owning token list; it need not contain the raw source.
 * Numeric tokens carry the parsed number; delimiter carries its codepoint.
 * Flags distinguish integer numbers, explicit signs, and ID-type hashes.
 * Unused fields are zero. Parser errors are separate from tokens.
 */
enum {
    LXA_CSS_INTEGER = 1,
    LXA_CSS_SIGNED = 2,
    LXA_CSS_ID_HASH = 4
};
typedef struct {
    lxa_css_token_kind_t kind;
    size_t offset;
    size_t length;
    double number;
    uint32_t codepoint;
    unsigned flags;
} lxa_css_token_t;

/*
 * One writer owns the list and the copied allocator descriptor. The scanner
 * copies cooked text and commits its token together; append failure leaves the logical
 * lengths and all previous tokens unchanged. The source bytes are never
 * retained: callers wanting raw slices must retain the input separately.
 * get copies the token and borrows its text. That view is invalid after a
 * successful append, clear, or destroy, even when the buffer does not move.
 * Token indices remain stable until clear. No concurrent mutation/read.
 */
typedef struct lxa_css_tokens lxa_css_tokens_t;

/* Allocate an empty list. out is required and cleared on failure. A supplied
 * budget must outlive destroy; the object and retained capacities charge
 * MEMORY. NULL leaves token storage unbound, for the default byte scanner.
 * The allocator context must outlive destroy. Choose the budget once here;
 * it cannot be changed while tokens or borrowed views remain live. */
lxa_status_t lxa_css_tokens_create(const lxa_allocator_t *allocator,
    lxa_budget_t *budget, lxa_css_tokens_t **out);
lxa_budget_kind_t lxa_css_tokens_limit_reason(const lxa_css_tokens_t *tokens);
void lxa_css_tokens_destroy(lxa_css_tokens_t *tokens);
void lxa_css_tokens_clear(lxa_css_tokens_t *tokens);
size_t lxa_css_tokens_length(const lxa_css_tokens_t *tokens);
lxa_status_t lxa_css_tokens_get(lxa_css_tokens_t *tokens, size_t index,
                                lxa_css_token_t *token, lxa_span_t *text);

typedef enum {
    LXA_CSS_SCAN_OK,
    LXA_CSS_SCAN_INVALID,
    LXA_CSS_SCAN_UNSUPPORTED, /* syntax not implemented yet, not a CSS error */
    LXA_CSS_SCAN_NO_MEMORY,
    LXA_CSS_SCAN_OVERFLOW,
    LXA_CSS_SCAN_OUTPUT_FULL,
    LXA_CSS_SCAN_LIMIT
} lxa_css_scan_result_t;

typedef struct lxa_css_byte_stream lxa_css_byte_stream_t;

/* Stateful UTF-8 replacement decoding + CSS input preprocessing. feed
 * borrows each chunk only during the call; owned scalar records retain raw
 * byte ranges. At most 1 MiB of source input is accepted. No tokens are
 * published until seal; then drain appends at most capacity complete tokens
 * per call to caller-owned storage, leaving earlier output published on later
 * failure. A decoder/allocation failure latches the stream; an oversized
 * chunk is rejected without consumption and may be retried in smaller parts.
 * The feed progress count on failure includes bytes already consumed. Input is
 * not scanned for tokens until sealed, so feed itself has no output callback.
 */
lxa_css_scan_result_t lxa_css_byte_stream_create(
    const lxa_allocator_t *allocator, lxa_css_byte_stream_t **out);
/* The budget is caller-owned and exclusively used for this stream while feed
 * runs. Its configured limits and consumed work outlive stream destruction.
 * create without a budget uses a stream-owned default profile instead.
 * The stream, decoder, scalar vector and scanner scratch reserve MEMORY and
 * release it on destruction. With an external budget, create the token list
 * with the same budget before finish/drain. The list and budget must both
 * outlive the stream; an unbound or differently bound list is rejected
 * without advancing the scanner. Default streams allow unbound lists under
 * their independent 1 MiB input cap.
 */
lxa_css_scan_result_t lxa_css_byte_stream_create_budget(
    const lxa_allocator_t *allocator, lxa_budget_t *budget,
    lxa_css_byte_stream_t **out);
/* Last LIMIT reason; COUNT if no limit failed or stream is NULL. */
lxa_budget_kind_t lxa_css_byte_stream_limit_reason(
    const lxa_css_byte_stream_t *stream);
lxa_css_scan_result_t lxa_css_byte_stream_feed(lxa_css_byte_stream_t *stream,
    lxa_span_t bytes, size_t *consumed);
lxa_css_scan_result_t lxa_css_byte_stream_seal(lxa_css_byte_stream_t *stream);
/* OUTPUT_FULL is retryable, including for zero capacity. A successful drain
 * emits EOF once and closes the stream. The caller initializes/clears tokens
 * before the first drain; emitted tokens are never rolled back by a later
 * failure. No token or buffer pointer is retained in the stream.
 */
lxa_css_scan_result_t lxa_css_byte_stream_drain(lxa_css_byte_stream_t *stream,
    lxa_css_tokens_t *tokens, size_t capacity, size_t *produced);
/* Convenience seal-and-drain; clears tokens and retains the one-shot rollback
 * rule. Do not call after seal/drain has begun.
 */
lxa_css_scan_result_t lxa_css_byte_stream_finish(lxa_css_byte_stream_t *stream,
    lxa_css_tokens_t *tokens);
void lxa_css_byte_stream_destroy(lxa_css_byte_stream_t *stream);

/* Convenience one-shot UTF-8 CSS input. Initial BOM is discarded.
 * Input is borrowed external storage, disjoint from tokens' buffers.
 * On any failure the initialized token list is empty. No other encoding
 * detection or incremental token publication is implied.
 */
lxa_css_scan_result_t lxa_css_scan_bytes(lxa_span_t bytes,
                                         lxa_css_tokens_t *tokens);

#define LXA_CSS_BLOCK_NONE UINT32_MAX

/* Flat, append-only simple-block/function index over immutable tokens.
 * open/close are token indices; an unclosed block ends at the EOF token
 * index, or at token count when the list has no explicit EOF.
 * Children belong to their parent's token interval and carry its block ID.
 * No input or token-text pointer is retained. */
typedef struct {
    size_t open;
    size_t close;
    uint32_t parent;
    lxa_css_token_kind_t opener;
} lxa_css_block_t;

typedef struct {
    lxa_vector_t entries;
    lxa_budget_t local_budget;
    lxa_budget_t *budget;
    lxa_budget_kind_t limit_reason;
    size_t end; /* first EOF index, or token count without explicit EOF */
    bool ready;
} lxa_css_blocks_t;

/* budget, when supplied, must outlive destroy and must be the same budget
 * backing the input token list. NULL selects an owned bounded profile.
 * Build replaces the previous logical index; failure leaves it empty, while
 * retaining charged backing capacity until destroy. Work is never refunded.
 * Input must remain immutable through build and all subsequent queries;
 * clear/rebuild the index before changing the source list. One writer.
 * Do not copy/move an initialized index: its owned budget may point inside it.
 * Destroy before reinitializing.
 */
lxa_status_t lxa_css_blocks_init(lxa_css_blocks_t *blocks,
                                 const lxa_allocator_t *allocator,
                                 lxa_budget_t *budget);
lxa_status_t lxa_css_blocks_build(lxa_css_blocks_t *blocks,
                                  lxa_css_tokens_t *tokens);
lxa_status_t lxa_css_blocks_get(lxa_css_blocks_t *blocks, size_t index,
                                lxa_css_block_t *out);
/* CSS Syntax component values: an opener consumes through its matching close
 * (or EOF); an ordinary token consumes one token. Indices exclude EOF. Query
 * only after a successful build and before changing its source token list.
 * Each search/step charges WORK; LIMIT leaves out unchanged. The index owns
 * no token pointers, so retaining returned offsets requires retaining the
 * unmodified source list as well.
 */
typedef struct {
    size_t first;
    size_t after;
    bool closed; /* true only for a matching closing token */
} lxa_css_component_range_t;
lxa_status_t lxa_css_blocks_next(lxa_css_blocks_t *blocks, size_t from,
                                 lxa_css_component_range_t *out);
void lxa_css_blocks_destroy(lxa_css_blocks_t *blocks);

/* Borrowed indices into a token list, never pointers into its text storage.
 * Ranges exclude the semicolon/EOF. The source list must remain unchanged
 * until the caller has finished using these indices. Syntax rejection returns
 * OK with valid=false; resource and argument failures return their status.
 * This is generic declaration syntax, not property/value validation.
 */
typedef struct {
    size_t name;
    size_t value_first;
    size_t value_after;
    bool important;
    bool valid;
} lxa_css_declaration_t;

/* Parse exactly one declaration from [first, after). The caller has already
 * split at a top-level semicolon using the component-value index. A block
 * index built from the same immutable token list is required. */
lxa_status_t lxa_css_declaration_parse(lxa_css_tokens_t *tokens,
                                       lxa_css_blocks_t *blocks,
                                       size_t first, size_t after,
                                       lxa_css_declaration_t *out);

typedef enum {
    LXA_CSS_RULE_AT,
    LXA_CSS_RULE_QUALIFIED
} lxa_css_rule_kind_t;

/* Structural rule syntax only. All indices refer to the immutable token list
 * used to build blocks; no pointers, declarations or child rules are retained.
 * A block's contents are [block_open+1, block_after), where block_after is
 * its closing token index (or the EOF index for an unclosed block).
 */
typedef struct {
    lxa_css_rule_kind_t kind;
    size_t name; /* at-keyword index for AT rules */
    size_t prelude_first;
    size_t prelude_after;
    size_t block_open;
    size_t block_after;
    size_t after; /* first token after this rule, including its terminator */
    bool has_block;
    bool valid;
} lxa_css_rule_t;

/* Consume a single rule from [first, limit). Whitespace/CDO/CDC filtering,
 * rule-context validation and child parsing belong to the caller. A missing
 * qualified-rule block or a custom-property-shaped qualified prelude returns
 * OK with valid=false. Resource failures leave out invalid. WORK is charged
 * for every token inspected, plus component-index probes.
 */
lxa_status_t lxa_css_rule_parse(lxa_css_tokens_t *tokens,
                                 lxa_css_blocks_t *blocks, size_t first,
                                 size_t limit, lxa_css_rule_t *out);

/* Declaration-only view of a block. Unsupported nested rules/at-rules are
 * skipped as complete component ranges; they are never published as outer
 * declarations. Token indices require an unchanged source and block index.
 * The block index and its budget must outlive this list.
 */
typedef struct lxa_css_declarations lxa_css_declarations_t;

/* Allocate an empty list; clear *out on every failure. The allocator context
 * and block budget must outlive destroy. Object and backing storage charge
 * MEMORY; destroy accepts NULL and releases both. */
lxa_status_t lxa_css_declarations_create(const lxa_allocator_t *allocator,
    lxa_css_blocks_t *blocks, lxa_css_declarations_t **out);
/* Zero for NULL, unbuilt or rejected output. Queries do not charge WORK. */
size_t lxa_css_declarations_length(const lxa_css_declarations_t *list);
/* Last build limit: COUNT/false for NULL or no matching failure. */
lxa_budget_kind_t lxa_css_declarations_limit_reason(const lxa_css_declarations_t *list);
bool lxa_css_declarations_count_limit(const lxa_css_declarations_t *list);
/* [first, after) excludes the enclosing braces. max_count is a caller-owned
 * remaining allowance (e.g. for an entire imported stylesheet graph).
 * Invalid arguments leave previous output unchanged; after build starts,
 * any failure clears logical output while retaining charged capacity.
 * A count rejection returns LIMIT with the count_limit query true. */
lxa_status_t lxa_css_declarations_build(lxa_css_declarations_t *list,
                                        lxa_css_tokens_t *tokens,
                                        lxa_css_blocks_t *blocks,
                                        size_t first, size_t after,
                                        size_t max_count);
lxa_status_t lxa_css_declarations_get(lxa_css_declarations_t *list,
                                      size_t at, lxa_css_declaration_t *out);
void lxa_css_declarations_destroy(lxa_css_declarations_t *list);

/* Parse only a top-level @import URI and retain the media prelude range for
 * the consumer to check. No resource is fetched here. URI is cooked token
 * text borrowed until the token list changes. Unsupported/invalid syntax
 * returns OK with valid=false; parser budget failures return an error. */
typedef struct {
    lxa_span_t uri;
    size_t media_first;
    size_t media_after;
    bool valid;
} lxa_css_import_t;

lxa_status_t lxa_css_import_parse(lxa_css_tokens_t *tokens,
                                   lxa_css_blocks_t *blocks,
                                   const lxa_css_rule_t *rule,
                                   lxa_css_import_t *out);

/* Consumer's rendering medium is screen. Empty prelude (an unqualified
 * import) applies. Recognize plain screen/all/print and their only/not
 * qualifiers; unknown types are false before NOT, and unsupported
 * feature expressions are nonmatching. Comma groups
 * are ORed. No environment or resource loader lives in the parser. */
lxa_status_t lxa_css_media_screen(lxa_css_tokens_t *tokens,
                                   lxa_css_blocks_t *blocks,
                                   size_t first, size_t after, bool *applies);

/* Normalize one simple selector into the exact key used by librsvg's style
 * lookup: optional tag, one class, one ID, in that order. No matching is
 * performed here. Unsupported syntax returns OK with supported=false.
 * Output is caller-owned, NUL-terminated (length excludes terminator) and
 * empty on rejection or failure after argument validation (invalid arguments
 * leave it unchanged). The caller owns an unchanged token list,
 * built block index and bound output buffer throughout the call. */
lxa_status_t lxa_css_selector_key(lxa_css_tokens_t *tokens,
                                   lxa_css_blocks_t *blocks,
                                   size_t first, size_t after,
                                   size_t max_bytes, lxa_buffer_t *output,
                                   bool *supported);

typedef struct {
    size_t first;
    size_t after;
} lxa_css_selector_range_t;

/* Split a rule prelude at top-level commas; functions and blocks are opaque.
 * The caller decides whether a selector is supported. An empty segment makes
 * the whole list syntactically invalid (OK with length zero). Indices borrow
 * an unchanged token list and block index. The bound budget outlives destroy.
 */
typedef struct lxa_css_selector_list lxa_css_selector_list_t;

/* Allocate an empty list; clear *out on every failure. The allocator context
 * and block budget must outlive destroy. Object and backing storage charge
 * MEMORY; destroy accepts NULL and releases both. */
lxa_status_t lxa_css_selector_list_create(const lxa_allocator_t *allocator,
    lxa_css_blocks_t *blocks, lxa_css_selector_list_t **out);
/* Zero for NULL, unbuilt or rejected output. Queries do not charge WORK. */
size_t lxa_css_selector_list_length(const lxa_css_selector_list_t *list);
/* Last build limit: COUNT/false for NULL or no matching failure. */
lxa_budget_kind_t lxa_css_selector_list_limit_reason(const lxa_css_selector_list_t *list);
bool lxa_css_selector_list_count_limit(const lxa_css_selector_list_t *list);
/* Invalid arguments leave the previous output alone; all other failures
 * clear logical output, retaining charged capacity until destruction. */
lxa_status_t lxa_css_selector_list_build(lxa_css_selector_list_t *list,
    lxa_css_tokens_t *tokens, lxa_css_blocks_t *blocks,
    size_t first, size_t after, size_t max_count);
lxa_status_t lxa_css_selector_list_get(lxa_css_selector_list_t *list,
    size_t index, lxa_css_selector_range_t *out);
void lxa_css_selector_list_destroy(lxa_css_selector_list_t *list);

/* Serialize token text for the consumer's property parser. Original UTF-8
 * source is borrowed only during the call; it must match the immutable token
 * list, and the caller must validate UTF-8 and reject embedded NUL before
 * passing the result to a C-string consumer. Comments are omitted; cooked identifiers, hashes and
 * function names reflect CSS escapes, while numeric spelling, quoted strings
 * and component punctuation retain their source representation. The result
 * is caller-owned, NUL-terminated, and excludes !important when the range
 * comes from lxa_css_declaration_t. Failure clears logical output. */
lxa_status_t lxa_css_value_text(lxa_css_tokens_t *tokens,
                                 lxa_css_blocks_t *blocks,
                                 size_t first, size_t after,
                                 lxa_span_t source, size_t max_bytes,
                                 lxa_buffer_t *output);

#ifdef __cplusplus
}
#endif

#endif /* LIBLEXA_CSS_H */
