#ifndef LIBLEXA_HTML_TREE_POLICY_H
#define LIBLEXA_HTML_TREE_POLICY_H

#include <string.h>
#include "html/tokenizer.h"
#include "html/tokenizer_classes.h"
#include "dom/namespace.h"

typedef enum {
    LXA_HTML_NO_QUIRKS, LXA_HTML_LIMITED_QUIRKS, LXA_HTML_QUIRKS
} lxa_html_document_mode_t;

typedef enum {
    LXA_HTML_TABLE_CELL, LXA_HTML_TABLE_ROW, LXA_HTML_TABLE_SECTION
} lxa_html_table_group_t;

typedef enum {
    LXA_HTML_ROOT_TABLE_TEMPLATE, LXA_HTML_ROOT_TABLE_TEXT_CURRENT,
    LXA_HTML_ROOT_TABLE_PLAINTEXT, LXA_HTML_ROOT_TABLE_XMP,
    LXA_HTML_ROOT_TABLE_TEXT_FOSTER, LXA_HTML_ROOT_TABLE_VOID,
    LXA_HTML_ROOT_TABLE_SVG, LXA_HTML_ROOT_TABLE_MATH,
    LXA_HTML_ROOT_TABLE_CONTAINER, LXA_HTML_ROOT_TABLE_UNSUPPORTED
} lxa_html_root_table_start_action_t;

/* Accepted non-part/non-form start at a root HTML table context, after earlier
 * control/select/foreign routing. Special routes do no WORK; fallback uses
 * supported-container policy. Publish only on success; engines own effects.
 */
lxa_status_t lxa_html_tree_root_table_start_action(lxa_span_t name,
    lxa_budget_t *budget, lxa_limit_reason_t *reason,
    lxa_html_root_table_start_action_t *out);

/* Append accepted pending table bytes to an engine-owned budgeted buffer.
 * Check combined TOKEN_BYTES before charging incoming WORK and growing storage.
 * Failure preserves content/length; retained WORK and capacity belong to the
 * engine. Input must not overlap the buffer object or its allocation.
 */
lxa_status_t lxa_html_tree_queue_table_text(lxa_buffer_t *pending, lxa_span_t bytes,
    lxa_limit_reason_t *reason);

typedef enum {
    LXA_HTML_ROOT_ELEMENT_HTML, LXA_HTML_ROOT_ELEMENT_HEAD, LXA_HTML_ROOT_ELEMENT_BODY
} lxa_html_root_element_t;

typedef enum {
    LXA_HTML_ROOT_ORDINARY_HEAD_VOID, LXA_HTML_ROOT_ORDINARY_BODY_VOID,
    LXA_HTML_ROOT_ORDINARY_TABLE_PART, LXA_HTML_ROOT_ORDINARY_FOREIGN,
    LXA_HTML_ROOT_ORDINARY_FORMATTING, LXA_HTML_ROOT_ORDINARY_CONTAINER,
    LXA_HTML_ROOT_ORDINARY_UNSUPPORTED
} lxa_html_root_ordinary_start_action_t;

/* Accepted start after root/control/select/table and text/form/template routing.
 * Special routes do no WORK; fallback performs one supported-container query.
 * Publish only on success. Engines retain admission and mode effects.
 */
lxa_status_t lxa_html_tree_root_ordinary_start_action(lxa_span_t name,
    lxa_budget_t *budget, lxa_limit_reason_t *reason,
    lxa_html_root_ordinary_start_action_t *out);

typedef enum {
    LXA_TEMPLATE_INITIAL, LXA_TEMPLATE_TABLE, LXA_TEMPLATE_TABLE_BODY,
    LXA_TEMPLATE_ROW, LXA_TEMPLATE_COLUMN_GROUP, LXA_TEMPLATE_BODY
} lxa_html_template_mode_t;

static inline bool
lxa_html_tree_template_table_context(lxa_html_template_mode_t mode)
{
    return mode == LXA_TEMPLATE_TABLE || mode == LXA_TEMPLATE_TABLE_BODY
        || mode == LXA_TEMPLATE_ROW;
}

/* Apply only after foreign end dispatch accepts an HTML br end tag. */
static inline bool
lxa_html_tree_template_end_br_start(lxa_html_template_mode_t mode, bool has_content)
{
    return has_content || mode == LXA_TEMPLATE_TABLE || mode == LXA_TEMPLATE_TABLE_BODY
        || mode == LXA_TEMPLATE_ROW || mode == LXA_TEMPLATE_BODY;
}

typedef enum {
    LXA_TREE_INITIAL, LXA_TREE_BEFORE_HTML, LXA_TREE_BEFORE_HEAD,
    LXA_TREE_IN_HEAD, LXA_TREE_HEAD_NOSCRIPT, LXA_TREE_TEMPLATE_TEXT,
    LXA_TREE_AFTER_HEAD, LXA_TREE_IN_BODY,
    LXA_TREE_TEXT, LXA_TREE_AFTER_BODY, LXA_TREE_AFTER_HTML, LXA_TREE_DONE,
    LXA_TREE_IN_FRAMESET, LXA_TREE_AFTER_FRAMESET, LXA_TREE_AFTER_AFTER_FRAMESET
} lxa_html_root_mode_t;

typedef enum {
    LXA_HTML_ROOT_START_OTHER, LXA_HTML_ROOT_START_HTML,
    LXA_HTML_ROOT_START_HEAD, LXA_HTML_ROOT_START_BODY, LXA_HTML_ROOT_START_IGNORE_HEAD
} lxa_html_root_start_action_t;

/* Root dispatch after template/text/frameset and after-body routing.
 * Classify normalized names without WORK or retained views.
 */
lxa_status_t lxa_html_tree_root_start_action(lxa_html_root_mode_t mode,
    lxa_span_t name, lxa_html_root_start_action_t *out);

typedef enum {
    LXA_HTML_ROOT_SPECIAL_ORDINARY, LXA_HTML_ROOT_SPECIAL_FRAME,
    LXA_HTML_ROOT_SPECIAL_TEMPLATE_HEAD, LXA_HTML_ROOT_SPECIAL_TEMPLATE,
    LXA_HTML_ROOT_SPECIAL_HEAD_NOSCRIPT, LXA_HTML_ROOT_SPECIAL_FORM,
    LXA_HTML_ROOT_SPECIAL_TEXT, LXA_HTML_ROOT_SPECIAL_XMP,
    LXA_HTML_ROOT_SPECIAL_PLAINTEXT
} lxa_html_root_special_start_action_t;

/* After root/control/select/table routing. No WORK or effects; TEMPLATE_HEAD
 * requires head admission and IN_HEAD publication before template admission.
 * Disabled scripting selects head-noscript only through IN_HEAD.
 */
lxa_status_t lxa_html_tree_root_special_start_action(lxa_html_root_mode_t mode,
    lxa_span_t name, lxa_html_root_special_start_action_t *out);

typedef enum {
    LXA_HTML_ROOT_END_CONTINUE, LXA_HTML_ROOT_END_IGNORE, LXA_HTML_ROOT_END_BR_START
} lxa_html_root_end_prelude_action_t;

static inline bool
lxa_html_tree_frameset_mode(lxa_html_root_mode_t mode)
{
    return mode == LXA_TREE_IN_FRAMESET || mode == LXA_TREE_AFTER_FRAMESET
        || mode == LXA_TREE_AFTER_AFTER_FRAMESET;
}

/* In-body frameset replacement requires the admitted BODY at stack root. */
static inline bool
lxa_html_tree_frameset_body_eligible(bool frameset_ok, uint64_t body_identity,
    size_t open_length, uint64_t first_identity)
{
    return frameset_ok && body_identity != 0 && open_length != 0
        && first_identity == body_identity;
}

static inline bool
lxa_html_tree_root_mode_valid(lxa_html_root_mode_t mode)
{
    return (unsigned) mode <= LXA_TREE_AFTER_AFTER_FRAMESET;
}

static inline bool
lxa_html_tree_text_return_mode_valid(lxa_html_root_mode_t mode)
{
    return mode == LXA_TREE_IN_HEAD || mode == LXA_TREE_HEAD_NOSCRIPT
        || mode == LXA_TREE_TEMPLATE_TEXT || mode == LXA_TREE_AFTER_HEAD
        || mode == LXA_TREE_IN_BODY || lxa_html_tree_frameset_mode(mode);
}

static inline bool
lxa_html_tree_template_return_mode_valid(lxa_html_root_mode_t mode)
{
    return mode == LXA_TREE_IN_HEAD || mode == LXA_TREE_AFTER_HEAD
        || mode == LXA_TREE_IN_BODY;
}

/* Check mode membership before using this predicate to validate an owner. */
static inline bool
lxa_html_tree_mode_has_template(lxa_html_root_mode_t mode,
    lxa_html_root_mode_t text_return)
{
    return mode == LXA_TREE_TEMPLATE_TEXT
        || (mode == LXA_TREE_TEXT && text_return == LXA_TREE_TEMPLATE_TEXT);
}

static inline bool
lxa_html_tree_template_boundary_valid(size_t frames, size_t elements,
    size_t boundary, size_t previous)
{
    return frames == 0 || (boundary <= elements && (frames == 1 || boundary >= previous));
}

/* Child pops must leave one OPEN_ELEMENTS credit for the active template. */
static inline bool
lxa_html_tree_template_credit_valid(size_t elements, size_t boundary, uint64_t credits)
{
    return boundary <= elements && elements - boundary < credits;
}

/* These predicates borrow an already validated text span. */
static inline size_t
lxa_html_tree_space_prefix(lxa_span_t data)
{
    size_t prefix = 0;
    while (prefix < data.length && lxa_html_byte_tree_space(data.data[prefix]))
        ++prefix;
    return prefix;
}

static inline bool
lxa_html_tree_space_only(lxa_span_t data)
{
    for (size_t i = 0; i < data.length; ++i)
        if (!lxa_html_byte_tree_space(data.data[i])) return false;
    return true;
}

typedef enum {
    LXA_HTML_INITIAL_COMMENT, LXA_HTML_INITIAL_IGNORE,
    LXA_HTML_INITIAL_DOCTYPE, LXA_HTML_INITIAL_QUIRKS_REPROCESS
} lxa_html_initial_action_t;

/* Initial-mode intake after token-shape validation. Only text reads data. */
static inline lxa_html_initial_action_t
lxa_html_tree_initial_action(lxa_html_token_kind_t kind, lxa_span_t data)
{
    if (kind == LXA_HTML_COMMENT || kind == LXA_HTML_PROCESSING_INSTRUCTION)
        return LXA_HTML_INITIAL_COMMENT;
    if (kind == LXA_HTML_TEXT && lxa_html_tree_space_only(data))
        return LXA_HTML_INITIAL_IGNORE;
    return kind == LXA_HTML_DOCTYPE ? LXA_HTML_INITIAL_DOCTYPE
                                   : LXA_HTML_INITIAL_QUIRKS_REPROCESS;
}

typedef enum {
    LXA_HTML_ROOT_DATA_IGNORE,
    LXA_HTML_ROOT_DATA_TABLE,
    LXA_HTML_ROOT_DATA_NORMAL
} lxa_html_root_data_action_t;

static inline bool
lxa_html_tree_null_only(lxa_span_t data)
{
    for (size_t i = 0; i < data.length; ++i)
        if (data.data[i] != 0) return false;
    return true;
}

static inline bool
lxa_html_tree_null_or_space_only(lxa_span_t data)
{
    size_t i;
    for (i = 0; i < data.length; ++i) {
        uint8_t c = data.data[i];
        if (!lxa_html_byte_tree_space(c) && c != 0)
            return false;
    }
    return true;
}

static inline bool
lxa_html_tree_name_equals(lxa_span_t name, const char *text, size_t length)
{
    return name.length == length && name.data != NULL
        && memcmp(name.data, text, length) == 0;
}

static inline bool
lxa_html_tree_heading(lxa_span_t name)
{
    return name.length == 2 && name.data != NULL && name.data[0] == 'h'
        && name.data[1] >= '1' && name.data[1] <= '6';
}

static inline bool
lxa_html_tree_ascii_case_equals(lxa_span_t value, const char *expected,
                                size_t length)
{
    if (value.length != length || value.data == NULL) return false;
    for (size_t i = 0; i < length; ++i) {
        uint8_t c = value.data[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (uint8_t) expected[i]) return false;
    }
    return true;
}

/* Classify a borrowed DOCTYPE token without constructing a node. The caller
 * decides whether the declaration applies and commits the mode on success.
 */
lxa_status_t
lxa_html_tree_doctype_mode(const lxa_html_token_t *data, lxa_budget_t *budget,
                           lxa_limit_reason_t *reason,
                           lxa_html_document_mode_t *mode);

/* Validate the token envelope and every effective attribute before tree/control
 * effects. Budgeted attribute queries retain their separate lazy read checks.
 */
bool lxa_html_tree_token_shape_valid(const lxa_html_token_t *token);

/* Template geometry includes html/head-or-body, two slots per active template
 * (element and virtual content root), and retained content elements. For a body
 * context, root_ancestors is the root open-stack length minus its body entry;
 * head contexts use zero. Check the predicted depth without charging live quota.
 */
lxa_status_t
lxa_html_tree_template_depth_check(lxa_budget_t *budget, size_t root_ancestors,
    size_t templates, size_t elements, lxa_limit_reason_t *reason);

/* Select the mode for an accepted start in an active template context.
 * Head-rule starts retain INITIAL; table parts choose their virtual table
 * mode; other starts leave INITIAL for BODY. Noninitial modes are unchanged.
 * This does not dispatch, admit elements or apply tokenizer text feedback.
 * The engine owns effect ordering and nested-template save/restore.
 */
lxa_status_t
lxa_html_tree_template_start_mode(lxa_html_template_mode_t current,
    lxa_span_t name, lxa_dom_namespace_t ns, lxa_html_template_mode_t *out);

typedef enum {
    LXA_HTML_COLUMN_GROUP_CONTINUE, LXA_HTML_COLUMN_GROUP_IGNORE,
    LXA_HTML_COLUMN_GROUP_SPACE_TEXT
} lxa_html_column_group_action_t;

/* Apply virtual column-group gating after earlier template dispatch.
 * The engine supplies the current HTML-colgroup predicate and owns text effects.
 */
lxa_status_t
lxa_html_tree_template_column_group(lxa_html_template_mode_t mode,
    bool group_open, lxa_html_token_kind_t kind, lxa_span_t name,
    lxa_html_column_group_action_t *out);

/* Query decoded, normalized start attributes in source order. Charge one WORK
 * per visited attribute and publish only on success. Engines own dispatch.
 */
lxa_status_t
lxa_html_tree_hidden_input(const lxa_html_token_t *token, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, bool *out);
lxa_status_t
lxa_html_tree_font_breakout(const lxa_html_token_t *token, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, bool *out);

typedef enum {
    LXA_HTML_HEAD_NOSCRIPT_CONTINUE, LXA_HTML_HEAD_NOSCRIPT_IGNORE,
    LXA_HTML_HEAD_NOSCRIPT_CLOSE, LXA_HTML_HEAD_NOSCRIPT_REPROCESS
} lxa_html_head_noscript_action_t;

typedef struct {
    lxa_html_head_noscript_action_t action;
    size_t space_prefix;
} lxa_html_head_noscript_plan_t;

static inline lxa_html_root_mode_t
lxa_html_tree_head_noscript_next_mode(lxa_html_head_noscript_action_t action)
{
    return action == LXA_HTML_HEAD_NOSCRIPT_CLOSE || action == LXA_HTML_HEAD_NOSCRIPT_REPROCESS
        ? LXA_TREE_IN_HEAD : LXA_TREE_HEAD_NOSCRIPT;
}

/* Plan accepted head-noscript tokens after EOF/text-mode dispatch. Charge the
 * text prefix scan before effects; engines insert spaces and publish mode.
 */
lxa_status_t
lxa_html_tree_head_noscript_plan(const lxa_html_token_t *token,
    lxa_budget_t *budget, lxa_limit_reason_t *reason,
    lxa_html_head_noscript_plan_t *out);

typedef enum {
    LXA_HTML_FRAMESET_IGNORE, LXA_HTML_FRAMESET_EOF, LXA_HTML_FRAMESET_COMMENT,
    LXA_HTML_FRAMESET_HTML_START, LXA_HTML_FRAMESET_TEXT,
    LXA_HTML_FRAMESET_NOFRAMES_START, LXA_HTML_FRAMESET_HTML_END,
    LXA_HTML_FRAMESET_START, LXA_HTML_FRAMESET_FRAME_START, LXA_HTML_FRAMESET_END
} lxa_html_frameset_action_t;

/* Classify frameset-family dispatch without quota charges or effects. Engines
 * retain destination selection, text filtering, admission and mode publication.
 */
lxa_status_t
lxa_html_tree_frameset_action(lxa_html_root_mode_t mode,
    lxa_html_token_kind_t kind, lxa_span_t name, lxa_html_frameset_action_t *out);
/* Filter accepted space-only tree text into an empty initialized budgeted
 * buffer. Precharge the complete scan; retain partial output on append failure.
 * Input must not alias the output object or its allocation.
 */
lxa_status_t lxa_html_tree_filter_spaces(lxa_span_t data, lxa_buffer_t *spaces,
    lxa_limit_reason_t *reason);

typedef enum {
    LXA_HTML_PREBODY_END_IGNORE, LXA_HTML_PREBODY_END_HTML_HEAD,
    LXA_HTML_PREBODY_END_HEAD, LXA_HTML_PREBODY_END_BODY
} lxa_html_prebody_end_action_t;

/* Classify accepted end tags before html/head or after head. Engines construct
 * missing elements and publish each intermediate mode only after admission.
 */
lxa_status_t
lxa_html_tree_prebody_end_action(lxa_html_root_mode_t mode,
    lxa_span_t name, lxa_html_prebody_end_action_t *out);

typedef struct {
    size_t space_prefix;
    bool emit_prefix, ensure_head;
} lxa_html_prebody_text_plan_t;

typedef enum {
    LXA_HTML_HEAD_END_IGNORE, LXA_HTML_HEAD_END_CLOSE, LXA_HTML_HEAD_END_BODY,
    LXA_HTML_HEAD_END_HTML, LXA_HTML_HEAD_END_BR
} lxa_html_head_end_action_t;

lxa_status_t lxa_html_tree_head_end_action(lxa_span_t name,
    lxa_html_head_end_action_t *out);

typedef struct {
    lxa_html_root_mode_t return_mode;
    bool body, disable_frameset, skip_newline;
} lxa_html_root_text_plan_t;

/* Accepted root returning-text starts, including frameset-dispatched noframes.
 * Earlier table/foster, template, xmp and plaintext routing remains separate.
 * No WORK or retained views.
 */
lxa_status_t lxa_html_tree_root_text_plan(lxa_html_root_mode_t mode,
    lxa_span_t name, lxa_html_root_text_plan_t *out);

/* Plan accepted prebody text. Precharge the prefix plus one nonspace byte
 * before publishing; owners emit spaces, construct head and change mode.
 */
lxa_status_t lxa_html_tree_prebody_text_plan(lxa_html_root_mode_t mode,
    lxa_span_t data, lxa_budget_t *budget, lxa_limit_reason_t *reason,
    lxa_html_prebody_text_plan_t *out);

typedef enum {
    LXA_HTML_AFTER_BODY_SPACE_TEXT, LXA_HTML_AFTER_BODY_HTML_START,
    LXA_HTML_AFTER_BODY_REPROCESS
} lxa_html_after_body_action_t;

/* Apply after-body/after-html dispatch after earlier EOF, foreign, comment and
 * DOCTYPE rules. Engines retain reconstruction, text insertion and mode changes.
 */
lxa_status_t
lxa_html_tree_after_body_action(const lxa_html_token_t *token,
    lxa_html_after_body_action_t *out);

typedef struct {
    lxa_html_text_mode_t mode;
    lxa_span_t end_tag; /* immutable static storage; empty for plaintext */
} lxa_html_text_start_plan_t;

/* Select feedback only after an HTML text-start rule accepts the token.
 * Publish mode and a static end target only on success, retaining no token
 * views. This does not replace namespace, insertion-mode or recovery dispatch.
 */
lxa_status_t
lxa_html_tree_text_start_plan(lxa_span_t name, lxa_html_text_start_plan_t *out);

typedef enum {
    LXA_HTML_TEXT_APPEND, LXA_HTML_TEXT_RETURN
} lxa_html_text_action_t;

/* Consume the accepted pre/listing/textarea newline latch after intake and
 * table-text flushing. Non-text tokens do not inspect data. Return a borrowed
 * text view; invalid arguments preserve the latch and output.
 */
lxa_status_t
lxa_html_tree_leading_newline(bool *pending, lxa_html_token_kind_t kind,
    lxa_span_t data, lxa_span_t *out);

/* Dispatch accepted text-mode tokens. Resolve target only for an end tag;
 * text/EOF do not inspect name views. A mismatched end is a pipeline failure.
 * Publish only on success; engines own text effects and return-mode timing.
 */
lxa_status_t
lxa_html_tree_text_action(lxa_html_token_kind_t kind, lxa_span_t name,
    lxa_span_t target, lxa_html_text_action_t *out);

typedef struct {
    lxa_html_text_start_plan_t plan; /* DATA means inactive */
    lxa_html_root_mode_t return_mode;
} lxa_html_text_context_t;

static inline bool
lxa_html_text_context_valid(const lxa_html_text_context_t *context)
{
    return context != NULL
        && (context->plan.mode == LXA_HTML_RCDATA || context->plan.mode == LXA_HTML_RAWTEXT
            || context->plan.mode == LXA_HTML_SCRIPT_DATA)
        && context->plan.end_tag.data != NULL && context->plan.end_tag.length != 0
        && context->plan.end_tag.length <= 8
        && lxa_html_tree_text_return_mode_valid(context->return_mode);
}

/* Zero-initialized, single-thread-confined context owns only static target
 * storage and the return mode. Commit after successful tokenizer feedback;
 * failure preserves it. Plaintext does not enter a returning text context.
 * Engines publish their text owner and TEXT mode after success.
 */
lxa_status_t
lxa_html_text_context_enter(lxa_html_text_context_t *context,
    lxa_html_tokenizer_t *tokenizer, lxa_span_t name,
    lxa_html_root_mode_t return_mode, lxa_limit_reason_t *reason);
/* Restore the mode and clear the active context without allocation or WORK.
 * The stable output field must be separate from the context. Engines own
 * accepted end/EOF dispatch and clearing any text insertion owner.
 */
lxa_status_t
lxa_html_text_context_leave(lxa_html_text_context_t *context,
    lxa_html_root_mode_t *mode);
/* Validate returning TEXT intake without DOM effects. Report APPEND without
 * copying/retaining bytes, or restore the return mode and clear the context for
 * matching ends/EOF. Publish action only on success; failures preserve state and
 * output. Engines emit text, latch failures and reprocess EOF in the restored
 * root/template/frameset mode. Token/mode/action must not alias context or each other.
 */
lxa_status_t lxa_html_text_context_token(lxa_html_text_context_t *context,
    const lxa_html_token_t *token, lxa_html_root_mode_t *mode,
    lxa_html_text_action_t *action);

/* Apply feedback for an already accepted HTML text-start rule. An unknown name
 * is an internal dispatch mismatch. The tokenizer copies its end target;
 * engines publish their text owner/return mode only after this succeeds.
 */
lxa_status_t
lxa_html_tree_text_start_feedback(lxa_html_tokenizer_t *tokenizer,
    lxa_span_t name, lxa_limit_reason_t *reason);

/* Publish adjusted-current namespace feedback after successful token effects.
 * Integration eligibility does not change namespace. No WORK or allocation.
 * A rejected tokenizer setter is a pipeline-state failure.
 */
lxa_status_t
lxa_html_tree_namespace_feedback(lxa_html_tokenizer_t *tokenizer,
    lxa_dom_namespace_t ns);

#endif
