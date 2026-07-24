/* Copyright (C) 2026 Paul Moses */
/* Bounded tree dump for selected upstream html5lib document cases. */
#include <stdio.h>
#include <string.h>

#include "dom/node_store_internal.h"
#include "liblexa/html.h"

typedef struct {
    lxa_dom_ref_t next;
    unsigned depth;
} frame_t;

static unsigned char output[16384];
static size_t used;

static int
append(const void *bytes, size_t length)
{
    if (length > sizeof(output) - used) return -1;
    if (length == 0) return 0;
    if (bytes == NULL) return -1;
    memcpy(output + used, bytes, length);
    used += length;
    return 0;
}

static int
line(unsigned depth, const char *open, lxa_span_t value, const char *close)
{
    unsigned i;
    if (depth > 64 || append("| ", 2) != 0) return -1;
    for (i = 0; i < depth; i++) {
        if (append("  ", 2) != 0) return -1;
    }
    if (append(open, strlen(open)) != 0
        || append(value.data, value.length) != 0
        || append(close, strlen(close)) != 0
        || append("\n", 1) != 0) return -1;
    return 0;
}

static int
attribute_line(unsigned depth, lxa_span_t name, lxa_span_t value)
{
    unsigned i;
    if (depth > 64 || append("| ", 2) != 0) return -1;
    for (i = 0; i < depth; i++) {
        if (append("  ", 2) != 0) return -1;
    }
    return append(name.data, name.length) != 0
           || append("=\"", 2) != 0
           || append(value.data, value.length) != 0
           || append("\"\n", 2) != 0 ? -1 : 0;
}

static int
dump(lxa_dom_nodes_t *nodes, lxa_dom_ref_t root)
{
    frame_t stack[65];
    size_t size = 1, visits = 0;
    if (lxa_dom_nodes_first_child(nodes, root, &stack[0].next) != LXA_OK)
        return -1;
    stack[0].depth = 0;
    while (size != 0) {
        frame_t *frame = &stack[size - 1];
        lxa_dom_ref_t node = frame->next, child = {0};
        lxa_dom_record_t record;
        lxa_span_t value;
        unsigned depth = frame->depth, child_depth = depth + 1;
        if (node.handle == LXA_HANDLE_NONE) {
            size--;
            continue;
        }
        if (++visits > 256
            || lxa_dom_nodes_next_sibling(nodes, node, &frame->next) != LXA_OK
            || lxa_dom_nodes_read(nodes, node, &record) != LXA_OK)
            return -1;
        switch (record.kind) {
        case LXA_DOM_KIND_ELEMENT:
        {
            lxa_dom_namespace_t ns;
            lxa_dom_ref_t attribute = {0}, next;
            const char *open;
            if (lxa_dom_nodes_element_name(nodes, node, &value) != LXA_OK
                || lxa_dom_nodes_element_namespace(nodes, node, &ns) != LXA_OK)
                return -1;
            switch (ns) {
            case LXA_DOM_NS_HTML: open = "<"; break;
            case LXA_DOM_NS_SVG: open = "<svg "; break;
            case LXA_DOM_NS_MATHML: open = "<math "; break;
            default: return -1;
            }
            if (line(depth, open, value, ">") != 0)
                return -1;
            for (;;) {
                lxa_span_t name, text;
                if (lxa_dom_nodes_attribute_next(nodes, node, attribute,
                                                  &next) != LXA_OK)
                    return -1;
                if (next.handle == LXA_HANDLE_NONE) break;
                if (++visits > 256
                    || lxa_dom_nodes_attribute_read(nodes, next, &name, &text)
                       != LXA_OK
                    || attribute_line(child_depth, name, text) != 0)
                    return -1;
                attribute = next;
            }
            if (ns == LXA_DOM_NS_HTML && value.length == 8
                && memcmp(value.data, "template", 8) == 0) {
                lxa_dom_ref_t fragment;
                if (line(child_depth, "content", (lxa_span_t){0}, "") != 0
                    || lxa_dom_nodes_template_contents(nodes, node, &fragment) != LXA_OK
                    || fragment.handle == LXA_HANDLE_NONE
                    || lxa_dom_nodes_first_child(nodes, fragment, &child) != LXA_OK)
                    return -1;
                child_depth++;
            }
            else if (lxa_dom_nodes_first_child(nodes, node, &child) != LXA_OK)
                return -1;
            break;
        }
        case LXA_DOM_KIND_TEXT:
            if (lxa_dom_nodes_character_read(nodes, node, &value) != LXA_OK
                || line(depth, "\"", value, "\"") != 0)
                return -1;
            break;
        case LXA_DOM_KIND_COMMENT:
            if (lxa_dom_nodes_character_read(nodes, node, &value) != LXA_OK
                || line(depth, "<!-- ", value, " -->") != 0)
                return -1;
            break;
        case LXA_DOM_KIND_DOCUMENT_TYPE:
        {
            lxa_dom_doctype_data_t data;
            if (lxa_dom_nodes_doctype_read(nodes, node, &data) != LXA_OK
                || data.has_public_id || data.has_system_id
                || line(depth, "<!DOCTYPE ", data.name, ">") != 0)
                return -1;
            break;
        }
        default:
            return -1;
        }
        if (child.handle != LXA_HANDLE_NONE) {
            if (size == sizeof(stack) / sizeof(stack[0])) return -1;
            stack[size].next = child;
            stack[size++].depth = child_depth;
        }
    }
    return 0;
}

int
main(void)
{
    unsigned char input[4097];
    size_t length = fread(input, 1, sizeof(input), stdin);
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_html_document_t *document = NULL;
    lxa_html_result_t result;
    lxa_limit_reason_t reason;
    lxa_status_t status;
    int failed;
    if (ferror(stdin) || length == sizeof(input)) {
        fputs("input exceeds 4096 bytes\n", stderr);
        return 2;
    }
    status = lxa_html_document_parse(&allocator, NULL,
        (lxa_span_t){input, length}, &result, &reason, &document);
    if (status != LXA_OK || result != LXA_HTML_OK || document == NULL) {
        fprintf(stderr, "parse: status=%d result=%d reason=%d\n",
                (int) status, (int) result, (int) reason);
        return 1;
    }
    failed = lxa_dom_nodes_validate(lxa_html_document_nodes(document)) != LXA_OK
        || dump(lxa_html_document_nodes(document),
                lxa_html_document_root(document)) != 0;
    if (!failed && fwrite(output, 1, used, stdout) != used) failed = 1;
    lxa_html_document_destroy(document);
    if (failed) fputs("bounded tree dump failed\n", stderr);
    return failed;
}
