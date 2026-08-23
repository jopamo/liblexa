#include <string.h>
#include "html/tree_policy.h"
#include "html-categories.h"

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
