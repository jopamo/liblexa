#include <string.h>
#include "html/tree_policy.h"
#include "html-categories.h"

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
    if (data == NULL || budget == NULL || mode == NULL
        || data->kind != LXA_HTML_DOCTYPE
        || (data->name.data == NULL && data->name.length != 0)
        || (data->public_id.data == NULL && data->public_id.length != 0)
        || (data->system_id.data == NULL && data->system_id.length != 0)
        || (!data->has_public_id && data->public_id.length != 0)
        || (!data->has_system_id && data->system_id.length != 0))
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
