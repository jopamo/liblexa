#include <string.h>
#include "dom/node_store_internal.h"
#include "liblexa/serialize.h"

typedef struct {
    lxa_write_f write;
    void *context;
    lxa_budget_t *budget;
    lxa_budget_kind_t *reason;
} writer_t;

static lxa_write_result_t
write_budgeted(void *context, lxa_span_t bytes)
{
    writer_t *writer = context;
    lxa_write_result_t result = {0, LXA_OK};
    result.status = lxa_budget_charge(writer->budget, LXA_BUDGET_WORK, 1,
                                       writer->reason);
    return result.status == LXA_OK ? writer->write(writer->context, bytes)
           : result;
}

static lxa_status_t
emit(writer_t *writer, lxa_span_t bytes)
{
    lxa_status_t status = lxa_budget_charge(writer->budget, LXA_BUDGET_WORK,
                                             bytes.length, writer->reason);
    return status == LXA_OK ? lxa_write_all(bytes, write_budgeted, writer)
           : status;
}

static lxa_status_t
literal_bytes(writer_t *writer, const char *text, size_t length)
{
    lxa_span_t bytes = {(const uint8_t *) text, length};
    return emit(writer, bytes);
}

#define literal(writer, text) literal_bytes((writer), (text), sizeof(text) - 1)

static lxa_status_t
escaped(writer_t *writer, lxa_span_t bytes, lxa_escape_context_t mode)
{
    lxa_status_t status = lxa_budget_charge(writer->budget, LXA_BUDGET_WORK,
                                             bytes.length, writer->reason);
    return status == LXA_OK
           ? lxa_html_escape(bytes, mode, write_budgeted, writer) : status;
}

static bool
is_name_length(lxa_span_t name, const char *text, size_t length)
{
    return name.length == length && memcmp(name.data, text, length) == 0;
}

#define is_name(name, text) is_name_length((name), (text), sizeof(text) - 1)
#define VOID_NAME(text) {text, sizeof(text) - 1}

static bool
is_void(lxa_span_t name)
{
    static const struct {
        const char *text;
        size_t length;
    } names[] = {
        VOID_NAME("area"), VOID_NAME("base"), VOID_NAME("basefont"),
        VOID_NAME("bgsound"), VOID_NAME("br"), VOID_NAME("col"),
        VOID_NAME("embed"), VOID_NAME("frame"), VOID_NAME("hr"),
        VOID_NAME("img"), VOID_NAME("input"), VOID_NAME("keygen"),
        VOID_NAME("link"), VOID_NAME("meta"), VOID_NAME("param"),
        VOID_NAME("source"), VOID_NAME("track"), VOID_NAME("wbr")
    };
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (is_name_length(name, names[i].text, names[i].length)) {
            return true;
        }
    }
    return false;
}

static lxa_status_t
element_open(lxa_dom_nodes_t *nodes, lxa_dom_ref_t element, writer_t *writer,
             bool *void_element)
{
    lxa_dom_ref_t attr = {0}, next;
    lxa_span_t name, key, value;
    lxa_dom_namespace_t ns;
    size_t visited = 0;
    lxa_status_t status = lxa_dom_nodes_element_name(nodes, element, &name);
    if (status != LXA_OK) return status;
    status = lxa_dom_nodes_element_namespace(nodes, element, &ns);
    if (status != LXA_OK) return status;
    *void_element = ns == LXA_DOM_NS_HTML && is_void(name);
    status = literal(writer, "<");
    if (status == LXA_OK) status = emit(writer, name);
    while (status == LXA_OK) {
        status = lxa_dom_nodes_attribute_next(nodes, element, attr, &next);
        if (status != LXA_OK || next.handle == LXA_HANDLE_NONE) break;
        if (++visited > lxa_dom_nodes_length(nodes)) return LXA_ERROR_ARGUMENT;
        status = lxa_dom_nodes_attribute_read(nodes, next, &key, &value);
        if (status == LXA_OK) status = literal(writer, " ");
        if (status == LXA_OK) status = emit(writer, key);
        if (status == LXA_OK) status = literal(writer, "=\"");
        if (status == LXA_OK) status = escaped(writer, value, LXA_ESCAPE_ATTRIBUTE);
        if (status == LXA_OK) status = literal(writer, "\"");
        attr = next;
    }
    if (status == LXA_OK) status = literal(writer, ">");
    return status;
}

static lxa_status_t
element_close(lxa_dom_nodes_t *nodes, lxa_dom_ref_t element, writer_t *writer)
{
    lxa_span_t name;
    lxa_status_t status = lxa_dom_nodes_element_name(nodes, element, &name);
    if (status == LXA_OK) status = literal(writer, "</");
    if (status == LXA_OK) status = emit(writer, name);
    if (status == LXA_OK) status = literal(writer, ">");
    return status;
}

static lxa_status_t
doctype_write(lxa_dom_nodes_t *nodes, lxa_dom_ref_t node, writer_t *writer)
{
    lxa_dom_doctype_data_t data;
    lxa_status_t status = lxa_dom_nodes_doctype_read(nodes, node, &data);
    /* HTML serialization omits the stored public and system identifiers. */
    if (status == LXA_OK) status = literal(writer, "<!DOCTYPE ");
    if (status == LXA_OK) status = emit(writer, data.name);
    if (status == LXA_OK) status = literal(writer, ">");
    return status;
}

lxa_status_t
lxa_html_serialize_document(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t document, lxa_write_f write, void *context,
    lxa_budget_kind_t *reason)
{
    writer_t writer;
    lxa_dom_ref_t current, next, template_fragment;
    lxa_dom_record_t record, parent;
    lxa_span_t bytes, name;
    lxa_dom_namespace_t ns;
    size_t visits = 0, maximum;
    bool entering = true, void_element = false;
    lxa_status_t status;
    if (reason != NULL) *reason = LXA_BUDGET_COUNT;
    if (nodes == NULL || write == NULL
        || lxa_dom_nodes_read(nodes, document, &record) != LXA_OK
        || record.kind != LXA_DOM_KIND_DOCUMENT
        || record.parent != LXA_ID_NONE
        || lxa_size_mul(lxa_dom_nodes_length(nodes), 3, &maximum) != LXA_OK) {
        return LXA_ERROR_ARGUMENT;
    }
    writer = (writer_t){write, context, nodes->budget, reason};
    current = document;
    for (;;) {
        if (++visits > maximum
            || lxa_dom_nodes_read(nodes, current, &record) != LXA_OK) {
            return LXA_ERROR_ARGUMENT;
        }
        if ((record.first_child == LXA_ID_NONE)
            != (record.last_child == LXA_ID_NONE)) {
            return LXA_ERROR_ARGUMENT;
        }
        if (record.first_child != LXA_ID_NONE
            && record.kind != LXA_DOM_KIND_DOCUMENT
            && record.kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT
            && record.kind != LXA_DOM_KIND_ELEMENT)
            return LXA_ERROR_ARGUMENT;
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1, reason);
        if (status != LXA_OK) return status;
        void_element = false;
        template_fragment = (lxa_dom_ref_t){0};
        if (record.kind == LXA_DOM_KIND_ELEMENT) {
            status = lxa_dom_nodes_element_name(nodes, current, &name);
            if (status != LXA_OK) return status;
            status = lxa_dom_nodes_element_namespace(nodes, current, &ns);
            if (status != LXA_OK) return status;
            void_element = ns == LXA_DOM_NS_HTML && is_void(name);
            if (void_element && record.first_child != LXA_ID_NONE)
                return LXA_ERROR_ARGUMENT;
            if (entering && ns == LXA_DOM_NS_HTML && is_name(name, "template")) {
                if (record.first_child != LXA_ID_NONE) return LXA_ERROR_ARGUMENT;
                status = lxa_dom_nodes_template_contents(nodes, current,
                                                           &template_fragment);
                if (status != LXA_OK) return status;
                if (template_fragment.handle == LXA_HANDLE_NONE)
                    return LXA_ERROR_ARGUMENT;
            }
        }
        if (entering) {
            switch (record.kind) {
                case LXA_DOM_KIND_DOCUMENT: status = LXA_OK; break;
                case LXA_DOM_KIND_DOCUMENT_FRAGMENT: status = LXA_OK; break;
                case LXA_DOM_KIND_ELEMENT:
                    status = element_open(nodes, current, &writer, &void_element);
                    break;
                case LXA_DOM_KIND_DOCUMENT_TYPE:
                    status = doctype_write(nodes, current, &writer);
                    break;
                case LXA_DOM_KIND_COMMENT:
                    status = lxa_dom_nodes_character_read(nodes, current, &bytes);
                    if (status == LXA_OK) status = literal(&writer, "<!--");
                    if (status == LXA_OK) status = emit(&writer, bytes);
                    if (status == LXA_OK) status = literal(&writer, "-->");
                    break;
                case LXA_DOM_KIND_PROCESSING_INSTRUCTION:
                    status = lxa_dom_nodes_pi_read(nodes, current, &name, &bytes);
                    if (status == LXA_OK) status = literal(&writer, "<?");
                    if (status == LXA_OK) status = emit(&writer, name);
                    if (status == LXA_OK) status = literal(&writer, " ");
                    if (status == LXA_OK) status = emit(&writer, bytes);
                    if (status == LXA_OK) status = literal(&writer, "?>");
                    break;
                case LXA_DOM_KIND_TEXT:
                    status = lxa_dom_nodes_character_read(nodes, current, &bytes);
                    if (status == LXA_OK && record.parent != LXA_ID_NONE) {
                        lxa_id_t parent_id = record.parent;
                        if (parent_id < nodes->retired
                            || parent_id >= lxa_dom_nodes_length(nodes))
                            return LXA_ERROR_ARGUMENT;
                        parent = ((const lxa_dom_record_t *)
                                  nodes->records.bytes.data)[parent_id];
                        if (parent.kind == LXA_DOM_KIND_ELEMENT) {
                            lxa_dom_ref_t owner = current;
                            status = lxa_handle_pack(parent_id, nodes->generation,
                                                      &owner.handle);
                            if (status == LXA_OK)
                                status = lxa_dom_nodes_element_name(nodes, owner,
                                                                    &name);
                            if (status != LXA_OK) return status;
                            status = lxa_dom_nodes_element_namespace(nodes,
                                                                      owner, &ns);
                            if (status != LXA_OK) return status;
                            if (ns == LXA_DOM_NS_HTML
                                && (is_name(name, "script") || is_name(name, "style")
                                || is_name(name, "xmp") || is_name(name, "iframe")
                                || is_name(name, "noembed") || is_name(name, "noframes")
                                || is_name(name, "plaintext")))
                                status = emit(&writer, bytes);
                            else
                                status = escaped(&writer, bytes, LXA_ESCAPE_TEXT);
                            break;
                        }
                    }
                    if (status == LXA_OK)
                        status = escaped(&writer, bytes, LXA_ESCAPE_TEXT);
                    break;
                default: return LXA_ERROR_ARGUMENT;
            }
            if (status != LXA_OK) return status;
            if (template_fragment.handle != LXA_HANDLE_NONE) {
                status = lxa_dom_nodes_first_child(nodes, template_fragment,
                                                    &next);
                if (status != LXA_OK) return status;
                if (next.handle != LXA_HANDLE_NONE) {
                    current = template_fragment;
                    continue;
                }
            }
            if (!void_element && record.first_child != LXA_ID_NONE) {
                status = lxa_dom_nodes_first_child(nodes, current, &next);
                if (status != LXA_OK) return status;
                if (next.handle == LXA_HANDLE_NONE) return LXA_ERROR_ARGUMENT;
                current = next;
                continue;
            }
        }
        if (record.kind == LXA_DOM_KIND_ELEMENT && !void_element) {
            status = element_close(nodes, current, &writer);
            if (status != LXA_OK) return status;
        }
        if (current.handle == document.handle) return LXA_OK;
        if (record.kind == LXA_DOM_KIND_DOCUMENT_FRAGMENT) {
            status = lxa_dom_nodes_template_owner(nodes, current, &next);
            if (status != LXA_OK) return status;
            if (next.handle == LXA_HANDLE_NONE) return LXA_ERROR_ARGUMENT;
            current = next;
            entering = false;
            continue;
        }
        status = lxa_dom_nodes_next_sibling(nodes, current, &next);
        if (status != LXA_OK) return status;
        if (next.handle != LXA_HANDLE_NONE) {
            current = next;
            entering = true;
            continue;
        }
        if (record.parent < nodes->retired
            || record.parent >= lxa_dom_nodes_length(nodes))
            return LXA_ERROR_ARGUMENT;
        parent = ((const lxa_dom_record_t *) nodes->records.bytes.data)[record.parent];
        if (parent.last_child != (lxa_id_t) current.handle)
            return LXA_ERROR_ARGUMENT;
        status = lxa_handle_pack(record.parent, nodes->generation, &current.handle);
        if (status != LXA_OK) return status;
        entering = false;
    }
}
