#ifndef LIBLEXA_HTML_TREE_CATEGORIES_H
#define LIBLEXA_HTML_TREE_CATEGORIES_H

#include "dom/namespace.h"
#include "html/tree_policy.h"
#include "html-categories.h"

/* Names are normalized HTML or adjusted foreign names. Predicates borrow
 * spans; stack owners retain traversal, WORK charges and mutation policy.
 */
typedef enum {
    LXA_HTML_SCOPE_NORMAL = 1,
    LXA_HTML_SCOPE_LIST_ITEM = 2,
    LXA_HTML_SCOPE_BUTTON = 4,
    LXA_HTML_SCOPE_TABLE = 8
} lxa_html_scope_kind_t;

static inline bool
lxa_html_scope_kind_valid(lxa_html_scope_kind_t scope)
{
    return scope == LXA_HTML_SCOPE_NORMAL || scope == LXA_HTML_SCOPE_LIST_ITEM
        || scope == LXA_HTML_SCOPE_BUTTON || scope == LXA_HTML_SCOPE_TABLE;
}

typedef struct {
    size_t search_length;
    size_t index; /* search_length when no target is found */
} lxa_html_end_target_plan_t;

typedef struct {
    size_t keep;
    bool foster;
} lxa_html_template_container_plan_t;

typedef struct {
    size_t keep;
    bool ignore;
} lxa_html_select_plan_t;

typedef struct {
    size_t keep;
    bool in_table;
} lxa_html_table_context_plan_t;

typedef struct {
    size_t keep, index;
    bool found; /* clear the form owner before pops only when found */
} lxa_html_form_end_plan_t;

typedef enum {
    LXA_HTML_TEMPLATE_END_CONTINUE, LXA_HTML_TEMPLATE_END_IGNORE,
    LXA_HTML_TEMPLATE_END_POP_TABLE, LXA_HTML_TEMPLATE_END_POP_MARKER
} lxa_html_template_end_action_t;

typedef struct {
    size_t keep;
    lxa_html_template_end_action_t action;
} lxa_html_template_end_plan_t;

typedef struct {
    size_t table, context, caption, cell; /* SIZE_MAX when absent */
    bool group_open, ordinary_above;
} lxa_html_template_table_scan_t;

typedef struct {
    size_t keep, extra;
    bool implied_section, implied_row;
} lxa_html_table_start_plan_t;

typedef enum {
    LXA_HTML_TABLE_START_INSERT, LXA_HTML_TABLE_START_IGNORE,
    LXA_HTML_TABLE_START_POP_IGNORE, LXA_HTML_TABLE_START_POP_REPROCESS
} lxa_html_table_start_action_t;

typedef struct {
    lxa_html_table_start_plan_t insertion;
    lxa_html_table_start_action_t action;
    size_t nodes_before_pop; /* POP_REPROCESS preflight */
    bool table_pop; /* POP_* cleanup; INSERT always uses table cleanup */
} lxa_html_template_table_start_plan_t;

typedef enum {
    LXA_HTML_COLUMN_START_VIRTUAL, LXA_HTML_COLUMN_START_IGNORE,
    LXA_HTML_COLUMN_START_POP_IGNORE, LXA_HTML_COLUMN_START_POP_REPROCESS,
    LXA_HTML_COLUMN_START_TABLE_PART, LXA_HTML_COLUMN_START_APPEND,
    LXA_HTML_COLUMN_START_IMPLY_GROUP
} lxa_html_column_start_action_t;

typedef struct {
    lxa_html_column_start_action_t action;
    size_t keep, nodes_before_pop;
} lxa_html_column_start_plan_t;

/* Plan a table-part start with an actual scoped table. top is the current name
 * after clearing ordinary entries to context; callers own that lookup/effect.
 */
lxa_status_t
lxa_html_tree_table_start_plan(const lxa_html_template_table_scan_t *scan,
    size_t length, lxa_span_t top, lxa_span_t target, lxa_html_table_start_plan_t *out);

typedef struct {
    size_t extra;
    bool implied_section, implied_row, ignore;
} lxa_html_virtual_table_start_plan_t;

/* With no actual table/context/cell/caption, plan a table-part start at the
 * active virtual boundary. Engines own clearing and admitting the suffix.
 */
lxa_status_t
lxa_html_tree_virtual_table_start_plan(lxa_html_template_mode_t mode,
    lxa_span_t target, lxa_html_virtual_table_start_plan_t *out);

/* Prepared actual table/section/row insertion after earlier recovery. Reuse
 * virtual-context child geometry; reject names requiring further recovery.
 */
lxa_status_t
lxa_html_tree_prepared_table_part_plan(lxa_span_t top, lxa_span_t target,
    lxa_html_virtual_table_start_plan_t *out);

lxa_status_t
lxa_html_tree_supported_container(lxa_span_t name, lxa_budget_t *budget,
    lxa_limit_reason_t *reason, bool *out);

lxa_status_t
lxa_html_tree_fostered_descendant(lxa_span_t name, lxa_dom_namespace_t ns,
    lxa_budget_t *budget, lxa_limit_reason_t *reason, bool *out);

static inline bool
lxa_html_tree_table_section(lxa_span_t name)
{
    return lxa_html_tree_name_equals(name, "tbody", 5)
        || lxa_html_tree_name_equals(name, "thead", 5)
        || lxa_html_tree_name_equals(name, "tfoot", 5);
}

static inline bool
lxa_html_tree_table_part(lxa_span_t name)
{
    return lxa_html_tree_name_equals(name, "table", 5)
        || lxa_html_tree_name_equals(name, "caption", 7)
        || lxa_html_tree_name_equals(name, "colgroup", 8)
        || lxa_html_tree_name_equals(name, "col", 3)
        || lxa_html_tree_table_section(name)
        || lxa_html_tree_name_equals(name, "tr", 2)
        || lxa_html_tree_name_equals(name, "td", 2)
        || lxa_html_tree_name_equals(name, "th", 2);
}

static inline bool
lxa_html_tree_table_context(lxa_span_t name)
{
    return lxa_html_tree_name_equals(name, "table", 5)
        || lxa_html_tree_name_equals(name, "colgroup", 8)
        || lxa_html_tree_table_section(name)
        || lxa_html_tree_name_equals(name, "tr", 2);
}

static inline bool
lxa_html_select_arguments_valid(lxa_html_token_kind_t kind, lxa_span_t name)
{
    return (unsigned) kind <= LXA_HTML_PROCESSING_INSTRUCTION
        && (name.length == 0 || name.data != NULL)
        && ((kind != LXA_HTML_START_TAG && kind != LXA_HTML_END_TAG)
            || name.length != 0);
}

static inline const char *
lxa_html_tree_namespace_name(lxa_dom_namespace_t ns)
{
    return ns == LXA_DOM_NS_HTML ? "html"
        : ns == LXA_DOM_NS_SVG ? "svg"
        : ns == LXA_DOM_NS_MATHML ? "mathml" : NULL;
}

static inline bool
lxa_html_tree_special_element(lxa_span_t name, lxa_dom_namespace_t ns)
{
    const char *namespace = lxa_html_tree_namespace_name(ns);
    if (namespace == NULL) return false;
    for (size_t i = 0; i < LIBLEXA_HTML_SPECIAL_COUNT; ++i) {
        const liblexa_html_special_tag *tag = &liblexa_html_special_tags[i];
        if (strcmp(tag->namespace_name, namespace) == 0
            && lxa_html_tree_name_equals(name, tag->tag, strlen(tag->tag)))
            return true;
    }
    return false;
}

/* Formatting and implied-end predicates require HTML-rule dispatch. */
static inline bool
lxa_html_tree_formatting_tag(lxa_span_t name)
{
    for (size_t i = 0; i < LIBLEXA_HTML_FORMATTING_COUNT; ++i) {
        const char *tag = liblexa_html_formatting_tags[i];
        if (lxa_html_tree_name_equals(name, tag, strlen(tag))) return true;
    }
    return false;
}

static inline bool
lxa_html_tree_formatting_marker_tag(lxa_span_t name)
{
#define LXA_MARKER_NAMED(text) lxa_html_tree_name_equals(name, text, sizeof(text) - 1)
    return LXA_MARKER_NAMED("td") || LXA_MARKER_NAMED("th")
        || LXA_MARKER_NAMED("caption") || LXA_MARKER_NAMED("object")
        || LXA_MARKER_NAMED("marquee") || LXA_MARKER_NAMED("applet");
#undef LXA_MARKER_NAMED
}

static inline bool
lxa_html_tree_scope_boundary(lxa_span_t name, lxa_dom_namespace_t ns,
                             lxa_html_scope_kind_t scope)
{
    const char *namespace = lxa_html_tree_namespace_name(ns);
    if (namespace == NULL) return false;
    for (size_t i = 0; i < LIBLEXA_HTML_SCOPE_BOUNDARY_COUNT; ++i) {
        const liblexa_html_scope_boundary *tag = &liblexa_html_scope_boundaries[i];
        if ((tag->mask & scope) != 0
            && strcmp(tag->namespace_name, namespace) == 0
            && lxa_html_tree_name_equals(name, tag->tag, strlen(tag->tag)))
            return true;
    }
    return false;
}

static inline bool
lxa_html_tree_implied_end_tag(lxa_span_t name)
{
    for (size_t i = 0; i < LIBLEXA_HTML_IMPLIED_END_COUNT; ++i) {
        const char *tag = liblexa_html_implied_end_tags[i];
        if (lxa_html_tree_name_equals(name, tag, strlen(tag))) return true;
    }
    return false;
}

#define LXA_CONTAINER_NAMED(name, text) \
    lxa_html_tree_name_equals((name), (text), sizeof(text) - 1)
static inline bool
lxa_html_tree_block_container(lxa_span_t name)
{
    return LXA_CONTAINER_NAMED(name, "div") || LXA_CONTAINER_NAMED(name, "section")
           || LXA_CONTAINER_NAMED(name, "address") || LXA_CONTAINER_NAMED(name, "dialog")
           || LXA_CONTAINER_NAMED(name, "dir") || LXA_CONTAINER_NAMED(name, "hgroup")
           || LXA_CONTAINER_NAMED(name, "search") || LXA_CONTAINER_NAMED(name, "summary")
           || LXA_CONTAINER_NAMED(name, "article") || LXA_CONTAINER_NAMED(name, "main")
           || LXA_CONTAINER_NAMED(name, "center") || LXA_CONTAINER_NAMED(name, "menu")
           || LXA_CONTAINER_NAMED(name, "aside")
           || LXA_CONTAINER_NAMED(name, "header") || LXA_CONTAINER_NAMED(name, "footer")
           || LXA_CONTAINER_NAMED(name, "nav") || LXA_CONTAINER_NAMED(name, "blockquote")
           || LXA_CONTAINER_NAMED(name, "pre") || LXA_CONTAINER_NAMED(name, "listing")
           || LXA_CONTAINER_NAMED(name, "figure") || LXA_CONTAINER_NAMED(name, "figcaption")
           || LXA_CONTAINER_NAMED(name, "fieldset")
           || LXA_CONTAINER_NAMED(name, "details") || LXA_CONTAINER_NAMED(name, "button")
           || LXA_CONTAINER_NAMED(name, "ul") || LXA_CONTAINER_NAMED(name, "ol")
           || LXA_CONTAINER_NAMED(name, "dl");
}


static inline bool
lxa_html_tree_inline_container(lxa_span_t name)
{
    return lxa_html_tree_formatting_tag(name) || LXA_CONTAINER_NAMED(name, "span")
           || LXA_CONTAINER_NAMED(name, "ins") || LXA_CONTAINER_NAMED(name, "slot")
           || LXA_CONTAINER_NAMED(name, "label")
           || LXA_CONTAINER_NAMED(name, "abbr") || LXA_CONTAINER_NAMED(name, "cite")
           || LXA_CONTAINER_NAMED(name, "data") || LXA_CONTAINER_NAMED(name, "dfn")
           || LXA_CONTAINER_NAMED(name, "mark") || LXA_CONTAINER_NAMED(name, "q")
           || LXA_CONTAINER_NAMED(name, "time")
           || LXA_CONTAINER_NAMED(name, "acronym") || LXA_CONTAINER_NAMED(name, "var")
           || LXA_CONTAINER_NAMED(name, "sub") || LXA_CONTAINER_NAMED(name, "sup")
           || LXA_CONTAINER_NAMED(name, "del")
           || LXA_CONTAINER_NAMED(name, "samp") || LXA_CONTAINER_NAMED(name, "kbd");
}


static inline bool
lxa_html_tree_select_container(lxa_span_t name)
{
    return LXA_CONTAINER_NAMED(name, "select") || LXA_CONTAINER_NAMED(name, "option")
        || LXA_CONTAINER_NAMED(name, "optgroup");
}


static inline bool
lxa_html_tree_simple_container(lxa_span_t name)
{
    return lxa_html_tree_block_container(name) || lxa_html_tree_inline_container(name)
           || LXA_CONTAINER_NAMED(name, "p") || LXA_CONTAINER_NAMED(name, "li")
           || LXA_CONTAINER_NAMED(name, "dt") || LXA_CONTAINER_NAMED(name, "dd")
           || LXA_CONTAINER_NAMED(name, "noscript")
           || lxa_html_tree_select_container(name)
           || lxa_html_tree_heading(name);
}


static inline bool
lxa_html_tree_ruby_annotation(lxa_span_t name)
{
    return LXA_CONTAINER_NAMED(name, "rb") || LXA_CONTAINER_NAMED(name, "rtc")
        || LXA_CONTAINER_NAMED(name, "rt") || LXA_CONTAINER_NAMED(name, "rp");
}

static inline bool
lxa_html_tree_container_reconstructs(lxa_span_t name)
{
    return !lxa_html_tree_ruby_annotation(name)
        && (lxa_html_tree_inline_container(name) || !lxa_html_tree_simple_container(name)
           || LXA_CONTAINER_NAMED(name, "noscript") || LXA_CONTAINER_NAMED(name, "button")
           || lxa_html_tree_select_container(name));
}

static inline bool
lxa_html_tree_template_container_reconstructs(lxa_span_t name)
{
    return lxa_html_tree_container_reconstructs(name)
        && !LXA_CONTAINER_NAMED(name, "table")
        && !LXA_CONTAINER_NAMED(name, "form")
        && !LXA_CONTAINER_NAMED(name, "button")
        && !LXA_CONTAINER_NAMED(name, "plaintext");
}

static inline bool
lxa_html_tree_head_void(lxa_span_t name)
{
    return LXA_CONTAINER_NAMED(name, "base") || LXA_CONTAINER_NAMED(name, "basefont")
           || LXA_CONTAINER_NAMED(name, "bgsound") || LXA_CONTAINER_NAMED(name, "link")
           || LXA_CONTAINER_NAMED(name, "meta");
}

static inline bool
lxa_html_tree_body_void(lxa_span_t name)
{
    return LXA_CONTAINER_NAMED(name, "br") || LXA_CONTAINER_NAMED(name, "img")
           || LXA_CONTAINER_NAMED(name, "input") || LXA_CONTAINER_NAMED(name, "keygen")
           || LXA_CONTAINER_NAMED(name, "hr")
           || LXA_CONTAINER_NAMED(name, "wbr") || LXA_CONTAINER_NAMED(name, "area")
           || LXA_CONTAINER_NAMED(name, "embed") || LXA_CONTAINER_NAMED(name, "source")
           || LXA_CONTAINER_NAMED(name, "track") || LXA_CONTAINER_NAMED(name, "param");
}

/* Accepted body-void/image starts only; head starts dispatch separately. */
static inline bool
lxa_html_tree_body_void_reconstructs(lxa_span_t name)
{
    return !LXA_CONTAINER_NAMED(name, "hr") && !LXA_CONTAINER_NAMED(name, "param")
        && !LXA_CONTAINER_NAMED(name, "source") && !LXA_CONTAINER_NAMED(name, "track");
}

static inline bool
lxa_html_tree_body_void_frameset_exempt(lxa_span_t name)
{
    return LXA_CONTAINER_NAMED(name, "param") || LXA_CONTAINER_NAMED(name, "source")
        || LXA_CONTAINER_NAMED(name, "track");
}

static inline bool
lxa_html_tree_body_control_start(lxa_span_t name)
{
    return LXA_CONTAINER_NAMED(name, "html") || LXA_CONTAINER_NAMED(name, "head")
        || LXA_CONTAINER_NAMED(name, "body")
        || LXA_CONTAINER_NAMED(name, "frameset") || LXA_CONTAINER_NAMED(name, "frame");
}

static inline bool
lxa_html_tree_root_control_group(lxa_html_token_kind_t kind, lxa_span_t name)
{
    return (kind == LXA_HTML_START_TAG && lxa_html_tree_body_control_start(name)
            && !LXA_CONTAINER_NAMED(name, "html"))
        || (kind == LXA_HTML_END_TAG && LXA_CONTAINER_NAMED(name, "head"));
}

static inline bool
lxa_html_tree_template_paragraph_container(lxa_span_t name)
{
    return (lxa_html_tree_block_container(name) && !LXA_CONTAINER_NAMED(name, "button"))
        || LXA_CONTAINER_NAMED(name, "p") || lxa_html_tree_heading(name);
}

static inline bool
lxa_html_tree_template_marker_container(lxa_span_t name)
{
    return LXA_CONTAINER_NAMED(name, "object") || LXA_CONTAINER_NAMED(name, "marquee")
        || LXA_CONTAINER_NAMED(name, "applet");
}

static inline bool
lxa_html_tree_template_list_item(lxa_span_t name)
{
    return LXA_CONTAINER_NAMED(name, "li") || LXA_CONTAINER_NAMED(name, "dt")
        || LXA_CONTAINER_NAMED(name, "dd");
}

static inline bool
lxa_html_tree_template_scoped_end(lxa_span_t name)
{
    return LXA_CONTAINER_NAMED(name, "p") || lxa_html_tree_template_list_item(name)
        || LXA_CONTAINER_NAMED(name, "form") || lxa_html_tree_block_container(name)
        || lxa_html_tree_heading(name);
}

static inline bool
lxa_html_tree_root_container_closes_paragraph(lxa_span_t name)
{
    return lxa_html_tree_template_paragraph_container(name)
        || lxa_html_tree_template_list_item(name);
}

static inline bool
lxa_html_tree_root_container_disables_frameset(lxa_span_t name)
{
    return LXA_CONTAINER_NAMED(name, "pre") || LXA_CONTAINER_NAMED(name, "listing")
        || lxa_html_tree_template_list_item(name) || LXA_CONTAINER_NAMED(name, "button")
        || LXA_CONTAINER_NAMED(name, "select") || lxa_html_tree_formatting_marker_tag(name);
}

/* Root scoped-end rules, after col/form/paragraph recovery; zero skips. */
static inline lxa_html_scope_kind_t
lxa_html_tree_root_end_scope(lxa_span_t name)
{
    if (lxa_html_tree_table_part(name) && !LXA_CONTAINER_NAMED(name, "col"))
        return LXA_HTML_SCOPE_TABLE;
    if (LXA_CONTAINER_NAMED(name, "li")) return LXA_HTML_SCOPE_LIST_ITEM;
    if (lxa_html_tree_template_list_item(name) || lxa_html_tree_block_container(name)
        || lxa_html_tree_heading(name) || lxa_html_tree_template_marker_container(name))
        return LXA_HTML_SCOPE_NORMAL;
    return 0;
}

typedef enum {
    LXA_HTML_ROOT_BODY_END_BODY, LXA_HTML_ROOT_BODY_END_HTML,
    LXA_HTML_ROOT_BODY_END_COL, LXA_HTML_ROOT_BODY_END_FORMATTING,
    LXA_HTML_ROOT_BODY_END_FORM, LXA_HTML_ROOT_BODY_END_PARAGRAPH,
    LXA_HTML_ROOT_BODY_END_SCOPED, LXA_HTML_ROOT_BODY_END_ORDINARY
} lxa_html_root_body_end_action_t;

typedef struct {
    lxa_html_root_body_end_action_t action;
    lxa_html_scope_kind_t scope;
} lxa_html_root_body_end_plan_t;

/* Accepted in-body ends after earlier foreign/template/head/br/select rules. */
static inline lxa_html_root_body_end_plan_t
lxa_html_tree_root_body_end_plan(lxa_span_t name)
{
    lxa_html_root_body_end_plan_t plan = {LXA_HTML_ROOT_BODY_END_ORDINARY, 0};
    if (LXA_CONTAINER_NAMED(name, "html")) plan.action = LXA_HTML_ROOT_BODY_END_HTML;
    else if (LXA_CONTAINER_NAMED(name, "body")) plan.action = LXA_HTML_ROOT_BODY_END_BODY;
    else if (LXA_CONTAINER_NAMED(name, "col")) plan.action = LXA_HTML_ROOT_BODY_END_COL;
    else if (lxa_html_tree_formatting_tag(name)) plan.action = LXA_HTML_ROOT_BODY_END_FORMATTING;
    else if (LXA_CONTAINER_NAMED(name, "form")) plan.action = LXA_HTML_ROOT_BODY_END_FORM;
    else if (LXA_CONTAINER_NAMED(name, "p")) plan.action = LXA_HTML_ROOT_BODY_END_PARAGRAPH;
    else if ((plan.scope = lxa_html_tree_root_end_scope(name)) != 0)
        plan.action = LXA_HTML_ROOT_BODY_END_SCOPED;
    return plan;
}

static inline bool
lxa_html_tree_template_ordinary_end(lxa_span_t name)
{
    if (lxa_html_tree_block_container(name) || lxa_html_tree_heading(name)
        || lxa_html_tree_table_part(name)
        || lxa_html_tree_template_marker_container(name)
        || LXA_CONTAINER_NAMED(name, "p") || LXA_CONTAINER_NAMED(name, "li")
        || LXA_CONTAINER_NAMED(name, "dt") || LXA_CONTAINER_NAMED(name, "dd")
        || LXA_CONTAINER_NAMED(name, "select") || LXA_CONTAINER_NAMED(name, "form")
        || LXA_CONTAINER_NAMED(name, "template") || LXA_CONTAINER_NAMED(name, "body")
        || LXA_CONTAINER_NAMED(name, "html")
        || LXA_CONTAINER_NAMED(name, "br"))
        return false;
    return !lxa_html_tree_formatting_tag(name);
}

static inline bool
lxa_html_tree_template_sectioning(lxa_span_t name)
{
    return LXA_CONTAINER_NAMED(name, "section") || LXA_CONTAINER_NAMED(name, "article")
        || LXA_CONTAINER_NAMED(name, "main") || LXA_CONTAINER_NAMED(name, "aside")
        || LXA_CONTAINER_NAMED(name, "nav") || LXA_CONTAINER_NAMED(name, "header")
        || LXA_CONTAINER_NAMED(name, "footer");
}

static inline bool
lxa_html_tree_returning_text_element(lxa_span_t name)
{
    return LXA_CONTAINER_NAMED(name, "title") || LXA_CONTAINER_NAMED(name, "style")
        || LXA_CONTAINER_NAMED(name, "script") || LXA_CONTAINER_NAMED(name, "textarea")
        || LXA_CONTAINER_NAMED(name, "iframe") || LXA_CONTAINER_NAMED(name, "noembed")
        || LXA_CONTAINER_NAMED(name, "noframes");
}

static inline bool
lxa_html_tree_template_html_text_element(lxa_span_t name)
{
    return lxa_html_tree_returning_text_element(name)
        || LXA_CONTAINER_NAMED(name, "xmp")
        || LXA_CONTAINER_NAMED(name, "plaintext");
}

/* Explicit template-container start categories. Earlier dispatch takes
 * precedence; use supported-container fallback when this predicate is false.
 */
static inline bool
lxa_html_tree_template_container_start(lxa_span_t name)
{
    return LXA_CONTAINER_NAMED(name, "form")
    || lxa_html_tree_template_marker_container(name)
    || lxa_html_tree_inline_container(name)
    || LXA_CONTAINER_NAMED(name, "plaintext")
    || LXA_CONTAINER_NAMED(name, "p") || LXA_CONTAINER_NAMED(name, "div")
    || LXA_CONTAINER_NAMED(name, "address")
    || LXA_CONTAINER_NAMED(name, "dialog")
    || LXA_CONTAINER_NAMED(name, "details")
    || LXA_CONTAINER_NAMED(name, "summary")
    || LXA_CONTAINER_NAMED(name, "fieldset")
    || lxa_html_tree_template_sectioning(name)
    || LXA_CONTAINER_NAMED(name, "pre")
    || LXA_CONTAINER_NAMED(name, "listing")
    || LXA_CONTAINER_NAMED(name, "blockquote")
    || LXA_CONTAINER_NAMED(name, "center")
    || LXA_CONTAINER_NAMED(name, "figure")
    || LXA_CONTAINER_NAMED(name, "figcaption")
    || LXA_CONTAINER_NAMED(name, "datalist")
    || LXA_CONTAINER_NAMED(name, "output")
    || LXA_CONTAINER_NAMED(name, "progress")
    || LXA_CONTAINER_NAMED(name, "meter")
    || LXA_CONTAINER_NAMED(name, "hgroup")
    || LXA_CONTAINER_NAMED(name, "search")
    || LXA_CONTAINER_NAMED(name, "dir")
    || LXA_CONTAINER_NAMED(name, "dl")
    || LXA_CONTAINER_NAMED(name, "dt")
    || LXA_CONTAINER_NAMED(name, "dd")
    || LXA_CONTAINER_NAMED(name, "noscript")
    || LXA_CONTAINER_NAMED(name, "ul")
    || LXA_CONTAINER_NAMED(name, "ol")
    || LXA_CONTAINER_NAMED(name, "menu")
    || LXA_CONTAINER_NAMED(name, "li")
    || LXA_CONTAINER_NAMED(name, "ruby")
    || LXA_CONTAINER_NAMED(name, "rt")
    || LXA_CONTAINER_NAMED(name, "rp")
    || lxa_html_tree_heading(name)
    || LXA_CONTAINER_NAMED(name, "table")
    || lxa_html_tree_select_container(name)
    || LXA_CONTAINER_NAMED(name, "span")
    || LXA_CONTAINER_NAMED(name, "label")
    || LXA_CONTAINER_NAMED(name, "button")
    || LXA_CONTAINER_NAMED(name, "q")
    || LXA_CONTAINER_NAMED(name, "cite")
    || LXA_CONTAINER_NAMED(name, "abbr")
    || LXA_CONTAINER_NAMED(name, "sub")
    || LXA_CONTAINER_NAMED(name, "sup")
    || LXA_CONTAINER_NAMED(name, "big")
    || LXA_CONTAINER_NAMED(name, "small")
    || LXA_CONTAINER_NAMED(name, "s")
    || LXA_CONTAINER_NAMED(name, "strike")
    || LXA_CONTAINER_NAMED(name, "tt")
    || LXA_CONTAINER_NAMED(name, "u")
    || LXA_CONTAINER_NAMED(name, "var")
    || LXA_CONTAINER_NAMED(name, "nobr")
    || LXA_CONTAINER_NAMED(name, "font")
    || lxa_html_tree_formatting_tag(name);
}

/* After earlier dispatch, for a current HTML colgroup. Start recovery also
 * requires the first content entry; engines own preflight, pops and reprocess.
 */
static inline bool
lxa_html_tree_template_colgroup_start_reprocess(lxa_span_t name)
{
    return !LXA_CONTAINER_NAMED(name, "col") && !LXA_CONTAINER_NAMED(name, "colgroup")
        && !lxa_html_tree_table_section(name) && !LXA_CONTAINER_NAMED(name, "caption")
        && !LXA_CONTAINER_NAMED(name, "tr") && !LXA_CONTAINER_NAMED(name, "td")
        && !LXA_CONTAINER_NAMED(name, "th") && !LXA_CONTAINER_NAMED(name, "html")
        && !LXA_CONTAINER_NAMED(name, "template");
}

static inline bool
lxa_html_tree_template_colgroup_end_reprocess(lxa_span_t name)
{
    return LXA_CONTAINER_NAMED(name, "select") || LXA_CONTAINER_NAMED(name, "frameset")
        || LXA_CONTAINER_NAMED(name, "body") || LXA_CONTAINER_NAMED(name, "html")
        || lxa_html_tree_heading(name) || LXA_CONTAINER_NAMED(name, "li")
        || LXA_CONTAINER_NAMED(name, "dt") || LXA_CONTAINER_NAMED(name, "dd")
        || LXA_CONTAINER_NAMED(name, "option") || LXA_CONTAINER_NAMED(name, "optgroup")
        || LXA_CONTAINER_NAMED(name, "ruby") || LXA_CONTAINER_NAMED(name, "rt")
        || LXA_CONTAINER_NAMED(name, "rp") || lxa_html_tree_block_container(name);
}

typedef enum {
    LXA_HTML_TEMPLATE_CURRENT_CONTINUE,
    LXA_HTML_TEMPLATE_CURRENT_TABLE,
    LXA_HTML_TEMPLATE_CURRENT_REPROCESS_START,
    LXA_HTML_TEMPLATE_CURRENT_REPROCESS_END
} lxa_html_template_current_action_t;

static inline bool
lxa_html_tree_foster_reconstructs(lxa_span_t name, lxa_dom_namespace_t ns,
    bool keep_open)
{
    return ns != LXA_DOM_NS_HTML
        || (keep_open && lxa_html_tree_container_reconstructs(name))
        || (lxa_html_tree_body_void(name) && lxa_html_tree_body_void_reconstructs(name));
}

static inline bool
lxa_html_tree_template_foster_text_element(lxa_span_t name)
{
    return LXA_CONTAINER_NAMED(name, "title") || LXA_CONTAINER_NAMED(name, "noframes")
        || LXA_CONTAINER_NAMED(name, "textarea") || LXA_CONTAINER_NAMED(name, "iframe")
        || LXA_CONTAINER_NAMED(name, "noembed") || LXA_CONTAINER_NAMED(name, "plaintext");
}

static inline bool
lxa_html_tree_template_foster_open_start(lxa_span_t name)
{
    return lxa_html_tree_formatting_tag(name)
        || lxa_html_tree_inline_container(name)
        || LXA_CONTAINER_NAMED(name, "object")
        || LXA_CONTAINER_NAMED(name, "marquee")
        || LXA_CONTAINER_NAMED(name, "applet")
        || LXA_CONTAINER_NAMED(name, "ruby")
        || LXA_CONTAINER_NAMED(name, "button")
        || LXA_CONTAINER_NAMED(name, "p")
        || LXA_CONTAINER_NAMED(name, "div")
        || LXA_CONTAINER_NAMED(name, "section")
        || LXA_CONTAINER_NAMED(name, "dialog")
        || LXA_CONTAINER_NAMED(name, "details")
        || LXA_CONTAINER_NAMED(name, "summary")
        || LXA_CONTAINER_NAMED(name, "fieldset")
        || LXA_CONTAINER_NAMED(name, "legend")
        || LXA_CONTAINER_NAMED(name, "blockquote")
        || LXA_CONTAINER_NAMED(name, "center")
        || LXA_CONTAINER_NAMED(name, "address")
        || LXA_CONTAINER_NAMED(name, "figure")
        || LXA_CONTAINER_NAMED(name, "figcaption")
        || LXA_CONTAINER_NAMED(name, "picture")
        || LXA_CONTAINER_NAMED(name, "video")
        || LXA_CONTAINER_NAMED(name, "audio")
        || LXA_CONTAINER_NAMED(name, "canvas")
        || LXA_CONTAINER_NAMED(name, "map")
        || LXA_CONTAINER_NAMED(name, "datalist")
        || LXA_CONTAINER_NAMED(name, "output")
        || LXA_CONTAINER_NAMED(name, "progress")
        || LXA_CONTAINER_NAMED(name, "meter")
        || LXA_CONTAINER_NAMED(name, "hgroup")
        || LXA_CONTAINER_NAMED(name, "search")
        || LXA_CONTAINER_NAMED(name, "dir")
        || LXA_CONTAINER_NAMED(name, "noscript")
        || LXA_CONTAINER_NAMED(name, "dl")
        || LXA_CONTAINER_NAMED(name, "dt")
        || LXA_CONTAINER_NAMED(name, "dd")
        || LXA_CONTAINER_NAMED(name, "article")
        || LXA_CONTAINER_NAMED(name, "main")
        || LXA_CONTAINER_NAMED(name, "aside")
        || LXA_CONTAINER_NAMED(name, "header")
        || LXA_CONTAINER_NAMED(name, "footer")
        || LXA_CONTAINER_NAMED(name, "nav")
        || LXA_CONTAINER_NAMED(name, "ul")
        || LXA_CONTAINER_NAMED(name, "ol")
        || LXA_CONTAINER_NAMED(name, "menu")
        || lxa_html_tree_heading(name)
        || LXA_CONTAINER_NAMED(name, "pre")
        || LXA_CONTAINER_NAMED(name, "listing")
        || LXA_CONTAINER_NAMED(name, "select")
        || LXA_CONTAINER_NAMED(name, "option")
        || LXA_CONTAINER_NAMED(name, "optgroup")
        || LXA_CONTAINER_NAMED(name, "rt")
        || LXA_CONTAINER_NAMED(name, "rp")
        || LXA_CONTAINER_NAMED(name, "li");
}

typedef enum {
    LXA_HTML_TEMPLATE_TABLE_START_CONTINUE,
    LXA_HTML_TEMPLATE_TABLE_START_COLUMN,
    LXA_HTML_TEMPLATE_TABLE_START_FOREIGN,
    LXA_HTML_TEMPLATE_TABLE_START_HEAD_VOID,
    LXA_HTML_TEMPLATE_TABLE_START_TEXT,
    LXA_HTML_TEMPLATE_TABLE_START_POP_GROUP,
    LXA_HTML_TEMPLATE_TABLE_START_XMP,
    LXA_HTML_TEMPLATE_TABLE_START_VOID,
    LXA_HTML_TEMPLATE_TABLE_START_OPEN,
    LXA_HTML_TEMPLATE_TABLE_START_PART,
    LXA_HTML_TEMPLATE_TABLE_START_FALLBACK
} lxa_html_template_table_start_action_t;

/* Accepted start in an actual HTML table context, after earlier dispatch.
 * FALLBACK retains the owner's budgeted supported-container lookup before pops.
 */
static inline lxa_html_template_table_start_action_t
lxa_html_tree_template_table_start_action(lxa_span_t name, bool column_group)
{
    if (LXA_CONTAINER_NAMED(name, "col")) return LXA_HTML_TEMPLATE_TABLE_START_COLUMN;
    if (LXA_CONTAINER_NAMED(name, "svg") || LXA_CONTAINER_NAMED(name, "math"))
        return LXA_HTML_TEMPLATE_TABLE_START_FOREIGN;
    if (lxa_html_tree_head_void(name)) return LXA_HTML_TEMPLATE_TABLE_START_HEAD_VOID;
    if (lxa_html_tree_template_foster_text_element(name)) return LXA_HTML_TEMPLATE_TABLE_START_TEXT;
    if (LXA_CONTAINER_NAMED(name, "style") || LXA_CONTAINER_NAMED(name, "script"))
        return column_group ? LXA_HTML_TEMPLATE_TABLE_START_POP_GROUP
                            : LXA_HTML_TEMPLATE_TABLE_START_CONTINUE;
    if (LXA_CONTAINER_NAMED(name, "xmp")) return LXA_HTML_TEMPLATE_TABLE_START_XMP;
    if (lxa_html_tree_body_void(name) || LXA_CONTAINER_NAMED(name, "image"))
        return LXA_HTML_TEMPLATE_TABLE_START_VOID;
    if (lxa_html_tree_template_foster_open_start(name)) return LXA_HTML_TEMPLATE_TABLE_START_OPEN;
    if (lxa_html_tree_table_section(name) || LXA_CONTAINER_NAMED(name, "caption")
        || LXA_CONTAINER_NAMED(name, "colgroup") || LXA_CONTAINER_NAMED(name, "tr")
        || LXA_CONTAINER_NAMED(name, "td") || LXA_CONTAINER_NAMED(name, "th"))
        return LXA_HTML_TEMPLATE_TABLE_START_PART;
    if (!LXA_CONTAINER_NAMED(name, "template") && !LXA_CONTAINER_NAMED(name, "table"))
        return LXA_HTML_TEMPLATE_TABLE_START_FALLBACK;
    return LXA_HTML_TEMPLATE_TABLE_START_CONTINUE;
}

typedef enum {
    LXA_HTML_TEMPLATE_TABLE_END_CONTINUE,
    LXA_HTML_TEMPLATE_TABLE_END_IGNORE,
    LXA_HTML_TEMPLATE_TABLE_END_POP_IGNORE,
    LXA_HTML_TEMPLATE_TABLE_END_PARAGRAPH,
    LXA_HTML_TEMPLATE_TABLE_END_ORDINARY,
    LXA_HTML_TEMPLATE_TABLE_END_FORMATTING
} lxa_html_template_table_end_action_t;

/* Accepted end in an actual HTML table context, after colgroup reprocessing. */
static inline lxa_html_template_table_end_action_t
lxa_html_tree_template_table_end_action(lxa_span_t name, bool column_group)
{
    if (LXA_CONTAINER_NAMED(name, "form"))
        return column_group ? LXA_HTML_TEMPLATE_TABLE_END_POP_IGNORE
                            : LXA_HTML_TEMPLATE_TABLE_END_IGNORE;
    if (LXA_CONTAINER_NAMED(name, "p")) return LXA_HTML_TEMPLATE_TABLE_END_PARAGRAPH;
    if (LXA_CONTAINER_NAMED(name, "body") || LXA_CONTAINER_NAMED(name, "html")
        || lxa_html_tree_heading(name) || lxa_html_tree_template_list_item(name)
        || LXA_CONTAINER_NAMED(name, "option") || LXA_CONTAINER_NAMED(name, "optgroup")
        || LXA_CONTAINER_NAMED(name, "ruby") || LXA_CONTAINER_NAMED(name, "rt")
        || LXA_CONTAINER_NAMED(name, "rp") || lxa_html_tree_block_container(name))
        return LXA_HTML_TEMPLATE_TABLE_END_IGNORE;
    if (LXA_CONTAINER_NAMED(name, "frameset"))
        return column_group ? LXA_HTML_TEMPLATE_TABLE_END_POP_IGNORE
                            : LXA_HTML_TEMPLATE_TABLE_END_IGNORE;
    if (lxa_html_tree_template_ordinary_end(name)) return LXA_HTML_TEMPLATE_TABLE_END_ORDINARY;
    if (column_group && lxa_html_tree_formatting_tag(name))
        return LXA_HTML_TEMPLATE_TABLE_END_FORMATTING;
    return LXA_HTML_TEMPLATE_TABLE_END_CONTINUE;
}

/* Classify one already-read active entry. Owners retain read charges,
 * start preflight, ordinary colgroup pops and full token reprocessing.
 */
static inline lxa_html_template_current_action_t
lxa_html_tree_template_current_action(lxa_span_t current, lxa_dom_namespace_t ns,
    bool first, lxa_html_token_kind_t kind, lxa_span_t name)
{
    if (ns != LXA_DOM_NS_HTML) return LXA_HTML_TEMPLATE_CURRENT_CONTINUE;
    if (LXA_CONTAINER_NAMED(current, "colgroup")) {
        if (first && kind == LXA_HTML_START_TAG
            && lxa_html_tree_template_colgroup_start_reprocess(name))
            return LXA_HTML_TEMPLATE_CURRENT_REPROCESS_START;
        if (kind == LXA_HTML_END_TAG
            && lxa_html_tree_template_colgroup_end_reprocess(name))
            return LXA_HTML_TEMPLATE_CURRENT_REPROCESS_END;
        return LXA_HTML_TEMPLATE_CURRENT_TABLE;
    }
    if (LXA_CONTAINER_NAMED(current, "table")
        || lxa_html_tree_table_section(current) || LXA_CONTAINER_NAMED(current, "tr"))
        return LXA_HTML_TEMPLATE_CURRENT_TABLE;
    return LXA_HTML_TEMPLATE_CURRENT_CONTINUE;
}

#undef LXA_CONTAINER_NAMED

#endif
