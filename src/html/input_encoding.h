#ifndef LIBLEXA_HTML_INPUT_ENCODING_H
#define LIBLEXA_HTML_INPUT_ENCODING_H

#include "liblexa/html.h"
#include "liblexa/encoding.h"
#include "html/tokenizer.h"

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
 * is not validated as an available encoding here. Unlike the meta prescan,
 * this allocation-free linear scan is bounded by the complete input length;
 * the selector checks the raw-input limit before calling it.
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

/* Resolve a tokenizer-confirmed meta label with the same remapping as the
 * meta prescan. The caller handles BOM precedence and chooses whether
 * this declaration applies; an unknown explicit label fails. */
lxa_html_input_result_t lxa_html_input_resolve_label(
    lxa_span_t label, const lxa_encoding_t **encoding);

/* Resolve an accepted HTML meta token's effective normalized attributes.
 * The tree/control owner decides eligibility and BOM/first-declaration policy.
 * charset takes precedence over pragma-qualified content, independent of
 * attribute order. No declaration returns OK with NULL encoding; an empty or
 * unknown explicit label fails. Views are borrowed only during the call.
 * Charge scanning WORK to budget; publish encoding only on success.
 */
lxa_html_input_result_t lxa_html_input_meta_encoding(
    const lxa_html_attribute_t *attributes, size_t count, lxa_budget_t *budget,
    const lxa_encoding_t **encoding, lxa_limit_reason_t *reason);

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

typedef struct {
    size_t decoded_end, raw_end;
    bool identity;
} lxa_html_input_mapping_t;

/* Opt-in raw-byte mapping for source-mode tokenization. mapping is an empty
 * vector of lxa_html_input_mapping_t bound to the same budget as output.
 * Publish both only on success. Offsets include a stripped BOM; malformed
 * sequences map to the raw bytes consumed by their replacement scalar.
 * Unchanged byte runs coalesce. No callbacks run; encoding is already selected.
 */
lxa_html_input_result_t lxa_html_input_decode_mapped(
    lxa_span_t bytes, const lxa_encoding_t *encoding, size_t prefix_length,
    lxa_budget_t *budget, lxa_buffer_t *output, lxa_vector_t *mapping,
    size_t *malformed, lxa_limit_reason_t *reason);

/* Rebuild both results after an encoding change, before direct effects.
 * INPUT must already equal bytes.length. Destroy both tentative results
 * before retrying; WORK/MEMORY are charged anew without new input credit.
 * Output and mapping have the same empty-state contract as decode_mapped.
 */
lxa_html_input_result_t lxa_html_input_decode_mapped_prepaid(
    lxa_span_t bytes, const lxa_encoding_t *encoding, size_t prefix_length,
    lxa_budget_t *budget, lxa_buffer_t *output, lxa_vector_t *mapping,
    size_t *malformed, lxa_limit_reason_t *reason);

/* Query a decoded scalar boundary or an offset inside an unchanged byte run.
 * Reject interior offsets in transcoded/replacement scalars. The mapping
 * must be an unmodified successful result of decode_mapped. */
lxa_status_t lxa_html_input_raw_offset(const lxa_vector_t *mapping,
    size_t decoded_offset, size_t *raw_offset);

/* Optional raw-byte malformed sequence positions. Only the malformed path
 * incurs a second, budgeted decoder pass. Callbacks receive offsets into the
 * original input, including the BOM. Their non-OK status returns
 * CALLBACK_FAILURE without publication; prior callbacks cannot be undone.
 * callback_status gets the exact return, or OK when no callback fails.
 * Neither callback nor allocator may reenter
 * this decode operation. output is still published only on complete success.
 */
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

#endif
