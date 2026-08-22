#ifndef LIBLEXA_HTML_FOREIGN_CONTEXT_H
#define LIBLEXA_HTML_FOREIGN_CONTEXT_H

#include <string.h>
#include "dom/namespace.h"
#include "html/tokenizer.h"

/* Classify a foreign current element from its adjusted name and namespace.
 * annotation_html is the resolved encoding-attribute predicate. Callers own
 * context and borrowed spans; no nodes or retained attribute storage are needed.
 */
typedef enum {
    LXA_HTML_FOREIGN_NONE,
    LXA_HTML_FOREIGN_SVG_HTML,
    LXA_HTML_FOREIGN_MATHML_TEXT,
    LXA_HTML_FOREIGN_ANNOTATION_HTML,
    LXA_HTML_FOREIGN_ANNOTATION_OTHER
} lxa_html_foreign_context_t;

static inline bool
lxa_html_foreign_named(lxa_span_t name, const char *text, size_t length)
{
    return name.length == length && name.data != NULL
        && memcmp(name.data, text, length) == 0;
}

static inline lxa_html_foreign_context_t
lxa_html_foreign_context(lxa_dom_namespace_t ns, lxa_span_t name,
                         bool annotation_html)
{
#define LXA_FOREIGN_NAMED(text) lxa_html_foreign_named(name, text, sizeof(text) - 1)
    if (ns == LXA_DOM_NS_SVG
        && (LXA_FOREIGN_NAMED("foreignObject") || LXA_FOREIGN_NAMED("title")
            || LXA_FOREIGN_NAMED("desc")))
        return LXA_HTML_FOREIGN_SVG_HTML;
    if (ns == LXA_DOM_NS_MATHML) {
        if (LXA_FOREIGN_NAMED("mi") || LXA_FOREIGN_NAMED("mo")
            || LXA_FOREIGN_NAMED("mn") || LXA_FOREIGN_NAMED("ms")
            || LXA_FOREIGN_NAMED("mtext"))
            return LXA_HTML_FOREIGN_MATHML_TEXT;
        if (LXA_FOREIGN_NAMED("annotation-xml"))
            return annotation_html ? LXA_HTML_FOREIGN_ANNOTATION_HTML
                                   : LXA_HTML_FOREIGN_ANNOTATION_OTHER;
    }
#undef LXA_FOREIGN_NAMED
    return LXA_HTML_FOREIGN_NONE;
}

static inline bool
lxa_html_foreign_integration(lxa_html_foreign_context_t context)
{
    return context == LXA_HTML_FOREIGN_SVG_HTML
        || context == LXA_HTML_FOREIGN_MATHML_TEXT
        || context == LXA_HTML_FOREIGN_ANNOTATION_HTML;
}

static inline bool
lxa_html_foreign_annotation_svg(lxa_html_foreign_context_t context,
                                const lxa_html_token_t *token)
{
    return (context == LXA_HTML_FOREIGN_ANNOTATION_HTML
            || context == LXA_HTML_FOREIGN_ANNOTATION_OTHER)
        && token->kind == LXA_HTML_START_TAG
        && lxa_html_foreign_named(token->name, "svg", sizeof("svg") - 1);
}

static inline bool
lxa_html_foreign_uses_html(lxa_html_foreign_context_t context,
                           const lxa_html_token_t *token)
{
    if (token->kind == LXA_HTML_EOF) return true;
    if (token->kind == LXA_HTML_TEXT)
        return lxa_html_foreign_integration(context);
    if (token->kind != LXA_HTML_START_TAG) return false;
    if (context == LXA_HTML_FOREIGN_MATHML_TEXT
        && (lxa_html_foreign_named(token->name, "mglyph", sizeof("mglyph") - 1)
            || lxa_html_foreign_named(token->name, "malignmark",
                                      sizeof("malignmark") - 1)))
        return false;
    return lxa_html_foreign_integration(context)
        || lxa_html_foreign_annotation_svg(context, token);
}

#endif
