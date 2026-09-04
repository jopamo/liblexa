#include <assert.h>
#include <string.h>
#include "html/tree_policy.h"
#include "html/tree_categories.h"
#include "html/formatting_policy.h"
#include "html-tags.h"

lxa_status_t
lxa_html_tree_namespace_feedback(lxa_html_tokenizer_t *tokenizer,
    lxa_dom_namespace_t ns)
{
    if (tokenizer == NULL || (unsigned) ns < LXA_DOM_NS_HTML || (unsigned) ns > LXA_DOM_NS_SVG)
        return LXA_ERROR_ARGUMENT;
    return lxa_html_tokenizer_allow_cdata(tokenizer, ns != LXA_DOM_NS_HTML) == LXA_HTML_OK
        ? LXA_OK : LXA_ERROR_INTERNAL;
}

lxa_status_t
lxa_html_tree_text_start_feedback(lxa_html_tokenizer_t *tokenizer,
    lxa_span_t name, lxa_limit_reason_t *reason)
{
    if (tokenizer == NULL) return LXA_ERROR_ARGUMENT;
    lxa_html_text_mode_t mode;
    lxa_status_t status = lxa_html_tree_text_start_mode(name, &mode);
    if (status != LXA_OK) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    return lxa_html_tokenizer_set_text_mode_status(tokenizer, mode,
        mode == LXA_HTML_PLAINTEXT ? (lxa_span_t){(const uint8_t *) "", 0} : name,
        reason);
}

lxa_status_t
lxa_html_tree_text_start_mode(lxa_span_t name, lxa_html_text_mode_t *out)
{
    if (out == NULL) return LXA_ERROR_ARGUMENT;
    lxa_html_text_mode_t mode;
    if (lxa_html_tree_name_equals(name, "title", 5)
        || lxa_html_tree_name_equals(name, "textarea", 8))
        mode = LXA_HTML_RCDATA;
    else if (lxa_html_tree_name_equals(name, "script", 6))
        mode = LXA_HTML_SCRIPT_DATA;
    else if (lxa_html_tree_name_equals(name, "plaintext", 9))
        mode = LXA_HTML_PLAINTEXT;
    else if (lxa_html_tree_name_equals(name, "style", 5)
        || lxa_html_tree_name_equals(name, "xmp", 3)
        || lxa_html_tree_name_equals(name, "iframe", 6)
        || lxa_html_tree_name_equals(name, "noembed", 7)
        || lxa_html_tree_name_equals(name, "noframes", 8))
        mode = LXA_HTML_RAWTEXT;
    else return LXA_ERROR_ARGUMENT;
    *out = mode;
    return LXA_OK;
}

static bool
doctype_token_shape_valid(const lxa_html_token_t *token)
{
    if (token == NULL || token->kind != LXA_HTML_DOCTYPE) return false;
    const lxa_dom_doctype_data_t declaration = {
        .name = token->name, .public_id = token->public_id, .system_id = token->system_id,
        .has_public_id = token->has_public_id, .has_system_id = token->has_system_id,
        .force_quirks = token->force_quirks
    };
    return lxa_dom_doctype_shape_valid(&declaration);
}

bool
lxa_html_tree_token_shape_valid(const lxa_html_token_t *token)
{
    if (token == NULL
        || (unsigned) token->kind > LXA_HTML_PROCESSING_INSTRUCTION
        || (token->name.data == NULL && token->name.length != 0)
        || ((token->kind == LXA_HTML_START_TAG || token->kind == LXA_HTML_END_TAG)
            && token->name.length == 0)
        || (token->data.data == NULL && token->data.length != 0)
        || (token->attribute_count != 0 && token->attributes == NULL)
        || token->attribute_count > SIZE_MAX / sizeof(*token->attributes)
        || (token->kind != LXA_HTML_START_TAG && token->attribute_count != 0)
        || (token->kind == LXA_HTML_DOCTYPE && !doctype_token_shape_valid(token)))
        return false;
    for (size_t i = 0; i < token->attribute_count; ++i) {
        const lxa_html_attribute_t *attribute = &token->attributes[i];
        if (!lxa_dom_attribute_shape_valid(attribute->name, attribute->value))
            return false;
    }
    return true;
}

lxa_status_t
lxa_html_tree_after_body_action(const lxa_html_token_t *token,
    lxa_html_after_body_action_t *out)
{
    if (token == NULL || out == NULL
        || (unsigned) token->kind > LXA_HTML_PROCESSING_INSTRUCTION
        || token->kind == LXA_HTML_EOF || token->kind == LXA_HTML_COMMENT
        || token->kind == LXA_HTML_PROCESSING_INSTRUCTION || token->kind == LXA_HTML_DOCTYPE
        || ((token->kind == LXA_HTML_START_TAG || token->kind == LXA_HTML_END_TAG)
            && (token->name.data == NULL || token->name.length == 0))
        || (token->kind == LXA_HTML_TEXT
            && token->data.length != 0 && token->data.data == NULL))
        return LXA_ERROR_ARGUMENT;
    lxa_html_after_body_action_t action = LXA_HTML_AFTER_BODY_REPROCESS;
    if (token->kind == LXA_HTML_TEXT && lxa_html_tree_space_only(token->data))
        action = LXA_HTML_AFTER_BODY_SPACE_TEXT;
    else if (token->kind == LXA_HTML_START_TAG
        && lxa_html_tree_name_equals(token->name, "html", 4))
        action = LXA_HTML_AFTER_BODY_HTML_START;
    *out = action;
    return LXA_OK;
}

lxa_status_t
lxa_html_tree_prebody_end_action(lxa_html_root_mode_t mode,
    lxa_span_t name, lxa_html_prebody_end_action_t *out)
{
    if (out == NULL || name.data == NULL || name.length == 0
        || (mode != LXA_TREE_BEFORE_HTML && mode != LXA_TREE_BEFORE_HEAD
            && mode != LXA_TREE_AFTER_HEAD))
        return LXA_ERROR_ARGUMENT;
    lxa_html_prebody_end_action_t action = LXA_HTML_PREBODY_END_IGNORE;
    if (lxa_html_tree_name_equals(name, "body", 4)
        || lxa_html_tree_name_equals(name, "html", 4)
        || lxa_html_tree_name_equals(name, "br", 2)
        || (mode != LXA_TREE_AFTER_HEAD
            && lxa_html_tree_name_equals(name, "head", 4))) {
        action = mode == LXA_TREE_BEFORE_HTML ? LXA_HTML_PREBODY_END_HTML_HEAD
            : mode == LXA_TREE_BEFORE_HEAD ? LXA_HTML_PREBODY_END_HEAD
            : LXA_HTML_PREBODY_END_BODY;
    }
    *out = action;
    return LXA_OK;
}

lxa_status_t
lxa_html_tree_frameset_action(lxa_html_root_mode_t mode,
    lxa_html_token_kind_t kind, lxa_span_t name, lxa_html_frameset_action_t *out)
{
    if (out == NULL
        || (mode != LXA_TREE_IN_FRAMESET && mode != LXA_TREE_AFTER_FRAMESET
            && mode != LXA_TREE_AFTER_AFTER_FRAMESET)
        || (unsigned) kind > LXA_HTML_PROCESSING_INSTRUCTION
        || ((kind == LXA_HTML_START_TAG || kind == LXA_HTML_END_TAG)
            && (name.data == NULL || name.length == 0)))
        return LXA_ERROR_ARGUMENT;
    lxa_html_frameset_action_t action = LXA_HTML_FRAMESET_IGNORE;
    if (kind == LXA_HTML_EOF) action = LXA_HTML_FRAMESET_EOF;
    else if (kind == LXA_HTML_COMMENT || kind == LXA_HTML_PROCESSING_INSTRUCTION)
        action = LXA_HTML_FRAMESET_COMMENT;
    else if (kind == LXA_HTML_TEXT) action = LXA_HTML_FRAMESET_TEXT;
    else if (kind == LXA_HTML_START_TAG) {
        if (lxa_html_tree_name_equals(name, "html", 4))
            action = LXA_HTML_FRAMESET_HTML_START;
        else if (lxa_html_tree_name_equals(name, "noframes", 8))
            action = LXA_HTML_FRAMESET_NOFRAMES_START;
        else if (mode == LXA_TREE_IN_FRAMESET) {
            if (lxa_html_tree_name_equals(name, "frameset", 8))
                action = LXA_HTML_FRAMESET_START;
            else if (lxa_html_tree_name_equals(name, "frame", 5))
                action = LXA_HTML_FRAMESET_FRAME_START;
        }
    }
    else if (kind == LXA_HTML_END_TAG) {
        if (mode == LXA_TREE_IN_FRAMESET
            && lxa_html_tree_name_equals(name, "frameset", 8))
            action = LXA_HTML_FRAMESET_END;
        else if (mode != LXA_TREE_IN_FRAMESET
            && lxa_html_tree_name_equals(name, "html", 4))
            action = LXA_HTML_FRAMESET_HTML_END;
    }
    *out = action;
    return LXA_OK;
}

lxa_status_t
lxa_html_tree_head_noscript_plan(const lxa_html_token_t *token,
    lxa_budget_t *budget, lxa_limit_reason_t *reason,
    lxa_html_head_noscript_plan_t *out)
{
    if (reason != NULL) *reason = LXA_BUDGET_COUNT;
    if (token == NULL || budget == NULL || out == NULL
        || (unsigned) token->kind > LXA_HTML_PROCESSING_INSTRUCTION
        || token->kind == LXA_HTML_EOF
        || ((token->kind == LXA_HTML_START_TAG || token->kind == LXA_HTML_END_TAG)
            && (token->name.data == NULL || token->name.length == 0))
        || (token->kind == LXA_HTML_TEXT
            && token->data.length != 0 && token->data.data == NULL))
        return LXA_ERROR_ARGUMENT;
    lxa_html_head_noscript_plan_t plan = {LXA_HTML_HEAD_NOSCRIPT_REPROCESS, 0};
    if (token->kind == LXA_HTML_END_TAG) {
        if (lxa_html_tree_name_equals(token->name, "noscript", 8))
            plan.action = LXA_HTML_HEAD_NOSCRIPT_CLOSE;
        else if (!lxa_html_tree_name_equals(token->name, "br", 2))
            plan.action = LXA_HTML_HEAD_NOSCRIPT_IGNORE;
    }
    else if (token->kind == LXA_HTML_START_TAG) {
        if (lxa_html_tree_name_equals(token->name, "head", 4)
            || lxa_html_tree_name_equals(token->name, "noscript", 8))
            plan.action = LXA_HTML_HEAD_NOSCRIPT_IGNORE;
        else if (lxa_html_tree_name_equals(token->name, "html", 4)
            || lxa_html_tree_name_equals(token->name, "basefont", 8)
            || lxa_html_tree_name_equals(token->name, "bgsound", 7)
            || lxa_html_tree_name_equals(token->name, "link", 4)
            || lxa_html_tree_name_equals(token->name, "meta", 4)
            || lxa_html_tree_name_equals(token->name, "style", 5)
            || lxa_html_tree_name_equals(token->name, "noframes", 8))
            plan.action = LXA_HTML_HEAD_NOSCRIPT_CONTINUE;
    }
    else if (token->kind == LXA_HTML_TEXT) {
        while (plan.space_prefix < token->data.length
            && lxa_html_byte_tree_space(token->data.data[plan.space_prefix]))
            ++plan.space_prefix;
        lxa_status_t status = lxa_budget_charge(budget, LXA_BUDGET_WORK,
            plan.space_prefix + (plan.space_prefix < token->data.length), reason);
        if (status != LXA_OK) return status;
        if (plan.space_prefix != 0 && plan.space_prefix == token->data.length)
            plan.action = LXA_HTML_HEAD_NOSCRIPT_IGNORE;
    }
    else plan.action = LXA_HTML_HEAD_NOSCRIPT_CONTINUE;
    *out = plan;
    return LXA_OK;
}

typedef enum {
    LXA_START_ATTRIBUTE_HIDDEN_INPUT, LXA_START_ATTRIBUTE_FONT_BREAKOUT
} start_attribute_query_t;

static lxa_status_t
start_attribute_query(const lxa_html_token_t *token, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, start_attribute_query_t query, bool *out)
{
    if (reason != NULL) *reason = LXA_BUDGET_COUNT;
    bool font = query == LXA_START_ATTRIBUTE_FONT_BREAKOUT;
    if (budget == NULL || out == NULL || !lxa_html_formatting_token_shape_valid(token)
        || !(font ? lxa_html_tree_name_equals(token->name, "font", 4)
                  : lxa_html_tree_name_equals(token->name, "input", 5)))
        return LXA_ERROR_ARGUMENT;
    const lxa_html_formatting_view_t view = {
        token->name, token->attributes, token->attribute_count, budget, reason
    };
    bool result = false;
    for (size_t i = 0; i < token->attribute_count; ++i) {
        lxa_span_t name, value;
        lxa_status_t status = lxa_budget_charge(budget, LXA_BUDGET_WORK, 1, reason);
        if (status == LXA_OK)
            status = lxa_html_formatting_view_read(&view, i + 1, &name, &value);
        if (status != LXA_OK) return status;
        if (font) {
            if (lxa_html_tree_name_equals(name, "color", 5)
                || lxa_html_tree_name_equals(name, "face", 4)
                || lxa_html_tree_name_equals(name, "size", 4)) {
                result = true;
                break;
            }
        }
        else if (lxa_html_tree_name_equals(name, "type", 4)) {
            result = lxa_html_tree_ascii_case_equals(value, "hidden", 6);
            break;
        }
    }
    *out = result;
    return LXA_OK;
}

lxa_status_t
lxa_html_tree_hidden_input(const lxa_html_token_t *token, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, bool *out)
{
    return start_attribute_query(token, budget, reason, LXA_START_ATTRIBUTE_HIDDEN_INPUT, out);
}

lxa_status_t
lxa_html_tree_font_breakout(const lxa_html_token_t *token, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, bool *out)
{
    return start_attribute_query(token, budget, reason, LXA_START_ATTRIBUTE_FONT_BREAKOUT, out);
}

lxa_status_t
lxa_html_tree_virtual_table_start_plan(lxa_html_template_mode_t mode,
    lxa_span_t target, lxa_html_virtual_table_start_plan_t *out)
{
    if (out == NULL || (unsigned) mode > LXA_TEMPLATE_BODY
        || !lxa_html_tree_table_part(target)
        || lxa_html_tree_name_equals(target, "table", 5))
        return LXA_ERROR_ARGUMENT;
    bool row = lxa_html_tree_name_equals(target, "tr", 2);
    bool cell = lxa_html_tree_name_equals(target, "td", 2)
        || lxa_html_tree_name_equals(target, "th", 2);
    lxa_html_virtual_table_start_plan_t plan = {1, false, false, false};
    if (mode == LXA_TEMPLATE_ROW || mode == LXA_TEMPLATE_TABLE_BODY) {
        plan.ignore = !cell && !(row && mode == LXA_TEMPLATE_TABLE_BODY);
        plan.implied_row = cell && mode == LXA_TEMPLATE_TABLE_BODY;
        if (plan.implied_row) plan.extra = 2;
    }
    else if (mode == LXA_TEMPLATE_TABLE && (row || cell)) {
        plan.implied_section = true;
        plan.implied_row = cell;
        plan.extra = cell ? 3 : 2;
    }
    *out = plan;
    return LXA_OK;
}

lxa_status_t
lxa_html_tree_table_start_plan(const lxa_html_template_table_scan_t *scan,
    size_t length, lxa_span_t top, lxa_span_t target, lxa_html_table_start_plan_t *out)
{
    if (scan == NULL || out == NULL || scan->table >= length
        || scan->context < scan->table || scan->context >= length
        || (scan->caption != SIZE_MAX
            && (scan->caption <= scan->table || scan->caption >= length))
        || (scan->cell != SIZE_MAX
            && (scan->cell <= scan->table || scan->cell >= length))
        || top.data == NULL || top.length == 0
        || !lxa_html_tree_table_part(target)
        || lxa_html_tree_name_equals(target, "table", 5))
        return LXA_ERROR_ARGUMENT;
    bool caption_open = scan->caption != SIZE_MAX;
    if (caption_open && scan->caption != scan->table + 1)
        return LXA_ERROR_INTERNAL;
    bool clear_ordinary = scan->ordinary_above && !caption_open && scan->cell == SIZE_MAX;
    bool row = lxa_html_tree_name_equals(target, "tr", 2);
    bool cell = lxa_html_tree_name_equals(target, "td", 2)
        || lxa_html_tree_name_equals(target, "th", 2);
    bool top_table = lxa_html_tree_name_equals(top, "table", 5);
    bool top_row = lxa_html_tree_name_equals(top, "tr", 2);
    bool top_section = lxa_html_tree_table_section(top);
    lxa_html_table_start_plan_t plan = {
        clear_ordinary ? scan->context + 1 : length, 1,
        (top_table || caption_open || scan->group_open) && (row || cell),
        cell && (top_table || caption_open || scan->group_open || top_section)
    };
    if (lxa_html_tree_name_equals(target, "col", 3)
        || lxa_html_tree_name_equals(target, "colgroup", 8)
        || lxa_html_tree_table_section(target))
        plan.keep = scan->table + 1;
    else if (lxa_html_tree_name_equals(target, "caption", 7)) {
        if (caption_open || scan->cell != SIZE_MAX || scan->group_open
            || top_section || top_row)
            plan.keep = scan->table + 1;
        else if (!top_table) return LXA_ERROR_INTERNAL;
    }
    else if (row) {
        if (top_table || caption_open || scan->group_open) {
            plan.extra = 2;
            if (caption_open || scan->group_open) plan.keep = scan->table + 1;
        }
        else if (top_row) plan.keep = clear_ordinary ? scan->context : length - 1;
        else if (scan->cell != SIZE_MAX) plan.keep = scan->cell - 1;
        else if (!top_section) return LXA_ERROR_INTERNAL;
    }
    else {
        if (top_table || caption_open || scan->group_open) {
            plan.extra = 3;
            if (caption_open || scan->group_open) plan.keep = scan->table + 1;
        }
        else if (top_section) plan.extra = 2;
        else if (scan->cell != SIZE_MAX) plan.keep = scan->cell;
        else if (!top_row) return LXA_ERROR_INTERNAL;
    }
    if (plan.keep <= scan->table) return LXA_ERROR_INTERNAL;
    *out = plan;
    return LXA_OK;
}

lxa_status_t
lxa_html_tree_template_column_group(lxa_html_template_mode_t mode,
    bool group_open, lxa_html_token_kind_t kind, lxa_span_t name,
    lxa_html_column_group_action_t *out)
{
    if (out == NULL || (unsigned) mode > LXA_TEMPLATE_BODY
        || !lxa_html_select_arguments_valid(kind, name))
        return LXA_ERROR_ARGUMENT;
    lxa_html_column_group_action_t action = LXA_HTML_COLUMN_GROUP_CONTINUE;
    if (mode == LXA_TEMPLATE_COLUMN_GROUP && !group_open) {
        if (kind == LXA_HTML_TEXT)
            action = LXA_HTML_COLUMN_GROUP_SPACE_TEXT;
        else if (kind == LXA_HTML_END_TAG
            || (kind == LXA_HTML_START_TAG
                && !lxa_html_tree_name_equals(name, "col", 3)
                && !lxa_html_tree_name_equals(name, "template", 8)
                && !lxa_html_tree_name_equals(name, "html", 4)))
            action = LXA_HTML_COLUMN_GROUP_IGNORE;
    }
    *out = action;
    return LXA_OK;
}

lxa_status_t
lxa_html_tree_template_start_mode(lxa_html_template_mode_t current,
    lxa_span_t name, lxa_dom_namespace_t ns, lxa_html_template_mode_t *out)
{
    if (out == NULL || (unsigned) current > LXA_TEMPLATE_BODY
        || name.data == NULL || name.length == 0
        || (unsigned) ns < LXA_DOM_NS_HTML || (unsigned) ns > LXA_DOM_NS_SVG)
        return LXA_ERROR_ARGUMENT;
    lxa_html_template_mode_t mode = current;
    if (current == LXA_TEMPLATE_INITIAL) {
        mode = LXA_TEMPLATE_BODY;
        if (ns == LXA_DOM_NS_HTML) {
#define LXA_TEMPLATE_NAMED(text) lxa_html_tree_name_equals(name, text, sizeof(text) - 1)
            if (LXA_TEMPLATE_NAMED("caption") || LXA_TEMPLATE_NAMED("colgroup")
                || LXA_TEMPLATE_NAMED("tbody") || LXA_TEMPLATE_NAMED("tfoot")
                || LXA_TEMPLATE_NAMED("thead"))
                mode = LXA_TEMPLATE_TABLE;
            else if (LXA_TEMPLATE_NAMED("col")) mode = LXA_TEMPLATE_COLUMN_GROUP;
            else if (LXA_TEMPLATE_NAMED("tr")) mode = LXA_TEMPLATE_TABLE_BODY;
            else if (LXA_TEMPLATE_NAMED("td") || LXA_TEMPLATE_NAMED("th"))
                mode = LXA_TEMPLATE_ROW;
            else if (LXA_TEMPLATE_NAMED("base") || LXA_TEMPLATE_NAMED("basefont")
                || LXA_TEMPLATE_NAMED("bgsound") || LXA_TEMPLATE_NAMED("link")
                || LXA_TEMPLATE_NAMED("meta") || LXA_TEMPLATE_NAMED("noframes")
                || LXA_TEMPLATE_NAMED("script") || LXA_TEMPLATE_NAMED("style")
                || LXA_TEMPLATE_NAMED("template") || LXA_TEMPLATE_NAMED("title"))
                mode = LXA_TEMPLATE_INITIAL;
#undef LXA_TEMPLATE_NAMED
        }
    }
    *out = mode;
    return LXA_OK;
}

lxa_status_t
lxa_html_tree_template_depth_check(lxa_budget_t *budget, size_t root_ancestors,
    size_t templates, size_t elements, lxa_limit_reason_t *reason)
{
    if (reason != NULL) *reason = LXA_BUDGET_COUNT;
    if (budget == NULL || templates == 0) return LXA_ERROR_ARGUMENT;
    size_t depth;
    lxa_status_t status = lxa_size_mul(templates, 2, &depth);
    if (status == LXA_OK) status = lxa_size_add(depth, 2, &depth);
    if (status == LXA_OK) status = lxa_size_add(depth, elements, &depth);
    if (status == LXA_OK) status = lxa_size_add(depth, root_ancestors, &depth);
    if (status == LXA_OK)
        status = lxa_budget_check(budget, LXA_BUDGET_TREE_DEPTH, depth, reason);
    return status;
}

lxa_status_t
lxa_html_tree_doctype_mode(const lxa_html_token_t *data, lxa_budget_t *budget,
                           lxa_limit_reason_t *reason,
                           lxa_html_document_mode_t *mode)
{
    if (reason != NULL) *reason = LXA_BUDGET_COUNT;
    if (budget == NULL || mode == NULL || !doctype_token_shape_valid(data))
        return LXA_ERROR_ARGUMENT;
    lxa_status_t status = lxa_budget_charge(budget, LXA_BUDGET_WORK, 1, reason);
    if (status != LXA_OK) return status;
    *mode = LXA_HTML_NO_QUIRKS;
    if (data->force_quirks
        || !lxa_html_tree_name_equals(data->name, "html", sizeof("html") - 1)) {
        *mode = LXA_HTML_QUIRKS;
        return LXA_OK;
    }
    if (!data->has_public_id && !data->has_system_id) return LXA_OK;
    status = lxa_budget_charge(budget, LXA_BUDGET_WORK,
        LIBLEXA_HTML_DOCTYPE_RULE_COUNT, reason);
    if (status != LXA_OK) return status;
    for (size_t i = 0; i < LIBLEXA_HTML_DOCTYPE_RULE_COUNT; ++i) {
        const liblexa_html_doctype_rule *rule = &liblexa_html_doctype_rules[i];
        lxa_span_t value;
        bool exact = false;
        switch (rule->condition) {
        case LIBLEXA_DOCTYPE_SYSTEM_EXACT:
            if (!data->has_system_id) continue;
            value = data->system_id;
            exact = true;
            break;
        case LIBLEXA_DOCTYPE_PUBLIC_EXACT:
            if (!data->has_public_id) continue;
            value = data->public_id;
            exact = true;
            break;
        case LIBLEXA_DOCTYPE_PUBLIC_PREFIX_EMPTY_SYSTEM:
            if (data->has_system_id && data->system_id.length != 0) continue;
            if (!data->has_public_id) continue;
            value = data->public_id;
            break;
        case LIBLEXA_DOCTYPE_PUBLIC_PREFIX_NONEMPTY_SYSTEM:
            if (!data->has_system_id || data->system_id.length == 0) continue;
            if (!data->has_public_id) continue;
            value = data->public_id;
            break;
        case LIBLEXA_DOCTYPE_PUBLIC_PREFIX:
            if (!data->has_public_id) continue;
            value = data->public_id;
            break;
        default:
            return LXA_ERROR_INTERNAL;
        }
        if (value.length < rule->length
            || (exact && value.length != rule->length))
            continue;
        value.length = rule->length;
        status = lxa_budget_charge(budget, LXA_BUDGET_WORK,
            value.length / 8 + (value.length % 8 != 0), reason);
        if (status != LXA_OK) return status;
        if (lxa_html_tree_ascii_case_equals(value, rule->identifier, rule->length)) {
            *mode = rule->limited ? LXA_HTML_LIMITED_QUIRKS : LXA_HTML_QUIRKS;
            return LXA_OK;
        }
    }
    return LXA_OK;
}

#define LXA_CONTAINER_NAMED(name, text) \
    lxa_html_tree_name_equals((name), (text), sizeof(text) - 1)
static lxa_status_t
in_tag_table(lxa_span_t name, lxa_budget_t *budget, lxa_limit_reason_t *reason,
             const liblexa_html_element *table, size_t count, bool *found)
{
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        size_t shared = name.length < table[mid].length
                        ? name.length : table[mid].length;
        int order;
        lxa_status_t status = lxa_budget_charge(budget,
            LXA_BUDGET_WORK, 1, reason);
        if (status != LXA_OK) return status;
        order = memcmp(name.data, table[mid].name, shared);
        if (order == 0)
            order = (name.length > table[mid].length)
                    - (name.length < table[mid].length);
        if (order == 0) {
            *found = true;
            return LXA_OK;
        }
        if (order < 0) hi = mid;
        else lo = mid + 1;
    }
    *found = false;
    return LXA_OK;
}

lxa_status_t
lxa_html_tree_supported_container(lxa_span_t name, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, bool *supported)
{
    if (reason != NULL) *reason = LXA_BUDGET_COUNT;
    if (budget == NULL || supported == NULL || (name.length != 0 && name.data == NULL))
        return LXA_ERROR_ARGUMENT;
    bool known;
    lxa_status_t status;
    if (lxa_html_tree_simple_container(name) || LXA_CONTAINER_NAMED(name, "selectedcontent")
        || lxa_html_tree_formatting_marker_tag(name)
        || lxa_html_tree_ruby_annotation(name) || LXA_CONTAINER_NAMED(name, "ruby")
        || LXA_CONTAINER_NAMED(name, "bdi") || LXA_CONTAINER_NAMED(name, "bdo")
        || LXA_CONTAINER_NAMED(name, "datalist") || LXA_CONTAINER_NAMED(name, "output")
        || LXA_CONTAINER_NAMED(name, "progress") || LXA_CONTAINER_NAMED(name, "meter")
        || LXA_CONTAINER_NAMED(name, "picture")
        || LXA_CONTAINER_NAMED(name, "video") || LXA_CONTAINER_NAMED(name, "audio")
        || LXA_CONTAINER_NAMED(name, "map") || LXA_CONTAINER_NAMED(name, "canvas")
        || LXA_CONTAINER_NAMED(name, "legend") || LXA_CONTAINER_NAMED(name, "isindex")
        || LXA_CONTAINER_NAMED(name, "blink") || LXA_CONTAINER_NAMED(name, "menuitem")
        || LXA_CONTAINER_NAMED(name, "multicol")
        || LXA_CONTAINER_NAMED(name, "nextid") || LXA_CONTAINER_NAMED(name, "spacer")) {
        *supported = true;
        return LXA_OK;
    }
    /* <image> is a parser alias of <img>, not a generic unknown element. */
    if (name.length == 0 || LXA_CONTAINER_NAMED(name, "image")) {
        *supported = false;
        return LXA_OK;
    }
    status = in_tag_table(name, budget, reason, liblexa_html_elements,
                          LIBLEXA_HTML_ELEMENT_COUNT, &known);
    if (status != LXA_OK) return status;
    if (!known)
        status = in_tag_table(name, budget, reason, liblexa_html_obsolete_elements,
                              LIBLEXA_HTML_OBSOLETE_COUNT, &known);
    if (status == LXA_OK) *supported = !known;
    return status;
}

lxa_status_t
lxa_html_tree_fostered_descendant(lxa_span_t name, lxa_dom_namespace_t ns,
    lxa_budget_t *budget, lxa_limit_reason_t *reason, bool *out)
{
    if (reason != NULL) *reason = LXA_BUDGET_COUNT;
    if (budget == NULL || out == NULL || (name.length != 0 && name.data == NULL)
        || (unsigned) ns < LXA_DOM_NS_HTML || (unsigned) ns > LXA_DOM_NS_SVG)
        return LXA_ERROR_ARGUMENT;
    if (ns != LXA_DOM_NS_HTML) {
        *out = true;
        return LXA_OK;
    }
    bool result = lxa_html_tree_block_container(name) || lxa_html_tree_heading(name)
        || LXA_CONTAINER_NAMED(name, "p") || LXA_CONTAINER_NAMED(name, "li")
        || LXA_CONTAINER_NAMED(name, "form")
        || LXA_CONTAINER_NAMED(name, "dt") || LXA_CONTAINER_NAMED(name, "dd")
        || LXA_CONTAINER_NAMED(name, "ruby")
        || (lxa_html_tree_inline_container(name) && !lxa_html_tree_formatting_tag(name))
        || LXA_CONTAINER_NAMED(name, "rt") || LXA_CONTAINER_NAMED(name, "rp")
        || LXA_CONTAINER_NAMED(name, "datalist") || LXA_CONTAINER_NAMED(name, "output")
        || LXA_CONTAINER_NAMED(name, "progress") || LXA_CONTAINER_NAMED(name, "meter")
        || LXA_CONTAINER_NAMED(name, "map") || LXA_CONTAINER_NAMED(name, "picture")
        || LXA_CONTAINER_NAMED(name, "canvas") || LXA_CONTAINER_NAMED(name, "video")
        || LXA_CONTAINER_NAMED(name, "audio") || LXA_CONTAINER_NAMED(name, "object")
        || LXA_CONTAINER_NAMED(name, "legend")
        || LXA_CONTAINER_NAMED(name, "marquee") || LXA_CONTAINER_NAMED(name, "applet")
        || LXA_CONTAINER_NAMED(name, "noscript") || LXA_CONTAINER_NAMED(name, "select")
        || LXA_CONTAINER_NAMED(name, "option")
        || LXA_CONTAINER_NAMED(name, "optgroup")
        || LXA_CONTAINER_NAMED(name, "ins") || LXA_CONTAINER_NAMED(name, "slot")
        || LXA_CONTAINER_NAMED(name, "data") || LXA_CONTAINER_NAMED(name, "dfn")
        || LXA_CONTAINER_NAMED(name, "mark") || LXA_CONTAINER_NAMED(name, "time")
        || LXA_CONTAINER_NAMED(name, "del") || LXA_CONTAINER_NAMED(name, "samp")
        || LXA_CONTAINER_NAMED(name, "kbd") || LXA_CONTAINER_NAMED(name, "sub")
        || LXA_CONTAINER_NAMED(name, "sup") || LXA_CONTAINER_NAMED(name, "var")
        || lxa_html_tree_formatting_tag(name);
    lxa_status_t status = LXA_OK;
    if (!result)
        status = lxa_html_tree_supported_container(name, budget, reason, &result);
    if (status == LXA_OK) *out = result;
    return status;
}

#undef LXA_CONTAINER_NAMED
