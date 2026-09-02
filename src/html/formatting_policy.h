#ifndef LIBLEXA_HTML_FORMATTING_POLICY_H
#define LIBLEXA_HTML_FORMATTING_POLICY_H

#include <string.h>
#include "html/tokenizer.h"
#include "dom/shape.h"

typedef enum {
    LXA_HTML_FORMATTING_ADOPT_ORDINARY,
    LXA_HTML_FORMATTING_ADOPT_REMOVE_CLOSED,
    LXA_HTML_FORMATTING_ADOPT_IGNORE,
    LXA_HTML_FORMATTING_ADOPT_POP,
    LXA_HTML_FORMATTING_ADOPT_REPAIR
} lxa_html_formatting_adopt_action_t;

typedef struct {
    lxa_html_formatting_adopt_action_t action;
    size_t formatting, open, furthest;
} lxa_html_formatting_adopt_plan_t;

static inline bool
lxa_html_formatting_token_shape_valid(const lxa_html_token_t *token)
{
    return token != NULL && token->kind == LXA_HTML_START_TAG
        && token->name.data != NULL && token->name.length != 0
        && (token->attribute_count == 0 || token->attributes != NULL)
        && token->attribute_count <= SIZE_MAX / sizeof(*token->attributes);
}

typedef struct {
    lxa_span_t name;
    const lxa_html_attribute_t *attributes;
    size_t count;
    lxa_budget_t *budget;
    lxa_limit_reason_t *reason;
} lxa_html_formatting_view_t;

static inline lxa_status_t
lxa_html_formatting_view_name(const lxa_html_formatting_view_t *view, lxa_span_t *name)
{
    *name = view->name;
    return LXA_OK;
}

static inline lxa_status_t
lxa_html_formatting_view_next(const lxa_html_formatting_view_t *view,
                              size_t cursor, size_t *out)
{
    *out = cursor == view->count ? 0 : cursor + 1;
    return LXA_OK;
}

static inline lxa_status_t
lxa_html_formatting_view_read(const lxa_html_formatting_view_t *view,
                              size_t cursor, lxa_span_t *name, lxa_span_t *value)
{
    const lxa_html_attribute_t *attribute = &view->attributes[cursor - 1];
    if (!lxa_dom_attribute_shape_valid(attribute->name, attribute->value))
        return LXA_ERROR_ARGUMENT;
    *name = attribute->name;
    *value = attribute->value;
    return LXA_OK;
}

#define LXA_FORMATTING_EQUAL_NAME lxa_html_formatting_view_equal
#define LXA_FORMATTING_VIEW lxa_html_formatting_view_t
#define LXA_FORMATTING_CURSOR size_t
#define LXA_FORMATTING_DONE(cursor) ((cursor) == 0)
#define LXA_FORMATTING_NAME lxa_html_formatting_view_name
#define LXA_FORMATTING_NEXT lxa_html_formatting_view_next
#define LXA_FORMATTING_READ lxa_html_formatting_view_read
#define LXA_FORMATTING_BUDGET(view) ((view)->budget)
#define LXA_FORMATTING_REASON(view) ((view)->reason)
#include "html/formatting_equal.inc"
#undef LXA_FORMATTING_EQUAL_NAME
#undef LXA_FORMATTING_VIEW
#undef LXA_FORMATTING_CURSOR
#undef LXA_FORMATTING_DONE
#undef LXA_FORMATTING_NAME
#undef LXA_FORMATTING_NEXT
#undef LXA_FORMATTING_READ
#undef LXA_FORMATTING_BUDGET
#undef LXA_FORMATTING_REASON

/* Compare normalized HTML names and effective, duplicate-suppressed attribute
 * sets as used by formatting admission. Borrow spans only until return.
 * Already accepted metadata incurs only comparison WORK, not DOM quotas.
 * Attribute order is irrelevant; decoded values compare byte-for-byte.
 * Publish equality only on success; no allocation or retained views.
 */
static inline lxa_status_t
lxa_html_formatting_equal(lxa_span_t name, const lxa_html_attribute_t *attributes,
    size_t count, const lxa_html_token_t *token, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, bool *out)
{
    if (reason != NULL) *reason = LXA_BUDGET_COUNT;
    if (out == NULL || budget == NULL || name.data == NULL || name.length == 0
        || (count != 0 && attributes == NULL)
        || count > SIZE_MAX / sizeof(*attributes))
        return LXA_ERROR_ARGUMENT;
    const lxa_html_formatting_view_t view = {name, attributes, count, budget, reason};
    return lxa_html_formatting_view_equal(&view, token, out);
}

#endif
