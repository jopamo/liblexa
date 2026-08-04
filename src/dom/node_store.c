#include <stdbool.h>
#include <string.h>
#include "liblexa/dom.h"
#include "dom/node_store_internal.h"

static bool
valid_kind(lxa_dom_kind_t kind)
{
    return kind > LXA_DOM_KIND_INVALID && kind < LXA_DOM_KIND_COUNT;
}

typedef struct {
    size_t offset, length;
} lxa_dom_character_t;

typedef struct {
    lxa_dom_character_t target, data;
} lxa_dom_pi_t;

typedef struct {
    lxa_id_t atom;
    uint32_t namespace_id;
    lxa_id_t first_attr;
    uint32_t attr_count;
} lxa_dom_element_t;

typedef struct {
    size_t offset, length;
    lxa_id_t id;
} lxa_dom_atom_t;

typedef struct {
    size_t name_offset, name_length;
    size_t value_offset, value_length;
    lxa_id_t owner, next;
    lxa_dom_attr_namespace_t namespace_id;
} lxa_dom_attr_t;

typedef struct {
    size_t name_offset, name_length;
    size_t public_offset, public_length;
    size_t system_offset, system_length;
    bool has_public, has_system, force_quirks;
} lxa_dom_doctype_t;

/* The tokenizer folds ASCII uppercase before handing names to the tree.
 * Keep raw markup delimiters out of the owned store: the HTML serializer
 * writes names verbatim. Bytes >= 0x80 come from the decoder/tokenizer.
 */
static bool
html_name_byte(uint8_t c)
{
    return c != 0 && c != ' ' && c != '\n' && c != '\r'
           && c != '\t' && c != '\f' && c != '/'
           && c != '<' && c != '>' && c != '='
           && c != '"' && c != '\''
           && (c < 'A' || c > 'Z');
}

lxa_status_t
lxa_dom_nodes_create(const lxa_allocator_t *allocator, lxa_dom_nodes_t **out)
{
    return lxa_dom_nodes_create_with_budget(allocator, NULL, NULL, out);
}

lxa_status_t
lxa_dom_nodes_create_with_budget(const lxa_allocator_t *allocator,
                                 lxa_budget_t *shared,
                                 lxa_budget_kind_t *reason,
                                 lxa_dom_nodes_t **out)
{
    return lxa_dom_nodes_create_with_entropy(
        allocator, shared, NULL, NULL, reason, out);
}

lxa_status_t
lxa_dom_nodes_create_with_entropy(const lxa_allocator_t *allocator,
                                   lxa_budget_t *shared,
                                   lxa_entropy_callback_t entropy,
                                   void *entropy_context,
                                   lxa_budget_kind_t *reason,
                                   lxa_dom_nodes_t **out)
{
    lxa_dom_nodes_t *nodes;
    lxa_budget_t local_budget = {0};
    lxa_budget_t *budget = shared;
    lxa_status_t status;
    if (reason != NULL) {
        *reason = LXA_BUDGET_COUNT;
    }
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = NULL;
    if (allocator == NULL || allocator->allocate == NULL
        || allocator->resize == NULL || allocator->release == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    if (shared == NULL) {
        lxa_budget_init(&local_budget, NULL);
        budget = &local_budget;
    }
    status = lxa_budget_charge(budget, LXA_BUDGET_MEMORY, sizeof(*nodes), reason);
    if (status != LXA_OK) {
        return status;
    }
    status = lxa_calloc(allocator, 1, sizeof(*nodes), (void **) &nodes);
    if (status != LXA_OK) {
        lxa_budget_release(budget, LXA_BUDGET_MEMORY, sizeof(*nodes));
        return status;
    }
    nodes->allocator = *allocator;
    status = lxa_vector_init(&nodes->records, allocator, sizeof(lxa_dom_record_t));
    if (status != LXA_OK) {
        lxa_free(allocator, nodes);
        lxa_budget_release(budget, LXA_BUDGET_MEMORY, sizeof(*nodes));
        return status;
    }
    status = lxa_vector_init(&nodes->characters, allocator,
                             sizeof(lxa_dom_character_t));
    if (status == LXA_OK) {
        status = lxa_vector_init(&nodes->pis, allocator, sizeof(lxa_dom_pi_t));
    }
    if (status == LXA_OK) {
        status = lxa_vector_init(&nodes->elements, allocator,
                                 sizeof(lxa_dom_element_t));
    }
    if (status == LXA_OK) {
        status = lxa_vector_init(&nodes->templates, allocator,
                                 sizeof(lxa_dom_template_t));
    }
    if (status == LXA_OK) {
        status = lxa_vector_init(&nodes->atoms, allocator,
                                 sizeof(lxa_dom_atom_t *));
    }
    if (status == LXA_OK) {
        status = lxa_vector_init(&nodes->attributes, allocator,
                                 sizeof(lxa_dom_attr_t));
    }
    if (status == LXA_OK) {
        status = lxa_vector_init(&nodes->doctypes, allocator,
                                 sizeof(lxa_dom_doctype_t));
    }
    if (status == LXA_OK) {
        status = lxa_buffer_init(&nodes->strings, allocator);
    }
    if (status != LXA_OK) {
        lxa_vector_destroy(&nodes->attributes);
        lxa_vector_destroy(&nodes->doctypes);
        lxa_vector_destroy(&nodes->atoms);
        lxa_vector_destroy(&nodes->templates);
        lxa_vector_destroy(&nodes->elements);
        lxa_vector_destroy(&nodes->pis);
        lxa_vector_destroy(&nodes->characters);
        lxa_vector_destroy(&nodes->records);
        lxa_free(allocator, nodes);
        lxa_budget_release(budget, LXA_BUDGET_MEMORY, sizeof(*nodes));
        return status;
    }
    nodes->epoch = 1;
    nodes->generation = 1;
    nodes->own_budget = local_budget;
    nodes->budget = shared == NULL ? &nodes->own_budget : shared;
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_vector_bind_budget(&nodes->records, nodes->budget);
    if (status == LXA_OK) {
        status = lxa_vector_bind_budget(&nodes->characters, nodes->budget);
    }
    if (status == LXA_OK) {
        status = lxa_vector_bind_budget(&nodes->pis, nodes->budget);
    }
    if (status == LXA_OK) {
        status = lxa_vector_bind_budget(&nodes->elements, nodes->budget);
    }
    if (status == LXA_OK) {
        status = lxa_vector_bind_budget(&nodes->templates, nodes->budget);
    }
    if (status == LXA_OK) {
        status = lxa_vector_bind_budget(&nodes->atoms, nodes->budget);
    }
    if (status == LXA_OK) {
        status = lxa_vector_bind_budget(&nodes->attributes, nodes->budget);
    }
    if (status == LXA_OK) {
        status = lxa_vector_bind_budget(&nodes->doctypes, nodes->budget);
    }
    if (status == LXA_OK) {
        status = lxa_buffer_bind_budget(&nodes->strings, nodes->budget);
    }
    if (status == LXA_OK) {
        status = lxa_table_create_with_budget(allocator, nodes->budget,
                        entropy, entropy_context, &nodes->atom_lookup);
        if (status == LXA_ERROR_LIMIT && reason != NULL) {
            *reason = LXA_BUDGET_MEMORY;
        }
    }
    if (status != LXA_OK) {
        lxa_dom_nodes_destroy(nodes);
        return status;
    }
    *out = nodes;
    return LXA_OK;
}

static void
release_atoms(lxa_dom_nodes_t *nodes)
{
    size_t i;
    lxa_dom_atom_t *const *atoms =
        (lxa_dom_atom_t *const *) nodes->atoms.bytes.data;
    for (i = 0; i < lxa_vector_length(&nodes->atoms); ++i) {
        lxa_free(&nodes->allocator, atoms[i]);
        lxa_budget_release(nodes->budget, LXA_BUDGET_MEMORY,
                           sizeof(lxa_dom_atom_t));
    }
    lxa_vector_clear(&nodes->atoms);
}

void
lxa_dom_nodes_destroy(lxa_dom_nodes_t *nodes)
{
    lxa_allocator_t allocator;
    lxa_budget_t *shared;
    if (nodes == NULL) {
        return;
    }
    allocator = nodes->allocator;
    shared = nodes->budget == &nodes->own_budget ? NULL : nodes->budget;
    lxa_table_destroy(nodes->atom_lookup);
    release_atoms(nodes);
    lxa_vector_destroy(&nodes->atoms);
    lxa_vector_destroy(&nodes->attributes);
    lxa_vector_destroy(&nodes->doctypes);
    lxa_buffer_destroy(&nodes->strings);
    lxa_vector_destroy(&nodes->templates);
    lxa_vector_destroy(&nodes->elements);
    lxa_vector_destroy(&nodes->characters);
    lxa_vector_destroy(&nodes->pis);
    lxa_vector_destroy(&nodes->records);
    lxa_free(&allocator, nodes);
    if (shared != NULL) {
        lxa_budget_release(shared, LXA_BUDGET_MEMORY, sizeof(*nodes));
    }
}

lxa_budget_kind_t
lxa_dom_nodes_limit_reason(const lxa_dom_nodes_t *nodes)
{
    return nodes == NULL ? LXA_BUDGET_COUNT : nodes->limit_reason;
}

static lxa_status_t
check_new_slot(lxa_dom_nodes_t *nodes, lxa_budget_kind_t *reason)
{
    *reason = LXA_BUDGET_COUNT;
    if (lxa_vector_length(&nodes->records) >= nodes->charged_slots) {
        return lxa_budget_check(nodes->budget, LXA_BUDGET_NODES, 1, reason);
    }
    return LXA_OK;
}

static void
charge_new_slot(lxa_dom_nodes_t *nodes)
{
    size_t length = lxa_vector_length(&nodes->records);
    if (length > nodes->charged_slots) {
        lxa_budget_charge(nodes->budget, LXA_BUDGET_NODES, 1, NULL);
        nodes->charged_slots = length;
    }
}

lxa_status_t
lxa_dom_nodes_reset(lxa_dom_nodes_t *nodes)
{
    size_t clear_work, total_work, atom_count;
    lxa_status_t status;
    if (nodes == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    nodes->limit_reason = LXA_BUDGET_COUNT;
    if (nodes->epoch == UINT64_MAX) {
        return LXA_ERROR_OVERFLOW;
    }
    atom_count = lxa_vector_length(&nodes->atoms);
    status = lxa_table_clear_cost(nodes->atom_lookup, &clear_work);
    if (status != LXA_OK) {
        return status;
    }
    status = lxa_size_add(clear_work, atom_count, &total_work);
    if (status != LXA_OK) {
        return status;
    }
    status = lxa_budget_check(nodes->budget, LXA_BUDGET_WORK, total_work,
                              &nodes->limit_reason);
    if (status != LXA_OK) {
        return status;
    }
    /* No callbacks or concurrent budget changes may intervene between
     * preflight and the two charges. Charge releases before clearing keys.
     */
    status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, atom_count,
                                &nodes->limit_reason);
    if (status != LXA_OK) {
        return status;
    }
    status = lxa_table_clear_checked(nodes->atom_lookup);
    if (status != LXA_OK) {
        nodes->limit_reason = lxa_table_limit_reason(nodes->atom_lookup);
        return status;
    }
    release_atoms(nodes);
    ++nodes->epoch;
    if (nodes->generation == UINT32_MAX) {
        /* Existing slots cannot be reused with generation 1. Keep their
         * indexes reserved, even across later resets.
         */
        nodes->retired = lxa_vector_length(&nodes->records);
        nodes->generation = 1;
    }
    else {
        ++nodes->generation;
        lxa_vector_erase(&nodes->records, nodes->retired,
                         lxa_vector_length(&nodes->records) - nodes->retired);
    }
    lxa_buffer_clear(&nodes->strings);
    lxa_vector_clear(&nodes->elements);
    lxa_vector_clear(&nodes->templates);
    lxa_vector_clear(&nodes->attributes);
    lxa_vector_clear(&nodes->doctypes);
    lxa_vector_clear(&nodes->characters);
    lxa_vector_clear(&nodes->pis);
    return LXA_OK;
}

size_t
lxa_dom_nodes_length(const lxa_dom_nodes_t *nodes)
{
    return nodes == NULL ? 0
           : lxa_vector_length(&nodes->records) - nodes->retired;
}

lxa_status_t
lxa_dom_nodes_append_draft(lxa_dom_nodes_t *nodes, lxa_dom_kind_t kind,
                           lxa_dom_ref_t *out)
{
    lxa_dom_record_t record;
    lxa_id_t id;
    lxa_handle_t packed;
    size_t next;
    lxa_status_t status;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (nodes == NULL || !valid_kind(kind)) {
        return LXA_ERROR_ARGUMENT;
    }
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_size_to_id(lxa_vector_length(&nodes->records), &id);
    if (status != LXA_OK) {
        return status;
    }
    status = check_new_slot(nodes, &nodes->limit_reason);
    if (status != LXA_OK) {
        return status;
    }
    status = lxa_handle_pack(id, nodes->generation, &packed);
    if (status != LXA_OK) {
        return status;
    }
    status = lxa_size_add(lxa_vector_length(&nodes->records), 1, &next);
    if (status == LXA_OK) {
        status = lxa_vector_reserve(&nodes->records, next);
    }
    if (status != LXA_OK) {
        if (status == LXA_ERROR_LIMIT) {
            nodes->limit_reason = LXA_BUDGET_MEMORY;
        }
        return status;
    }
    status = check_new_slot(nodes, &nodes->limit_reason);
    if (status != LXA_OK) {
        return status;
    }
    memset(&record, 0, sizeof(record));
    record.parent = record.first_child = record.last_child = LXA_ID_NONE;
    record.prev = record.next = record.payload = LXA_ID_NONE;
    record.kind = (uint16_t) kind;
    record.generation = nodes->generation;
    status = lxa_vector_push(&nodes->records, &record);
    if (status == LXA_ERROR_LIMIT) {
        nodes->limit_reason = LXA_BUDGET_MEMORY;
    }
    if (status == LXA_OK) {
        charge_new_slot(nodes);
        out->document = nodes;
        out->epoch = nodes->epoch;
        out->handle = packed;
    }
    return status;
}

lxa_status_t
lxa_dom_nodes_read(const lxa_dom_nodes_t *nodes, lxa_dom_ref_t handle,
                   lxa_dom_record_t *out)
{
    const lxa_dom_record_t *entry;
    lxa_id_t index;
    uint32_t generation;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (nodes == NULL || handle.document != nodes || handle.epoch != nodes->epoch
        || lxa_handle_unpack(handle.handle, &index, &generation) != LXA_OK) {
        return LXA_ERROR_ARGUMENT;
    }
    if (index < nodes->retired || index >= lxa_vector_length(&nodes->records)) {
        return LXA_ERROR_ARGUMENT;
    }
    entry = (const lxa_dom_record_t *) nodes->records.bytes.data + index;
    if (entry->generation != generation) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = *entry;
    return LXA_OK;
}

lxa_status_t
lxa_dom_nodes_kind(const lxa_dom_nodes_t *nodes, lxa_dom_ref_t node,
                   lxa_dom_kind_t *out)
{
    lxa_dom_record_t record;
    lxa_status_t status;
    if (out == NULL) return LXA_ERROR_ARGUMENT;
    *out = LXA_DOM_KIND_INVALID;
    status = lxa_dom_nodes_read(nodes, node, &record);
    if (status != LXA_OK) return status;
    if (!valid_kind((lxa_dom_kind_t) record.kind)) return LXA_ERROR_ARGUMENT;
    *out = (lxa_dom_kind_t) record.kind;
    return LXA_OK;
}

typedef enum {
    LXA_REL_FIRST, LXA_REL_NEXT, LXA_REL_LAST, LXA_REL_PREV, LXA_REL_PARENT
} lxa_relation_t;

static lxa_status_t
read_relation(lxa_dom_nodes_t *nodes, lxa_dom_ref_t source,
              lxa_dom_ref_t *out, lxa_relation_t relation)
{
    lxa_dom_record_t record;
    const lxa_dom_record_t *records;
    lxa_id_t id, source_id;
    lxa_status_t status;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    status = lxa_dom_nodes_read(nodes, source, &record);
    if (status != LXA_OK) {
        return status;
    }
    nodes->limit_reason = LXA_BUDGET_COUNT;
    switch (relation) {
        case LXA_REL_FIRST: id = record.first_child; break;
        case LXA_REL_NEXT: id = record.next; break;
        case LXA_REL_LAST: id = record.last_child; break;
        case LXA_REL_PREV: id = record.prev; break;
        case LXA_REL_PARENT: id = record.parent; break;
        default: return LXA_ERROR_ARGUMENT;
    }
    if ((relation == LXA_REL_FIRST || relation == LXA_REL_LAST)
        && record.kind != LXA_DOM_KIND_DOCUMENT
        && record.kind != LXA_DOM_KIND_ELEMENT
        && record.kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT) {
        return LXA_ERROR_ARGUMENT;
    }
    if ((relation == LXA_REL_FIRST || relation == LXA_REL_LAST)
        && (record.first_child == LXA_ID_NONE)
           != (record.last_child == LXA_ID_NONE)) {
        return LXA_ERROR_ARGUMENT;
    }
    if (id == LXA_ID_NONE) {
        return LXA_OK;
    }
    status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                               &nodes->limit_reason);
    if (status != LXA_OK) {
        return status;
    }
    if (id < nodes->retired || id >= lxa_vector_length(&nodes->records)) {
        return LXA_ERROR_ARGUMENT;
    }
    source_id = (lxa_id_t) source.handle;
    records = (const lxa_dom_record_t *) nodes->records.bytes.data;
    if (records[id].generation != nodes->generation) {
        return LXA_ERROR_ARGUMENT;
    }
    if ((relation == LXA_REL_FIRST
         && (records[id].parent != source_id
             || records[id].prev != LXA_ID_NONE))
        || (relation == LXA_REL_LAST
            && (records[id].parent != source_id
                || records[id].next != LXA_ID_NONE))
        || (relation == LXA_REL_NEXT
            && (record.parent == LXA_ID_NONE
                || records[id].parent != record.parent
                || records[id].prev != source_id))
        || (relation == LXA_REL_PREV
            && (record.parent == LXA_ID_NONE
                || records[id].parent != record.parent
                || records[id].next != source_id))
        || (relation == LXA_REL_PARENT
            && (record.prev == LXA_ID_NONE
                ? records[id].first_child != source_id
                : (record.prev < nodes->retired
                   || record.prev >= lxa_vector_length(&nodes->records)
                   || records[record.prev].next != source_id
                   || records[record.prev].parent != id)))) {
        return LXA_ERROR_ARGUMENT;
    }
    out->document = nodes;
    out->epoch = nodes->epoch;
    return lxa_handle_pack(id, nodes->generation, &out->handle);
}

lxa_status_t
lxa_dom_nodes_first_child(lxa_dom_nodes_t *nodes, lxa_dom_ref_t parent,
                           lxa_dom_ref_t *out)
{
    return read_relation(nodes, parent, out, LXA_REL_FIRST);
}

lxa_status_t
lxa_dom_nodes_next_sibling(lxa_dom_nodes_t *nodes, lxa_dom_ref_t sibling,
                            lxa_dom_ref_t *out)
{
    return read_relation(nodes, sibling, out, LXA_REL_NEXT);
}

lxa_status_t
lxa_dom_nodes_last_child(lxa_dom_nodes_t *nodes, lxa_dom_ref_t parent,
                          lxa_dom_ref_t *out)
{
    return read_relation(nodes, parent, out, LXA_REL_LAST);
}

lxa_status_t
lxa_dom_nodes_prev_sibling(lxa_dom_nodes_t *nodes, lxa_dom_ref_t sibling,
                            lxa_dom_ref_t *out)
{
    return read_relation(nodes, sibling, out, LXA_REL_PREV);
}

lxa_status_t
lxa_dom_nodes_parent(lxa_dom_nodes_t *nodes, lxa_dom_ref_t child,
                      lxa_dom_ref_t *out)
{
    return read_relation(nodes, child, out, LXA_REL_PARENT);
}

lxa_status_t
lxa_dom_nodes_append_character(lxa_dom_nodes_t *nodes, lxa_dom_kind_t kind,
                               lxa_span_t text, lxa_dom_ref_t *out)
{
    size_t total, next;
    lxa_id_t payload_id, node_id;
    lxa_handle_t packed;
    lxa_dom_character_t character;
    lxa_dom_record_t record;
    lxa_status_t status;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (nodes == NULL || (kind != LXA_DOM_KIND_TEXT
                          && kind != LXA_DOM_KIND_COMMENT)
        || (text.data == NULL && text.length != 0)) {
        return LXA_ERROR_ARGUMENT;
    }
    nodes->limit_reason = LXA_BUDGET_COUNT;
    /* Narrow both indexes and count before reserving any resources. */
    status = lxa_size_to_id(lxa_vector_length(&nodes->records), &node_id);
    if (status == LXA_OK) {
        status = lxa_handle_pack(node_id, nodes->generation, &packed);
    }
    if (status == LXA_OK) {
        status = lxa_size_to_id(lxa_vector_length(&nodes->characters), &payload_id);
    }
    if (status == LXA_OK) {
        status = check_new_slot(nodes, &nodes->limit_reason);
    }
    if (status == LXA_OK) {
        status = lxa_size_add(nodes->strings.length, text.length, &total);
    }
    if (status == LXA_OK && total > nodes->charged_string_bytes) {
        status = lxa_budget_check(nodes->budget, LXA_BUDGET_STRINGS,
                                  total - nodes->charged_string_bytes,
                                  &nodes->limit_reason);
    }
    if (status == LXA_OK) {
        status = lxa_size_add(lxa_vector_length(&nodes->records), 1, &next);
    }
    if (status == LXA_OK) {
        status = lxa_vector_reserve(&nodes->records, next);
    }
    if (status == LXA_OK) {
        status = lxa_size_add(lxa_vector_length(&nodes->characters), 1, &next);
    }
    if (status == LXA_OK) {
        status = lxa_vector_reserve(&nodes->characters, next);
    }
    if (status == LXA_OK) {
        status = lxa_buffer_reserve(&nodes->strings, total);
    }
    if (status != LXA_OK) {
        if (status == LXA_ERROR_LIMIT) {
            if (nodes->limit_reason == LXA_BUDGET_COUNT) {
                nodes->limit_reason = LXA_BUDGET_MEMORY;
            }
        }
        return status;
    }
    /* Allocator callbacks in the preceding reserves may have charged a
     * shared budget. Recheck before the allocation-free commit below.
     */
    status = check_new_slot(nodes, &nodes->limit_reason);
    if (status == LXA_OK && total > nodes->charged_string_bytes) {
        status = lxa_budget_check(nodes->budget, LXA_BUDGET_STRINGS,
                                  total - nodes->charged_string_bytes,
                                  &nodes->limit_reason);
    }
    if (status != LXA_OK) {
        return status;
    }
    character.offset = nodes->strings.length;
    character.length = text.length;
    status = lxa_buffer_append(&nodes->strings, text);
    if (status != LXA_OK) {
        if (status == LXA_ERROR_LIMIT) {
            nodes->limit_reason = LXA_BUDGET_MEMORY;
        }
        return status;
    }
    status = lxa_vector_push(&nodes->characters, &character);
    if (status != LXA_OK) {
        lxa_buffer_truncate(&nodes->strings, character.offset);
        if (status == LXA_ERROR_LIMIT) {
            nodes->limit_reason = LXA_BUDGET_MEMORY;
        }
        return status;
    }
    memset(&record, 0, sizeof(record));
    record.parent = record.first_child = record.last_child = LXA_ID_NONE;
    record.prev = record.next = LXA_ID_NONE;
    record.payload = payload_id;
    record.kind = (uint16_t) kind;
    record.generation = nodes->generation;
    status = lxa_vector_push(&nodes->records, &record);
    if (status != LXA_OK) {
        lxa_vector_erase(&nodes->characters, payload_id, 1);
        lxa_buffer_truncate(&nodes->strings, character.offset);
        if (status == LXA_ERROR_LIMIT) {
            nodes->limit_reason = LXA_BUDGET_MEMORY;
        }
        return status;
    }
    if (total > nodes->charged_string_bytes) {
        lxa_budget_charge(nodes->budget, LXA_BUDGET_STRINGS,
                          total - nodes->charged_string_bytes, NULL);
        nodes->charged_string_bytes = total;
    }
    charge_new_slot(nodes);
    out->document = nodes;
    out->epoch = nodes->epoch;
    out->handle = packed;
    return LXA_OK;
}

lxa_status_t
lxa_dom_nodes_character_read(const lxa_dom_nodes_t *nodes,
                             lxa_dom_ref_t handle, lxa_span_t *out)
{
    lxa_dom_record_t node;
    const lxa_dom_character_t *payload;
    lxa_status_t status;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    out->data = NULL;
    out->length = 0;
    status = lxa_dom_nodes_read(nodes, handle, &node);
    if (status != LXA_OK) {
        return status;
    }
    if ((node.kind != LXA_DOM_KIND_TEXT && node.kind != LXA_DOM_KIND_COMMENT)
        || node.payload >= lxa_vector_length(&nodes->characters)) {
        return LXA_ERROR_ARGUMENT;
    }
    payload = (const lxa_dom_character_t *) nodes->characters.bytes.data
              + node.payload;
    if (payload->offset > nodes->strings.length
        || payload->length > nodes->strings.length - payload->offset) {
        return LXA_ERROR_ARGUMENT;
    }
    out->data = nodes->strings.data == NULL ? NULL
                : nodes->strings.data + payload->offset;
    out->length = payload->length;
    return LXA_OK;
}

static lxa_status_t
string_source(const lxa_dom_nodes_t *nodes, lxa_span_t span,
              bool *self, size_t *offset)
{
    uintptr_t begin, address;
    *self = false;
    *offset = 0;
    if (nodes->strings.data == NULL || span.data == NULL) {
        return LXA_OK;
    }
    begin = (uintptr_t) nodes->strings.data;
    address = (uintptr_t) span.data;
    if (address >= begin && address - begin < nodes->strings.capacity) {
        *offset = (size_t) (address - begin);
        if (*offset > nodes->strings.length
            || span.length > nodes->strings.length - *offset) {
            return LXA_ERROR_ARGUMENT;
        }
        *self = true;
    }
    return LXA_OK;
}

lxa_status_t
lxa_dom_nodes_append_namespaced_element(lxa_dom_nodes_t *nodes,
    lxa_span_t name, lxa_dom_namespace_t ns, lxa_dom_ref_t *out)
{
    lxa_dom_element_t element;
    lxa_dom_record_t record;
    lxa_dom_atom_t *atom = NULL;
    void *atom_value = NULL;
    lxa_handle_t packed;
    lxa_id_t node_id, payload_id, atom_id;
    size_t total, next, i, self_offset = 0;
    bool self = false, found = false, appended_atom = false, inserted = false;
    lxa_status_t status;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (nodes == NULL || ns < LXA_DOM_NS_HTML || ns > LXA_DOM_NS_SVG
        || name.data == NULL || name.length == 0) {
        return LXA_ERROR_ARGUMENT;
    }
    if (name.data[0] < 'a' || name.data[0] > 'z')
        return LXA_ERROR_UNSUPPORTED;
    status = string_source(nodes, name, &self, &self_offset);
    if (status != LXA_OK) {
        return status;
    }
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_budget_check_item(nodes->budget, LXA_BUDGET_NAME_BYTES,
                                    name.length, &nodes->limit_reason);
    if (status != LXA_OK) {
        return status;
    }
    status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                               &nodes->limit_reason);
    if (status != LXA_OK) {
        return status;
    }
    /* HTML names stay tokenizer-lowercase; foreign names may carry the
     * canonical mixed-case spelling supplied by tree construction. */
    for (i = 1; i < name.length; ++i) {
        uint8_t c = name.data[i];
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                   &nodes->limit_reason);
        if (status != LXA_OK) {
            return status;
        }
        if (!html_name_byte(c)
            && !(ns != LXA_DOM_NS_HTML && c >= 'A' && c <= 'Z')) {
            return LXA_ERROR_UNSUPPORTED;
        }
    }
    status = lxa_table_find(nodes->atom_lookup, name, &found, &atom_value);
    atom = atom_value;
    if (status != LXA_OK) {
        if (status == LXA_ERROR_LIMIT) {
            nodes->limit_reason = lxa_table_limit_reason(nodes->atom_lookup);
        }
        return status;
    }
    if (found && (atom == NULL || atom->id >= lxa_vector_length(&nodes->atoms))) {
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_size_to_id(lxa_vector_length(&nodes->records), &node_id);
    if (status == LXA_OK) {
        status = lxa_handle_pack(node_id, nodes->generation, &packed);
    }
    if (status == LXA_OK) {
        status = lxa_size_to_id(lxa_vector_length(&nodes->elements), &payload_id);
    }
    if (status == LXA_OK) {
        status = check_new_slot(nodes, &nodes->limit_reason);
    }
    if (status == LXA_OK) {
        total = nodes->strings.length;
        if (!found) {
            status = lxa_size_add(total, name.length, &total);
        }
    }
    if (status == LXA_OK && !found) {
        status = lxa_size_to_id(lxa_vector_length(&nodes->atoms), &atom_id);
    }
    if (status == LXA_OK && total > nodes->charged_string_bytes) {
        status = lxa_budget_check(nodes->budget, LXA_BUDGET_STRINGS,
            total - nodes->charged_string_bytes, &nodes->limit_reason);
    }
    if (status == LXA_OK) {
        status = lxa_size_add(lxa_vector_length(&nodes->records), 1, &next);
    }
    if (status == LXA_OK) {
        status = lxa_vector_reserve(&nodes->records, next);
    }
    if (status == LXA_OK) {
        status = lxa_size_add(lxa_vector_length(&nodes->elements), 1, &next);
    }
    if (status == LXA_OK) {
        status = lxa_vector_reserve(&nodes->elements, next);
    }
    if (status == LXA_OK && !found) {
        status = lxa_size_add(lxa_vector_length(&nodes->atoms), 1, &next);
        if (status == LXA_OK) {
            status = lxa_vector_reserve(&nodes->atoms, next);
        }
    }
    if (status == LXA_OK) {
        status = lxa_buffer_reserve(&nodes->strings, total);
    }
    if (status != LXA_OK) {
        if (status == LXA_ERROR_LIMIT
            && nodes->limit_reason == LXA_BUDGET_COUNT) {
            nodes->limit_reason = LXA_BUDGET_MEMORY;
        }
        return status;
    }
    status = check_new_slot(nodes, &nodes->limit_reason);
    if (status == LXA_OK && total > nodes->charged_string_bytes) {
        status = lxa_budget_check(nodes->budget, LXA_BUDGET_STRINGS,
            total - nodes->charged_string_bytes, &nodes->limit_reason);
    }
    if (status != LXA_OK) {
        return status;
    }
    if (!found) {
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_MEMORY,
                                    sizeof(*atom), &nodes->limit_reason);
        if (status != LXA_OK) {
            return status;
        }
        status = lxa_alloc(&nodes->allocator, sizeof(*atom), (void **) &atom);
        if (status != LXA_OK) {
            lxa_budget_release(nodes->budget, LXA_BUDGET_MEMORY,
                               sizeof(*atom));
            return status;
        }
        atom->offset = nodes->strings.length;
        atom->length = name.length;
        atom->id = atom_id;
        status = self ? lxa_buffer_append_self(&nodes->strings,
                                                self_offset, name.length)
                      : lxa_buffer_append(&nodes->strings, name);
        if (status != LXA_OK) {
            goto rollback;
        }
        {
            lxa_span_t owned = {nodes->strings.data + atom->offset, atom->length};
            bool replaced;
            /* The map does all fallible work before publishing its key.
             * No node or atom index is visible to allocator callbacks.
             */
            status = lxa_table_set(nodes->atom_lookup, owned, atom,
                                    &replaced, NULL);
            if (status != LXA_OK || replaced) {
                if (status == LXA_OK) {
                    status = LXA_ERROR_ARGUMENT;
                }
                else if (status == LXA_ERROR_LIMIT) {
                    nodes->limit_reason = lxa_table_limit_reason(nodes->atom_lookup);
                }
                goto rollback;
            }
            inserted = true;
        }
        status = lxa_vector_push(&nodes->atoms, &atom);
        if (status != LXA_OK) {
            goto rollback;
        }
        appended_atom = true;
    }
    element.atom = atom->id;
    element.namespace_id = ns;
    element.first_attr = LXA_ID_NONE;
    element.attr_count = 0;
    status = lxa_vector_push(&nodes->elements, &element);
    if (status != LXA_OK) {
        goto rollback;
    }
    memset(&record, 0, sizeof(record));
    record.parent = record.first_child = record.last_child = LXA_ID_NONE;
    record.prev = record.next = LXA_ID_NONE;
    record.kind = LXA_DOM_KIND_ELEMENT;
    record.payload = payload_id;
    record.generation = nodes->generation;
    status = lxa_vector_push(&nodes->records, &record);
    if (status != LXA_OK) {
        lxa_vector_erase(&nodes->elements, payload_id, 1);
        goto rollback;
    }
    if (total > nodes->charged_string_bytes) {
        lxa_budget_charge(nodes->budget, LXA_BUDGET_STRINGS,
                          total - nodes->charged_string_bytes, NULL);
        nodes->charged_string_bytes = total;
    }
    charge_new_slot(nodes);
    out->document = nodes;
    out->epoch = nodes->epoch;
    out->handle = packed;
    return LXA_OK;
rollback:
    if (inserted) {
        lxa_span_t key = {nodes->strings.data + atom->offset, atom->length};
        lxa_status_t undone = lxa_table_delete(nodes->atom_lookup, key,
                                                 NULL, NULL);
        if (undone != LXA_OK) {
            /* Pushes above cannot fail after the checked reserves unless
             * shared state was modified reentrantly. Retain the owned atom
             * rather than leave a table pointing at freed storage. */
            return undone;
        }
    }
    if (appended_atom) {
        lxa_vector_erase(&nodes->atoms, atom_id, 1);
    }
    if (!found && atom != NULL) {
        lxa_buffer_truncate(&nodes->strings, atom->offset);
        lxa_free(&nodes->allocator, atom);
        lxa_budget_release(nodes->budget, LXA_BUDGET_MEMORY, sizeof(*atom));
    }
    if (status == LXA_ERROR_LIMIT
        && nodes->limit_reason == LXA_BUDGET_COUNT) {
        nodes->limit_reason = LXA_BUDGET_MEMORY;
    }
    return status;
}

lxa_status_t
lxa_dom_nodes_append_html_element(lxa_dom_nodes_t *nodes, lxa_span_t name,
                                   lxa_dom_ref_t *out)
{
    return lxa_dom_nodes_append_namespaced_element(nodes, name,
                                                    LXA_DOM_NS_HTML, out);
}

lxa_status_t
lxa_dom_nodes_element_name(const lxa_dom_nodes_t *nodes,
                            lxa_dom_ref_t element, lxa_span_t *out)
{
    lxa_dom_record_t record;
    const lxa_dom_element_t *payload;
    const lxa_dom_atom_t *atom;
    lxa_status_t status;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    out->data = NULL;
    out->length = 0;
    status = lxa_dom_nodes_read(nodes, element, &record);
    if (status != LXA_OK) {
        return status;
    }
    if (record.kind != LXA_DOM_KIND_ELEMENT
        || record.payload >= lxa_vector_length(&nodes->elements)) {
        return LXA_ERROR_ARGUMENT;
    }
    payload = (const lxa_dom_element_t *) nodes->elements.bytes.data
              + record.payload;
    if (payload->atom >= lxa_vector_length(&nodes->atoms)) {
        return LXA_ERROR_ARGUMENT;
    }
    atom = ((lxa_dom_atom_t *const *) nodes->atoms.bytes.data)[payload->atom];
    if (atom == NULL || atom->offset > nodes->strings.length
        || atom->length > nodes->strings.length - atom->offset) {
        return LXA_ERROR_ARGUMENT;
    }
    out->data = nodes->strings.data + atom->offset;
    out->length = atom->length;
    return LXA_OK;
}

lxa_status_t
lxa_dom_nodes_element_namespace(const lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t element, lxa_dom_namespace_t *out)
{
    lxa_dom_record_t record;
    const lxa_dom_element_t *payload;
    if (out == NULL) return LXA_ERROR_ARGUMENT;
    *out = 0;
    if (lxa_dom_nodes_read(nodes, element, &record) != LXA_OK
        || record.kind != LXA_DOM_KIND_ELEMENT
        || record.payload >= lxa_vector_length(&nodes->elements))
        return LXA_ERROR_ARGUMENT;
    payload = (const lxa_dom_element_t *) nodes->elements.bytes.data
              + record.payload;
    if (payload->namespace_id < LXA_DOM_NS_HTML
        || payload->namespace_id > LXA_DOM_NS_SVG)
        return LXA_ERROR_ARGUMENT;
    *out = (lxa_dom_namespace_t) payload->namespace_id;
    return LXA_OK;
}

/* Treat a bound fragment's template as its virtual parent. Physical links
 * alone do not detect moving a template (or its ancestor) into its content.
 */
static lxa_status_t
check_virtual_ancestor(const lxa_dom_nodes_t *nodes, lxa_id_t start,
                       lxa_id_t target, lxa_budget_kind_t *reason)
{
    const lxa_dom_record_t *records =
        (const lxa_dom_record_t *) nodes->records.bytes.data;
    const lxa_dom_template_t *entries =
        (const lxa_dom_template_t *) nodes->templates.bytes.data;
    size_t length = lxa_vector_length(&nodes->records), steps = 0;
    lxa_id_t cursor = start;
    while (cursor != LXA_ID_NONE) {
        size_t i;
        lxa_status_t status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK,
                                                 1, reason);
        if (status != LXA_OK) return status;
        if (cursor == target || cursor < nodes->retired || cursor >= length
            || ++steps > length)
            return LXA_ERROR_ARGUMENT;
        if (records[cursor].parent != LXA_ID_NONE) {
            cursor = records[cursor].parent;
            continue;
        }
        if (records[cursor].kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT) break;
        for (i = 0; i < lxa_vector_length(&nodes->templates); ++i) {
            status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                       reason);
            if (status != LXA_OK) return status;
            if (entries[i].fragment == cursor) break;
        }
        cursor = i == lxa_vector_length(&nodes->templates)
                 ? LXA_ID_NONE : entries[i].element;
    }
    return LXA_OK;
}

lxa_status_t
lxa_dom_nodes_bind_template(lxa_dom_nodes_t *nodes, lxa_dom_ref_t element,
                            lxa_dom_ref_t fragment)
{
    lxa_dom_record_t host, contents;
    lxa_dom_namespace_t ns;
    lxa_dom_template_t pair;
    lxa_span_t name;
    size_t i;
    lxa_status_t status;
    if (nodes == NULL)
        return LXA_ERROR_ARGUMENT;
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_dom_nodes_read(nodes, element, &host);
    if (status == LXA_OK)
        status = lxa_dom_nodes_read(nodes, fragment, &contents);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_namespace(nodes, element, &ns);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_name(nodes, element, &name);
    if (status != LXA_OK) return status;
    if (ns != LXA_DOM_NS_HTML || name.length != sizeof("template") - 1
        || memcmp(name.data, "template", sizeof("template") - 1) != 0
        || contents.kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT
        || contents.parent != LXA_ID_NONE || contents.prev != LXA_ID_NONE
        || contents.next != LXA_ID_NONE || host.first_child != LXA_ID_NONE
        || host.last_child != LXA_ID_NONE)
        return LXA_ERROR_ARGUMENT;
    pair.element = (lxa_id_t) element.handle;
    pair.fragment = (lxa_id_t) fragment.handle;
    status = check_virtual_ancestor(nodes, pair.element, pair.fragment,
                                    &nodes->limit_reason);
    if (status != LXA_OK) return status;
    for (i = 0; i < lxa_vector_length(&nodes->templates); ++i) {
        const lxa_dom_template_t *entries =
            (const lxa_dom_template_t *) nodes->templates.bytes.data;
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                   &nodes->limit_reason);
        if (status != LXA_OK) return status;
        if (entries[i].element == pair.element
            || entries[i].fragment == pair.fragment)
            return LXA_ERROR_ARGUMENT;
    }
    status = lxa_vector_push(&nodes->templates, &pair);
    if (status == LXA_ERROR_LIMIT) nodes->limit_reason = LXA_BUDGET_MEMORY;
    return status;
}

lxa_status_t
lxa_dom_nodes_template_contents(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t element, lxa_dom_ref_t *out)
{
    lxa_dom_record_t host;
    const lxa_dom_template_t *entries;
    size_t i;
    lxa_status_t status;
    if (out == NULL) return LXA_ERROR_ARGUMENT;
    *out = (lxa_dom_ref_t){0};
    if (nodes == NULL) return LXA_ERROR_ARGUMENT;
    status = lxa_dom_nodes_read(nodes, element, &host);
    if (status != LXA_OK || host.kind != LXA_DOM_KIND_ELEMENT)
        return LXA_ERROR_ARGUMENT;
    nodes->limit_reason = LXA_BUDGET_COUNT;
    entries = (const lxa_dom_template_t *) nodes->templates.bytes.data;
    for (i = 0; i < lxa_vector_length(&nodes->templates); ++i) {
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                   &nodes->limit_reason);
        if (status != LXA_OK) return status;
        if (entries[i].element != (lxa_id_t) element.handle) continue;
        out->document = nodes;
        out->epoch = nodes->epoch;
        return lxa_handle_pack(entries[i].fragment, nodes->generation,
                               &out->handle);
    }
    return LXA_OK;
}

lxa_status_t
lxa_dom_nodes_template_owner(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t fragment, lxa_dom_ref_t *out)
{
    lxa_dom_record_t record;
    const lxa_dom_template_t *entries;
    size_t i;
    lxa_status_t status;
    if (out == NULL) return LXA_ERROR_ARGUMENT;
    *out = (lxa_dom_ref_t){0};
    if (nodes == NULL) return LXA_ERROR_ARGUMENT;
    status = lxa_dom_nodes_read(nodes, fragment, &record);
    if (status != LXA_OK || record.kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT
        || record.parent != LXA_ID_NONE)
        return LXA_ERROR_ARGUMENT;
    nodes->limit_reason = LXA_BUDGET_COUNT;
    entries = (const lxa_dom_template_t *) nodes->templates.bytes.data;
    for (i = 0; i < lxa_vector_length(&nodes->templates); ++i) {
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                   &nodes->limit_reason);
        if (status != LXA_OK) return status;
        if (entries[i].fragment != (lxa_id_t) fragment.handle) continue;
        out->document = nodes;
        out->epoch = nodes->epoch;
        return lxa_handle_pack(entries[i].element, nodes->generation,
                               &out->handle);
    }
    return LXA_OK;
}

static lxa_status_t
check_template_parent(lxa_dom_nodes_t *nodes, lxa_id_t parent)
{
    const lxa_dom_template_t *entries =
        (const lxa_dom_template_t *) nodes->templates.bytes.data;
    size_t i;
    for (i = 0; i < lxa_vector_length(&nodes->templates); ++i) {
        lxa_status_t status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK,
                                                 1, &nodes->limit_reason);
        if (status != LXA_OK) return status;
        if (entries[i].element == parent) return LXA_ERROR_ARGUMENT;
    }
    return LXA_OK;
}

static lxa_status_t
append_attribute(lxa_dom_nodes_t *nodes, lxa_span_t name, lxa_span_t value,
                 bool foreign, lxa_dom_attr_namespace_t ns, lxa_dom_ref_t *out)
{
    lxa_dom_attr_t attr;
    lxa_dom_record_t record;
    lxa_handle_t packed;
    lxa_id_t node_id, payload_id;
    size_t next, total, i, name_source, value_source;
    bool name_self, value_self;
    lxa_status_t status;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (nodes == NULL || name.data == NULL || name.length == 0
        || (value.data == NULL && value.length != 0)) {
        return LXA_ERROR_ARGUMENT;
    }
    if (ns > LXA_DOM_ATTR_NS_XMLNS
        || (!foreign && ns != LXA_DOM_ATTR_NS_NONE))
        return LXA_ERROR_ARGUMENT;
    if ((ns == LXA_DOM_ATTR_NS_XML
         && (name.length <= 4 || memcmp(name.data, "xml:", 4) != 0))
        || (ns == LXA_DOM_ATTR_NS_XLINK
            && (name.length <= 6 || memcmp(name.data, "xlink:", 6) != 0))
        || (ns == LXA_DOM_ATTR_NS_XMLNS
            && !(name.length == 5 && memcmp(name.data, "xmlns", 5) == 0)
            && !(name.length > 6
                 && memcmp(name.data, "xmlns:", 6) == 0)))
        return LXA_ERROR_ARGUMENT;
    status = string_source(nodes, name, &name_self, &name_source);
    if (status == LXA_OK) {
        status = string_source(nodes, value, &value_self, &value_source);
    }
    if (status != LXA_OK) {
        return status;
    }
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_budget_check_item(nodes->budget, LXA_BUDGET_NAME_BYTES,
                                    name.length, &nodes->limit_reason);
    if (status != LXA_OK) {
        return status;
    }
    for (i = 0; i < name.length; ++i) {
        uint8_t c = name.data[i];
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                    &nodes->limit_reason);
        if (status != LXA_OK) {
            return status;
        }
        if (!html_name_byte(c)
            && !(foreign && c >= 'A' && c <= 'Z')) {
            return LXA_ERROR_UNSUPPORTED;
        }
    }
    status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, value.length,
                                &nodes->limit_reason);
    if (status == LXA_OK) {
        status = lxa_size_to_id(lxa_vector_length(&nodes->records), &node_id);
    }
    if (status == LXA_OK) {
        status = lxa_handle_pack(node_id, nodes->generation, &packed);
    }
    if (status == LXA_OK) {
        status = lxa_size_to_id(lxa_vector_length(&nodes->attributes), &payload_id);
    }
    if (status == LXA_OK) {
        status = check_new_slot(nodes, &nodes->limit_reason);
    }
    if (status == LXA_OK && lxa_vector_length(&nodes->attributes)
                            >= nodes->charged_attrs) {
        status = lxa_budget_check(nodes->budget, LXA_BUDGET_ATTRIBUTES, 1,
                                   &nodes->limit_reason);
    }
    if (status == LXA_OK) {
        status = lxa_size_add(nodes->strings.length, name.length, &total);
    }
    if (status == LXA_OK) {
        status = lxa_size_add(total, value.length, &total);
    }
    if (status == LXA_OK && total > nodes->charged_string_bytes) {
        status = lxa_budget_check(nodes->budget, LXA_BUDGET_STRINGS,
            total - nodes->charged_string_bytes, &nodes->limit_reason);
    }
    if (status == LXA_OK) {
        status = lxa_size_add(lxa_vector_length(&nodes->records), 1, &next);
    }
    if (status == LXA_OK) {
        status = lxa_vector_reserve(&nodes->records, next);
    }
    if (status == LXA_OK) {
        status = lxa_size_add(lxa_vector_length(&nodes->attributes), 1, &next);
    }
    if (status == LXA_OK) {
        status = lxa_vector_reserve(&nodes->attributes, next);
    }
    if (status == LXA_OK) {
        status = lxa_buffer_reserve(&nodes->strings, total);
    }
    if (status != LXA_OK) {
        if (status == LXA_ERROR_LIMIT
            && nodes->limit_reason == LXA_BUDGET_COUNT) {
            nodes->limit_reason = LXA_BUDGET_MEMORY;
        }
        return status;
    }
    status = check_new_slot(nodes, &nodes->limit_reason);
    if (status == LXA_OK && lxa_vector_length(&nodes->attributes)
                            >= nodes->charged_attrs) {
        status = lxa_budget_check(nodes->budget, LXA_BUDGET_ATTRIBUTES, 1,
                                   &nodes->limit_reason);
    }
    if (status == LXA_OK && total > nodes->charged_string_bytes) {
        status = lxa_budget_check(nodes->budget, LXA_BUDGET_STRINGS,
            total - nodes->charged_string_bytes, &nodes->limit_reason);
    }
    if (status != LXA_OK) {
        return status;
    }
    attr.name_offset = nodes->strings.length;
    attr.name_length = name.length;
    attr.value_offset = attr.name_offset + name.length;
    attr.value_length = value.length;
    attr.owner = attr.next = LXA_ID_NONE;
    attr.namespace_id = ns;
    status = name_self ? lxa_buffer_append_self(&nodes->strings,
                                                 name_source, name.length)
                       : lxa_buffer_append(&nodes->strings, name);
    if (status == LXA_OK) {
        status = value_self ? lxa_buffer_append_self(&nodes->strings,
                                                      value_source, value.length)
                            : lxa_buffer_append(&nodes->strings, value);
    }
    if (status != LXA_OK) {
        lxa_buffer_truncate(&nodes->strings, attr.name_offset);
        return status;
    }
    status = lxa_vector_push(&nodes->attributes, &attr);
    if (status != LXA_OK) {
        lxa_buffer_truncate(&nodes->strings, attr.name_offset);
        return status;
    }
    memset(&record, 0, sizeof(record));
    record.parent = record.first_child = record.last_child = LXA_ID_NONE;
    record.prev = record.next = LXA_ID_NONE;
    record.kind = LXA_DOM_KIND_ATTRIBUTE;
    record.payload = payload_id;
    record.generation = nodes->generation;
    status = lxa_vector_push(&nodes->records, &record);
    if (status != LXA_OK) {
        lxa_vector_erase(&nodes->attributes, payload_id, 1);
        lxa_buffer_truncate(&nodes->strings, attr.name_offset);
        return status;
    }
    if (total > nodes->charged_string_bytes) {
        lxa_budget_charge(nodes->budget, LXA_BUDGET_STRINGS,
                          total - nodes->charged_string_bytes, NULL);
        nodes->charged_string_bytes = total;
    }
    if (lxa_vector_length(&nodes->attributes) > nodes->charged_attrs) {
        lxa_budget_charge(nodes->budget, LXA_BUDGET_ATTRIBUTES, 1, NULL);
        ++nodes->charged_attrs;
    }
    charge_new_slot(nodes);
    out->document = nodes;
    out->epoch = nodes->epoch;
    out->handle = packed;
    return LXA_OK;
}

lxa_status_t
lxa_dom_nodes_append_html_attribute(lxa_dom_nodes_t *nodes,
    lxa_span_t name, lxa_span_t value, lxa_dom_ref_t *out)
{
    return append_attribute(nodes, name, value, false,
                            LXA_DOM_ATTR_NS_NONE, out);
}

lxa_status_t
lxa_dom_nodes_append_foreign_attribute(lxa_dom_nodes_t *nodes,
    lxa_span_t name, lxa_span_t value, lxa_dom_attr_namespace_t ns,
    lxa_dom_ref_t *out)
{
    return append_attribute(nodes, name, value, true, ns, out);
}

lxa_status_t
lxa_dom_nodes_attribute_namespace(const lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t attribute, lxa_dom_attr_namespace_t *ns,
    lxa_span_t *local_name)
{
    lxa_span_t name, value;
    lxa_dom_record_t record;
    const lxa_dom_attr_t *attr;
    size_t prefix = 0;
    lxa_status_t status;
    if (ns == NULL || local_name == NULL) return LXA_ERROR_ARGUMENT;
    status = lxa_dom_nodes_read(nodes, attribute, &record);
    if (status != LXA_OK) return status;
    if (record.kind != LXA_DOM_KIND_ATTRIBUTE
        || record.payload >= lxa_vector_length(&nodes->attributes))
        return LXA_ERROR_ARGUMENT;
    attr = (const lxa_dom_attr_t *) nodes->attributes.bytes.data + record.payload;
    status = lxa_dom_nodes_attribute_read(nodes, attribute, &name, &value);
    if (status != LXA_OK) return status;
    if (attr->namespace_id == LXA_DOM_ATTR_NS_XML
        || attr->namespace_id == LXA_DOM_ATTR_NS_XMLNS
        || attr->namespace_id == LXA_DOM_ATTR_NS_XLINK) {
        if (attr->namespace_id == LXA_DOM_ATTR_NS_XMLNS
            && name.length == 5 && memcmp(name.data, "xmlns", 5) == 0)
            prefix = 0;
        else prefix = attr->namespace_id == LXA_DOM_ATTR_NS_XLINK ? 6
                      : attr->namespace_id == LXA_DOM_ATTR_NS_XMLNS ? 6 : 4;
    }
    if (attr->namespace_id > LXA_DOM_ATTR_NS_XMLNS
        || prefix >= name.length) return LXA_ERROR_ARGUMENT;
    *ns = attr->namespace_id;
    local_name->data = name.data + prefix;
    local_name->length = name.length - prefix;
    return LXA_OK;
}

lxa_status_t
lxa_dom_nodes_attribute_read(const lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t attribute, lxa_span_t *name, lxa_span_t *value)
{
    lxa_dom_record_t record;
    const lxa_dom_attr_t *attr;
    lxa_status_t status;
    if (name == NULL || value == NULL || name == value) {
        return LXA_ERROR_ARGUMENT;
    }
    name->data = value->data = NULL;
    name->length = value->length = 0;
    status = lxa_dom_nodes_read(nodes, attribute, &record);
    if (status != LXA_OK) {
        return status;
    }
    if (record.kind != LXA_DOM_KIND_ATTRIBUTE
        || record.payload >= lxa_vector_length(&nodes->attributes)) {
        return LXA_ERROR_ARGUMENT;
    }
    attr = (const lxa_dom_attr_t *) nodes->attributes.bytes.data + record.payload;
    if (attr->name_offset > nodes->strings.length
        || attr->name_length > nodes->strings.length - attr->name_offset
        || attr->value_offset > nodes->strings.length
        || attr->value_length > nodes->strings.length - attr->value_offset) {
        return LXA_ERROR_ARGUMENT;
    }
    name->data = nodes->strings.data + attr->name_offset;
    name->length = attr->name_length;
    value->data = nodes->strings.data + attr->value_offset;
    value->length = attr->value_length;
    return LXA_OK;
}

lxa_status_t
lxa_dom_nodes_attribute_set_value(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t attribute, lxa_span_t value)
{
    lxa_dom_record_t record;
    lxa_dom_attr_t *attr;
    size_t offset, total, self_offset;
    bool self;
    lxa_status_t status;
    if (nodes == NULL || (value.data == NULL && value.length != 0)) {
        return LXA_ERROR_ARGUMENT;
    }
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_dom_nodes_read(nodes, attribute, &record);
    if (status != LXA_OK || record.kind != LXA_DOM_KIND_ATTRIBUTE
        || record.payload >= lxa_vector_length(&nodes->attributes)) {
        return LXA_ERROR_ARGUMENT;
    }
    attr = (lxa_dom_attr_t *) nodes->attributes.bytes.data + record.payload;
    if (attr->name_offset > nodes->strings.length
        || attr->name_length > nodes->strings.length - attr->name_offset
        || attr->value_offset > nodes->strings.length
        || attr->value_length > nodes->strings.length - attr->value_offset) {
        return LXA_ERROR_ARGUMENT;
    }
    status = string_source(nodes, value, &self, &self_offset);
    if (status != LXA_OK) {
        return status;
    }
    offset = nodes->strings.length;
    status = lxa_size_add(offset, value.length, &total);
    if (status == LXA_OK && total > nodes->charged_string_bytes) {
        status = lxa_budget_check(nodes->budget, LXA_BUDGET_STRINGS,
            total - nodes->charged_string_bytes, &nodes->limit_reason);
    }
    if (status == LXA_OK) {
        status = lxa_buffer_reserve(&nodes->strings, total);
    }
    if (status != LXA_OK) {
        if (status == LXA_ERROR_LIMIT
            && nodes->limit_reason == LXA_BUDGET_COUNT) {
            nodes->limit_reason = LXA_BUDGET_MEMORY;
        }
        return status;
    }
    if (total > nodes->charged_string_bytes) {
        status = lxa_budget_check(nodes->budget, LXA_BUDGET_STRINGS,
            total - nodes->charged_string_bytes, &nodes->limit_reason);
        if (status != LXA_OK) {
            return status;
        }
    }
    status = self ? lxa_buffer_append_self(&nodes->strings, self_offset,
                                            value.length)
                  : lxa_buffer_append(&nodes->strings, value);
    if (status != LXA_OK) {
        if (status == LXA_ERROR_LIMIT) {
            nodes->limit_reason = LXA_BUDGET_MEMORY;
        }
        return status;
    }
    /* No fallible work between publishing bytes and switching the value. */
    attr->value_offset = offset;
    attr->value_length = value.length;
    if (total > nodes->charged_string_bytes) {
        lxa_budget_charge(nodes->budget, LXA_BUDGET_STRINGS,
                          total - nodes->charged_string_bytes, NULL);
        nodes->charged_string_bytes = total;
    }
    return LXA_OK;
}

lxa_status_t
lxa_dom_nodes_append_doctype(lxa_dom_nodes_t *nodes,
    lxa_dom_doctype_data_t data, lxa_dom_ref_t *out)
{
    lxa_span_t fields[3] = {data.name, data.public_id, data.system_id};
    size_t sources[3], positions[3], total, next, i;
    bool self[3];
    lxa_dom_doctype_t payload;
    lxa_dom_record_t record;
    lxa_id_t id, payload_id;
    lxa_handle_t handle;
    lxa_status_t status;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (nodes == NULL || (fields[0].data == NULL && fields[0].length != 0)
        || (fields[1].data == NULL && fields[1].length != 0)
        || (fields[2].data == NULL && fields[2].length != 0)
        || (!data.has_public_id && fields[1].length != 0)
        || (!data.has_system_id && fields[2].length != 0)) {
        return LXA_ERROR_ARGUMENT;
    }
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_budget_check_item(nodes->budget, LXA_BUDGET_NAME_BYTES,
        fields[0].length, &nodes->limit_reason);
    if (status == LXA_OK) {
        status = lxa_budget_check_item(nodes->budget, LXA_BUDGET_TOKEN_BYTES,
            fields[1].length, &nodes->limit_reason);
    }
    if (status == LXA_OK) {
        status = lxa_budget_check_item(nodes->budget, LXA_BUDGET_TOKEN_BYTES,
            fields[2].length, &nodes->limit_reason);
    }
    for (i = 0; status == LXA_OK && i < 3; ++i) {
        status = string_source(nodes, fields[i], &self[i], &sources[i]);
    }
    if (status != LXA_OK) {
        return status;
    }
    status = lxa_size_to_id(lxa_vector_length(&nodes->records), &id);
    if (status == LXA_OK) {
        status = lxa_handle_pack(id, nodes->generation, &handle);
    }
    if (status == LXA_OK) {
        status = lxa_size_to_id(lxa_vector_length(&nodes->doctypes), &payload_id);
    }
    if (status == LXA_OK) {
        status = check_new_slot(nodes, &nodes->limit_reason);
    }
    total = nodes->strings.length;
    for (i = 0; status == LXA_OK && i < 3; ++i) {
        positions[i] = total;
        status = lxa_size_add(total, fields[i].length, &total);
    }
    if (status == LXA_OK) {
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK,
                                    total - nodes->strings.length,
                                    &nodes->limit_reason);
    }
    if (status == LXA_OK && total > nodes->charged_string_bytes) {
        status = lxa_budget_check(nodes->budget, LXA_BUDGET_STRINGS,
            total - nodes->charged_string_bytes, &nodes->limit_reason);
    }
    if (status == LXA_OK) {
        status = lxa_size_add(lxa_vector_length(&nodes->records), 1, &next);
    }
    if (status == LXA_OK) {
        status = lxa_vector_reserve(&nodes->records, next);
    }
    if (status == LXA_OK) {
        status = lxa_size_add(lxa_vector_length(&nodes->doctypes), 1, &next);
    }
    if (status == LXA_OK) {
        status = lxa_vector_reserve(&nodes->doctypes, next);
    }
    if (status == LXA_OK) {
        status = lxa_buffer_reserve(&nodes->strings, total);
    }
    if (status != LXA_OK) {
        if (status == LXA_ERROR_LIMIT
            && nodes->limit_reason == LXA_BUDGET_COUNT) {
            nodes->limit_reason = LXA_BUDGET_MEMORY;
        }
        return status;
    }
    status = check_new_slot(nodes, &nodes->limit_reason);
    if (status == LXA_OK && total > nodes->charged_string_bytes) {
        status = lxa_budget_check(nodes->budget, LXA_BUDGET_STRINGS,
            total - nodes->charged_string_bytes, &nodes->limit_reason);
    }
    if (status != LXA_OK) {
        return status;
    }
    for (i = 0; i < 3; ++i) {
        status = self[i] ? lxa_buffer_append_self(&nodes->strings,
                                                   sources[i], fields[i].length)
                         : lxa_buffer_append(&nodes->strings, fields[i]);
        if (status != LXA_OK) {
            lxa_buffer_truncate(&nodes->strings, positions[0]);
            return status;
        }
    }
    memset(&payload, 0, sizeof(payload));
    payload.name_offset = positions[0];
    payload.name_length = fields[0].length;
    payload.public_offset = positions[1];
    payload.public_length = fields[1].length;
    payload.system_offset = positions[2];
    payload.system_length = fields[2].length;
    payload.has_public = data.has_public_id;
    payload.has_system = data.has_system_id;
    payload.force_quirks = data.force_quirks;
    status = lxa_vector_push(&nodes->doctypes, &payload);
    if (status != LXA_OK) {
        lxa_buffer_truncate(&nodes->strings, positions[0]);
        return status;
    }
    memset(&record, 0, sizeof(record));
    record.parent = record.first_child = record.last_child = LXA_ID_NONE;
    record.prev = record.next = LXA_ID_NONE;
    record.kind = LXA_DOM_KIND_DOCUMENT_TYPE;
    record.payload = payload_id;
    record.generation = nodes->generation;
    status = lxa_vector_push(&nodes->records, &record);
    if (status != LXA_OK) {
        lxa_vector_erase(&nodes->doctypes, payload_id, 1);
        lxa_buffer_truncate(&nodes->strings, positions[0]);
        return status;
    }
    if (total > nodes->charged_string_bytes) {
        lxa_budget_charge(nodes->budget, LXA_BUDGET_STRINGS,
                          total - nodes->charged_string_bytes, NULL);
        nodes->charged_string_bytes = total;
    }
    charge_new_slot(nodes);
    out->document = nodes;
    out->epoch = nodes->epoch;
    out->handle = handle;
    return LXA_OK;
}

lxa_status_t
lxa_dom_nodes_doctype_read(const lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t doctype, lxa_dom_doctype_data_t *out)
{
    lxa_dom_record_t record;
    const lxa_dom_doctype_t *payload;
    lxa_status_t status;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    status = lxa_dom_nodes_read(nodes, doctype, &record);
    if (status != LXA_OK || record.kind != LXA_DOM_KIND_DOCUMENT_TYPE
        || record.payload >= lxa_vector_length(&nodes->doctypes)) {
        return LXA_ERROR_ARGUMENT;
    }
    payload = (const lxa_dom_doctype_t *) nodes->doctypes.bytes.data
              + record.payload;
    if (payload->name_offset > nodes->strings.length
        || payload->name_length > nodes->strings.length - payload->name_offset
        || payload->public_offset > nodes->strings.length
        || payload->public_length > nodes->strings.length - payload->public_offset
        || payload->system_offset > nodes->strings.length
        || payload->system_length > nodes->strings.length - payload->system_offset) {
        return LXA_ERROR_ARGUMENT;
    }
    out->name.data = nodes->strings.data == NULL ? NULL
                     : nodes->strings.data + payload->name_offset;
    out->name.length = payload->name_length;
    if (payload->has_public) {
        out->public_id.data = nodes->strings.data == NULL ? NULL
                              : nodes->strings.data + payload->public_offset;
        out->public_id.length = payload->public_length;
    }
    if (payload->has_system) {
        out->system_id.data = nodes->strings.data == NULL ? NULL
                              : nodes->strings.data + payload->system_offset;
        out->system_id.length = payload->system_length;
    }
    out->has_public_id = payload->has_public;
    out->has_system_id = payload->has_system;
    out->force_quirks = payload->force_quirks;
    return LXA_OK;
}

/* PI target and data are separate owned spans; tokenizer callback storage
 * and document string growth cannot change either after publication. */
lxa_status_t
lxa_dom_nodes_append_pi(lxa_dom_nodes_t *nodes, lxa_span_t target,
                        lxa_span_t data, lxa_dom_ref_t *out)
{
    lxa_span_t fields[2] = {target, data};
    size_t sources[2], positions[2], total, next, i;
    bool self[2];
    lxa_dom_pi_t payload;
    lxa_dom_record_t record;
    lxa_id_t id, payload_id;
    lxa_handle_t handle;
    lxa_status_t status;
    if (out == NULL) return LXA_ERROR_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (nodes == NULL || target.data == NULL || target.length == 0
        || (data.data == NULL && data.length != 0)
        || !((target.data[0] >= 'a' && target.data[0] <= 'z')
             || (target.data[0] >= 'A' && target.data[0] <= 'Z')
             || target.data[0] == '_'))
        return LXA_ERROR_ARGUMENT;
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_budget_check_item(nodes->budget, LXA_BUDGET_NAME_BYTES,
                                    target.length, &nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_check_item(nodes->budget, LXA_BUDGET_TOKEN_BYTES,
                                        data.length, &nodes->limit_reason);
    if (status != LXA_OK) return status;
    status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK,
                                target.length, &nodes->limit_reason);
    if (status != LXA_OK) return status;
    for (i = 0; i < target.length; ++i) {
        uint8_t c = target.data[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
              || (c >= '0' && c <= '9' && i != 0)
              || c == '_' || (c == '-' && i != 0)))
            return LXA_ERROR_ARGUMENT;
    }
    if (target.length == 3 || target.length == 14) {
        const char *forbidden = target.length == 3 ? "xml" : "xml-stylesheet";
        for (i = 0; i < target.length; ++i) {
            uint8_t c = target.data[i];
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (c != (uint8_t) forbidden[i]) break;
        }
        if (i == target.length) return LXA_ERROR_ARGUMENT;
    }
    for (i = 0; i < 2; ++i) {
        status = string_source(nodes, fields[i], &self[i], &sources[i]);
        if (status != LXA_OK) return status;
    }
    status = lxa_size_to_id(lxa_vector_length(&nodes->records), &id);
    if (status == LXA_OK)
        status = lxa_handle_pack(id, nodes->generation, &handle);
    if (status == LXA_OK)
        status = lxa_size_to_id(lxa_vector_length(&nodes->pis), &payload_id);
    if (status == LXA_OK)
        status = check_new_slot(nodes, &nodes->limit_reason);
    total = nodes->strings.length;
    for (i = 0; status == LXA_OK && i < 2; ++i) {
        positions[i] = total;
        status = lxa_size_add(total, fields[i].length, &total);
    }
    if (status == LXA_OK)
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK,
                                    total - nodes->strings.length,
                                    &nodes->limit_reason);
    if (status == LXA_OK && total > nodes->charged_string_bytes)
        status = lxa_budget_check(nodes->budget, LXA_BUDGET_STRINGS,
            total - nodes->charged_string_bytes, &nodes->limit_reason);
    if (status == LXA_OK)
        status = lxa_size_add(lxa_vector_length(&nodes->records), 1, &next);
    if (status == LXA_OK) status = lxa_vector_reserve(&nodes->records, next);
    if (status == LXA_OK)
        status = lxa_size_add(lxa_vector_length(&nodes->pis), 1, &next);
    if (status == LXA_OK) status = lxa_vector_reserve(&nodes->pis, next);
    if (status == LXA_OK) status = lxa_buffer_reserve(&nodes->strings, total);
    if (status != LXA_OK) {
        if (status == LXA_ERROR_LIMIT
            && nodes->limit_reason == LXA_BUDGET_COUNT)
            nodes->limit_reason = LXA_BUDGET_MEMORY;
        return status;
    }
    status = check_new_slot(nodes, &nodes->limit_reason);
    if (status == LXA_OK && total > nodes->charged_string_bytes)
        status = lxa_budget_check(nodes->budget, LXA_BUDGET_STRINGS,
            total - nodes->charged_string_bytes, &nodes->limit_reason);
    if (status != LXA_OK) return status;
    for (i = 0; i < 2; ++i) {
        status = self[i] ? lxa_buffer_append_self(&nodes->strings, sources[i],
                                                   fields[i].length)
                         : lxa_buffer_append(&nodes->strings, fields[i]);
        if (status != LXA_OK) {
            lxa_buffer_truncate(&nodes->strings, positions[0]);
            return status;
        }
    }
    payload.target = (lxa_dom_character_t){positions[0], target.length};
    payload.data = (lxa_dom_character_t){positions[1], data.length};
    status = lxa_vector_push(&nodes->pis, &payload);
    if (status != LXA_OK) {
        lxa_buffer_truncate(&nodes->strings, positions[0]);
        return status;
    }
    memset(&record, 0, sizeof(record));
    record.parent = record.first_child = record.last_child = LXA_ID_NONE;
    record.prev = record.next = LXA_ID_NONE;
    record.kind = LXA_DOM_KIND_PROCESSING_INSTRUCTION;
    record.payload = payload_id;
    record.generation = nodes->generation;
    status = lxa_vector_push(&nodes->records, &record);
    if (status != LXA_OK) {
        lxa_vector_erase(&nodes->pis, payload_id, 1);
        lxa_buffer_truncate(&nodes->strings, positions[0]);
        return status;
    }
    if (total > nodes->charged_string_bytes) {
        lxa_budget_charge(nodes->budget, LXA_BUDGET_STRINGS,
                          total - nodes->charged_string_bytes, NULL);
        nodes->charged_string_bytes = total;
    }
    charge_new_slot(nodes);
    out->document = nodes;
    out->epoch = nodes->epoch;
    out->handle = handle;
    return LXA_OK;
}

lxa_status_t
lxa_dom_nodes_pi_read(const lxa_dom_nodes_t *nodes, lxa_dom_ref_t pi,
                       lxa_span_t *target, lxa_span_t *data)
{
    lxa_dom_record_t record;
    const lxa_dom_pi_t *payload;
    if (target == NULL || data == NULL || target == data)
        return LXA_ERROR_ARGUMENT;
    *target = (lxa_span_t){0};
    *data = (lxa_span_t){0};
    if (lxa_dom_nodes_read(nodes, pi, &record) != LXA_OK
        || record.kind != LXA_DOM_KIND_PROCESSING_INSTRUCTION
        || record.payload >= lxa_vector_length(&nodes->pis))
        return LXA_ERROR_ARGUMENT;
    payload = (const lxa_dom_pi_t *) nodes->pis.bytes.data + record.payload;
    if (payload->target.offset > nodes->strings.length
        || payload->target.length > nodes->strings.length - payload->target.offset
        || payload->data.offset > nodes->strings.length
        || payload->data.length > nodes->strings.length - payload->data.offset)
        return LXA_ERROR_ARGUMENT;
    *target = (lxa_span_t){nodes->strings.data + payload->target.offset,
                           payload->target.length};
    *data = (lxa_span_t){nodes->strings.data + payload->data.offset,
                         payload->data.length};
    return LXA_OK;
}

static lxa_status_t
scan_attributes(lxa_dom_nodes_t *nodes, lxa_id_t element_id,
                lxa_dom_element_t element, lxa_span_t name,
                lxa_id_t *match, lxa_id_t *tail, lxa_id_t *predecessor)
{
    const lxa_dom_record_t *records =
        (const lxa_dom_record_t *) nodes->records.bytes.data;
    const lxa_dom_attr_t *attrs =
        (const lxa_dom_attr_t *) nodes->attributes.bytes.data;
    lxa_id_t cursor = element.first_attr;
    size_t count = 0;
    *match = *tail = LXA_ID_NONE;
    if (predecessor != NULL) {
        *predecessor = LXA_ID_NONE;
    }
    while (cursor != LXA_ID_NONE) {
        const lxa_dom_attr_t *attr;
        size_t i;
        lxa_status_t status = lxa_budget_charge(nodes->budget,
            LXA_BUDGET_WORK, 1, &nodes->limit_reason);
        if (status != LXA_OK) {
            return status;
        }
        if (count >= element.attr_count || cursor < nodes->retired
            || cursor >= lxa_vector_length(&nodes->records)
            || records[cursor].kind != LXA_DOM_KIND_ATTRIBUTE
            || records[cursor].payload >= lxa_vector_length(&nodes->attributes)) {
            return LXA_ERROR_ARGUMENT;
        }
        ++count;
        attr = &attrs[records[cursor].payload];
        if (attr->owner != element_id || attr->name_offset > nodes->strings.length
            || attr->name_length > nodes->strings.length - attr->name_offset
            || attr->value_offset > nodes->strings.length
            || attr->value_length > nodes->strings.length - attr->value_offset) {
            return LXA_ERROR_ARGUMENT;
        }
        if (attr->name_length == name.length) {
            for (i = 0; i < name.length; ++i) {
                status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK,
                                            1, &nodes->limit_reason);
                if (status != LXA_OK) {
                    return status;
                }
                if (nodes->strings.data[attr->name_offset + i] != name.data[i]) {
                    break;
                }
            }
            if (i == name.length && *match == LXA_ID_NONE) {
                *match = cursor;
                if (predecessor != NULL) {
                    *predecessor = *tail;
                }
            }
        }
        *tail = cursor;
        cursor = attr->next;
    }
    return count == element.attr_count ? LXA_OK : LXA_ERROR_ARGUMENT;
}

static lxa_status_t
option_selected_attribute(lxa_dom_nodes_t *nodes, lxa_dom_ref_t element,
                          lxa_dom_record_t host, lxa_span_t name,
                          lxa_dom_attr_namespace_t ns, bool *applies)
{
    const lxa_dom_element_t *data =
        (const lxa_dom_element_t *) nodes->elements.bytes.data + host.payload;
    lxa_span_t tag;
    lxa_status_t status;
    *applies = false;
    if (data->namespace_id != LXA_DOM_NS_HTML
        || (host.flags & LXA_DOM_OPTION_DIRTY) != 0
        || ns != LXA_DOM_ATTR_NS_NONE || name.length != 8)
        return LXA_OK;
    status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 8,
                               &nodes->limit_reason);
    if (status != LXA_OK || memcmp(name.data, "selected", 8) != 0)
        return status;
    status = lxa_dom_nodes_element_name(nodes, element, &tag);
    if (status == LXA_OK && tag.length == 6) {
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 6,
                                   &nodes->limit_reason);
        if (status == LXA_OK) *applies = memcmp(tag.data, "option", 6) == 0;
    }
    return status;
}

lxa_status_t
lxa_dom_nodes_attach_attribute(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t element, lxa_dom_ref_t attribute)
{
    lxa_dom_record_t host, child;
    lxa_dom_element_t *elements, *data;
    lxa_dom_attr_t *attrs, *attr;
    lxa_id_t host_id, attr_id, match, tail;
    lxa_span_t name, value;
    bool select_option;
    lxa_status_t status;
    if (nodes == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_dom_nodes_read(nodes, element, &host);
    if (status == LXA_OK) {
        status = lxa_dom_nodes_read(nodes, attribute, &child);
    }
    if (status != LXA_OK || host.kind != LXA_DOM_KIND_ELEMENT
        || child.kind != LXA_DOM_KIND_ATTRIBUTE
        || host.payload >= lxa_vector_length(&nodes->elements)
        || child.payload >= lxa_vector_length(&nodes->attributes)) {
        return LXA_ERROR_ARGUMENT;
    }
    elements = (lxa_dom_element_t *) nodes->elements.bytes.data;
    attrs = (lxa_dom_attr_t *) nodes->attributes.bytes.data;
    data = &elements[host.payload];
    attr = &attrs[child.payload];
    if (attr->owner != LXA_ID_NONE || attr->next != LXA_ID_NONE
        || data->namespace_id < LXA_DOM_NS_HTML
        || data->namespace_id > LXA_DOM_NS_SVG
        || lxa_dom_nodes_attribute_read(nodes, attribute, &name, &value)
           != LXA_OK) {
        return LXA_ERROR_ARGUMENT;
    }
    host_id = (lxa_id_t) element.handle;
    attr_id = (lxa_id_t) attribute.handle;
    status = scan_attributes(nodes, host_id, *data, name, &match, &tail, NULL);
    if (status != LXA_OK || match != LXA_ID_NONE) {
        return status == LXA_OK ? LXA_ERROR_ARGUMENT : status;
    }
    status = lxa_budget_check_item(nodes->budget, LXA_BUDGET_ATTRS_PER_ELEMENT,
                                    (uint64_t) data->attr_count + 1,
                                    &nodes->limit_reason);
    if (status != LXA_OK) {
        return status;
    }
    if (data->attr_count == UINT32_MAX) {
        return LXA_ERROR_OVERFLOW;
    }
    status = option_selected_attribute(nodes, element, host, name,
                                       attr->namespace_id, &select_option);
    if (status != LXA_OK) return status;
    /* No allocation, callbacks or fallible work after this point. */
    attr->owner = host_id;
    if (tail == LXA_ID_NONE) {
        data->first_attr = attr_id;
    }
    else {
        attrs[((lxa_dom_record_t *) nodes->records.bytes.data)[tail].payload].next
            = attr_id;
    }
    ++data->attr_count;
    if (select_option)
        ((lxa_dom_record_t *) nodes->records.bytes.data)[host_id].flags
            |= LXA_DOM_OPTION_SELECTED;
    return LXA_OK;
}

lxa_status_t
lxa_dom_nodes_detach_attribute(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t element, lxa_dom_ref_t attribute)
{
    lxa_dom_record_t host, child;
    lxa_dom_element_t *data;
    lxa_dom_attr_t *attrs, *attr;
    lxa_id_t host_id, attr_id, match, tail, predecessor;
    lxa_span_t name, value;
    bool deselect_option;
    lxa_status_t status;
    if (nodes == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_dom_nodes_read(nodes, element, &host);
    if (status == LXA_OK) {
        status = lxa_dom_nodes_read(nodes, attribute, &child);
    }
    if (status != LXA_OK || host.kind != LXA_DOM_KIND_ELEMENT
        || child.kind != LXA_DOM_KIND_ATTRIBUTE
        || host.payload >= lxa_vector_length(&nodes->elements)
        || child.payload >= lxa_vector_length(&nodes->attributes)) {
        return LXA_ERROR_ARGUMENT;
    }
    host_id = (lxa_id_t) element.handle;
    attr_id = (lxa_id_t) attribute.handle;
    data = (lxa_dom_element_t *) nodes->elements.bytes.data + host.payload;
    attrs = (lxa_dom_attr_t *) nodes->attributes.bytes.data;
    attr = &attrs[child.payload];
    if (data->namespace_id < LXA_DOM_NS_HTML
        || data->namespace_id > LXA_DOM_NS_SVG
        || attr->owner != host_id
        || lxa_dom_nodes_attribute_read(nodes, attribute, &name, &value)
           != LXA_OK) {
        return LXA_ERROR_ARGUMENT;
    }
    status = scan_attributes(nodes, host_id, *data, name, &match, &tail,
                              &predecessor);
    if (status != LXA_OK || match != attr_id || data->attr_count == 0) {
        return status == LXA_OK ? LXA_ERROR_ARGUMENT : status;
    }
    status = option_selected_attribute(nodes, element, host, name,
                                       attr->namespace_id, &deselect_option);
    if (status != LXA_OK) return status;
    /* All inspection and budgeted work precedes this link commit. */
    if (predecessor == LXA_ID_NONE) {
        data->first_attr = attr->next;
    }
    else {
        attrs[((lxa_dom_record_t *) nodes->records.bytes.data)[predecessor].payload]
            .next = attr->next;
    }
    attr->owner = attr->next = LXA_ID_NONE;
    --data->attr_count;
    if (deselect_option)
        ((lxa_dom_record_t *) nodes->records.bytes.data)[host_id].flags
            &= (uint16_t) ~LXA_DOM_OPTION_SELECTED;
    return LXA_OK;
}

lxa_status_t
lxa_dom_nodes_replace_attribute(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t element, lxa_dom_ref_t old_attribute,
    lxa_dom_ref_t new_attribute)
{
    lxa_dom_record_t host, old_record, new_record;
    lxa_dom_element_t *data;
    lxa_dom_attr_t *attrs, *old_attr, *new_attr;
    lxa_id_t host_id, old_id, new_id, match, tail, predecessor;
    lxa_span_t old_name, new_name, value;
    bool deselect_option, select_option;
    lxa_status_t status;
    if (nodes == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_dom_nodes_read(nodes, element, &host);
    if (status == LXA_OK) {
        status = lxa_dom_nodes_read(nodes, old_attribute, &old_record);
    }
    if (status == LXA_OK) {
        status = lxa_dom_nodes_read(nodes, new_attribute, &new_record);
    }
    if (status != LXA_OK || host.kind != LXA_DOM_KIND_ELEMENT
        || old_record.kind != LXA_DOM_KIND_ATTRIBUTE
        || new_record.kind != LXA_DOM_KIND_ATTRIBUTE
        || host.payload >= lxa_vector_length(&nodes->elements)
        || old_record.payload >= lxa_vector_length(&nodes->attributes)
        || new_record.payload >= lxa_vector_length(&nodes->attributes)) {
        return LXA_ERROR_ARGUMENT;
    }
    host_id = (lxa_id_t) element.handle;
    old_id = (lxa_id_t) old_attribute.handle;
    new_id = (lxa_id_t) new_attribute.handle;
    data = (lxa_dom_element_t *) nodes->elements.bytes.data + host.payload;
    attrs = (lxa_dom_attr_t *) nodes->attributes.bytes.data;
    old_attr = &attrs[old_record.payload];
    new_attr = &attrs[new_record.payload];
    if (data->namespace_id < LXA_DOM_NS_HTML
        || data->namespace_id > LXA_DOM_NS_SVG
        || old_attr->owner != host_id
        || new_attr->owner != LXA_ID_NONE || new_attr->next != LXA_ID_NONE
        || lxa_dom_nodes_attribute_read(nodes, old_attribute, &old_name, &value)
           != LXA_OK
        || lxa_dom_nodes_attribute_read(nodes, new_attribute, &new_name, &value)
           != LXA_OK) {
        return LXA_ERROR_ARGUMENT;
    }
    status = scan_attributes(nodes, host_id, *data, old_name, &match, &tail,
                              &predecessor);
    if (status != LXA_OK || match != old_id) {
        return status == LXA_OK ? LXA_ERROR_ARGUMENT : status;
    }
    status = scan_attributes(nodes, host_id, *data, new_name, &match, &tail,
                              NULL);
    if (status != LXA_OK || (match != LXA_ID_NONE && match != old_id)) {
        return status == LXA_OK ? LXA_ERROR_ARGUMENT : status;
    }
    status = option_selected_attribute(nodes, element, host, old_name,
                                       old_attr->namespace_id,
                                       &deselect_option);
    if (status == LXA_OK)
        status = option_selected_attribute(nodes, element, host, new_name,
                                           new_attr->namespace_id,
                                           &select_option);
    if (status != LXA_OK) return status;
    /* Both scans complete before changing ownership or links. */
    new_attr->owner = host_id;
    new_attr->next = old_attr->next;
    if (predecessor == LXA_ID_NONE) {
        data->first_attr = new_id;
    }
    else {
        attrs[((lxa_dom_record_t *) nodes->records.bytes.data)[predecessor].payload]
            .next = new_id;
    }
    old_attr->owner = old_attr->next = LXA_ID_NONE;
    if (select_option != deselect_option) {
        lxa_dom_record_t *record =
            (lxa_dom_record_t *) nodes->records.bytes.data + host_id;
        if (select_option) record->flags |= LXA_DOM_OPTION_SELECTED;
        else record->flags &= (uint16_t) ~LXA_DOM_OPTION_SELECTED;
    }
    return LXA_OK;
}

lxa_status_t
lxa_dom_nodes_find_attribute(lxa_dom_nodes_t *nodes, lxa_dom_ref_t element,
    lxa_span_t name, lxa_dom_ref_t *out)
{
    lxa_dom_record_t host;
    const lxa_dom_element_t *elements;
    lxa_id_t match, tail;
    lxa_status_t status;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (nodes == NULL || (name.data == NULL && name.length != 0)) {
        return LXA_ERROR_ARGUMENT;
    }
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_dom_nodes_read(nodes, element, &host);
    if (status != LXA_OK || host.kind != LXA_DOM_KIND_ELEMENT
        || host.payload >= lxa_vector_length(&nodes->elements)) {
        return LXA_ERROR_ARGUMENT;
    }
    elements = (const lxa_dom_element_t *) nodes->elements.bytes.data;
    status = scan_attributes(nodes, (lxa_id_t) element.handle,
                             elements[host.payload], name, &match, &tail, NULL);
    if (status != LXA_OK || match == LXA_ID_NONE) {
        return status;
    }
    out->document = nodes;
    out->epoch = nodes->epoch;
    return lxa_handle_pack(match, nodes->generation, &out->handle);
}

lxa_status_t
lxa_dom_nodes_attribute_next(lxa_dom_nodes_t *nodes, lxa_dom_ref_t element,
    lxa_dom_ref_t previous, lxa_dom_ref_t *out)
{
    lxa_dom_record_t host, entry;
    const lxa_dom_element_t *elements;
    const lxa_dom_attr_t *attrs;
    lxa_id_t id, owner;
    lxa_status_t status;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    status = lxa_dom_nodes_read(nodes, element, &host);
    if (status != LXA_OK || host.kind != LXA_DOM_KIND_ELEMENT
        || host.payload >= lxa_vector_length(&nodes->elements)) {
        return LXA_ERROR_ARGUMENT;
    }
    nodes->limit_reason = LXA_BUDGET_COUNT;
    elements = (const lxa_dom_element_t *) nodes->elements.bytes.data;
    attrs = (const lxa_dom_attr_t *) nodes->attributes.bytes.data;
    owner = (lxa_id_t) element.handle;
    if (previous.handle == LXA_HANDLE_NONE) {
        id = elements[host.payload].first_attr;
        if ((id == LXA_ID_NONE) != (elements[host.payload].attr_count == 0)) {
            return LXA_ERROR_ARGUMENT;
        }
    }
    else {
        status = lxa_dom_nodes_read(nodes, previous, &entry);
        if (status != LXA_OK || entry.kind != LXA_DOM_KIND_ATTRIBUTE
            || entry.payload >= lxa_vector_length(&nodes->attributes)
            || attrs[entry.payload].owner != owner) {
            return LXA_ERROR_ARGUMENT;
        }
        id = attrs[entry.payload].next;
    }
    if (id == LXA_ID_NONE) {
        return LXA_OK;
    }
    status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                &nodes->limit_reason);
    if (status != LXA_OK) {
        return status;
    }
    if (id < nodes->retired || id >= lxa_vector_length(&nodes->records)) {
        return LXA_ERROR_ARGUMENT;
    }
    entry = ((const lxa_dom_record_t *) nodes->records.bytes.data)[id];
    if (entry.kind != LXA_DOM_KIND_ATTRIBUTE
        || entry.payload >= lxa_vector_length(&nodes->attributes)
        || attrs[entry.payload].owner != owner) {
        return LXA_ERROR_ARGUMENT;
    }
    out->document = nodes;
    out->epoch = nodes->epoch;
    return lxa_handle_pack(id, nodes->generation, &out->handle);
}

typedef enum {
    SELECT_OTHER, SELECT_SELECT, SELECT_OPTION, SELECT_OPTGROUP,
    SELECT_DATALIST, SELECT_HR, SELECT_LEAF, SELECT_SELECTEDCONTENT
} select_tag_t;

static lxa_status_t
select_tag(lxa_dom_nodes_t *nodes, lxa_dom_ref_t ref, select_tag_t *out)
{
    lxa_dom_record_t record;
    lxa_dom_namespace_t ns;
    lxa_span_t tag;
    lxa_status_t status = lxa_dom_nodes_read(nodes, ref, &record);
    *out = SELECT_OTHER;
    if (status != LXA_OK) return status;
    if (record.kind != LXA_DOM_KIND_ELEMENT) {
        *out = SELECT_LEAF;
        return LXA_OK;
    }
    status = lxa_dom_nodes_element_namespace(nodes, ref, &ns);
    if (status != LXA_OK || ns != LXA_DOM_NS_HTML) return status;
    status = lxa_dom_nodes_element_name(nodes, ref, &tag);
    if (status == LXA_OK)
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, tag.length,
                                   &nodes->limit_reason);
    if (status != LXA_OK) return status;
    if (tag.length == 6 && memcmp(tag.data, "select", 6) == 0)
        *out = SELECT_SELECT;
    else if (tag.length == 6 && memcmp(tag.data, "option", 6) == 0)
        *out = SELECT_OPTION;
    else if (tag.length == 8 && memcmp(tag.data, "optgroup", 8) == 0)
        *out = SELECT_OPTGROUP;
    else if (tag.length == 8 && memcmp(tag.data, "datalist", 8) == 0)
        *out = SELECT_DATALIST;
    else if (tag.length == 2 && memcmp(tag.data, "hr", 2) == 0)
        *out = SELECT_HR;
    else if (tag.length == 15 && memcmp(tag.data, "selectedcontent", 15) == 0)
        *out = SELECT_SELECTEDCONTENT;
    return LXA_OK;
}

static lxa_status_t
selectedcontent_context(lxa_dom_nodes_t *nodes, lxa_dom_ref_t parent,
                          bool *disabled, lxa_dom_ref_t *select)
{
    size_t steps = 0, length;
    lxa_status_t status;
    *disabled = false;
    *select = (lxa_dom_ref_t){0};
    length = lxa_vector_length(&nodes->records);
    while (parent.handle != LXA_HANDLE_NONE) {
        select_tag_t tag;
        if (++steps > length) return LXA_ERROR_ARGUMENT;
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                   &nodes->limit_reason);
        if (status == LXA_OK) status = select_tag(nodes, parent, &tag);
        if (status != LXA_OK) return status;
        if (tag == SELECT_SELECT) {
            if (select->handle != LXA_HANDLE_NONE) {
                *disabled = true;
                return LXA_OK;
            }
            *select = parent;
        }
        if (tag == SELECT_OPTION || tag == SELECT_SELECTEDCONTENT) {
            *disabled = true;
            return LXA_OK;
        }
        status = lxa_dom_nodes_parent(nodes, parent, &parent);
        if (status != LXA_OK) return status;
    }
    return LXA_OK;
}

lxa_status_t
lxa_dom_nodes_prepare_selectedcontent(lxa_dom_nodes_t *nodes,
                                      lxa_dom_ref_t parent, bool *disabled)
{
    lxa_dom_ref_t select, attribute;
    lxa_status_t status;
    if (nodes == NULL || disabled == NULL) return LXA_ERROR_ARGUMENT;
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = selectedcontent_context(nodes, parent, disabled, &select);
    if (status != LXA_OK || *disabled || select.handle == LXA_HANDLE_NONE)
        return status;
    status = lxa_dom_nodes_find_attribute(nodes, select,
        (lxa_span_t){(const uint8_t *) "multiple", 8}, &attribute);
    if (status != LXA_OK) return status;
    return attribute.handle == LXA_HANDLE_NONE ? LXA_ERROR_UNSUPPORTED : LXA_OK;
}

lxa_status_t
lxa_dom_nodes_connect_selectedcontent(lxa_dom_nodes_t *nodes,
                                      lxa_dom_ref_t element)
{
    lxa_dom_ref_t parent, select, attribute = {0};
    lxa_dom_record_t record;
    select_tag_t tag;
    bool disabled;
    lxa_status_t status;
    if (nodes == NULL) return LXA_ERROR_ARGUMENT;
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = select_tag(nodes, element, &tag);
    if (status != LXA_OK || tag != SELECT_SELECTEDCONTENT)
        return status == LXA_OK ? LXA_ERROR_ARGUMENT : status;
    status = lxa_dom_nodes_read(nodes, element, &record);
    if (status == LXA_OK
        && (record.flags & ~LXA_DOM_SELECTEDCONTENT_DISABLED) != 0)
        return LXA_ERROR_ARGUMENT;
    if (status == LXA_OK) status = lxa_dom_nodes_parent(nodes, element, &parent);
    if (status == LXA_OK)
        status = selectedcontent_context(nodes, parent, &disabled, &select);
    if (status == LXA_OK && !disabled && select.handle != LXA_HANDLE_NONE)
        status = lxa_dom_nodes_find_attribute(nodes, select,
            (lxa_span_t){(const uint8_t *) "multiple", 8}, &attribute);
    if (status != LXA_OK) return status;
    ((lxa_dom_record_t *) nodes->records.bytes.data)[(lxa_id_t) element.handle].flags =
        disabled ? LXA_DOM_SELECTEDCONTENT_DISABLED : 0;
    if (disabled || select.handle == LXA_HANDLE_NONE
        || attribute.handle != LXA_HANDLE_NONE) return LXA_OK;
    status = lxa_dom_nodes_update_selectedcontent(nodes, select);
    if (status == LXA_OK)
        status = lxa_dom_nodes_clear_nonprimary_selectedcontent(nodes, select);
    return status;
}

static lxa_status_t
next_descendant(lxa_dom_nodes_t *nodes, lxa_dom_ref_t root,
                  lxa_dom_ref_t current, bool descend, size_t *ascents,
                  lxa_dom_ref_t *out)
{
    size_t length = lxa_vector_length(&nodes->records);
    lxa_status_t status;
    if (descend) {
        status = lxa_dom_nodes_first_child(nodes, current, out);
        if (status != LXA_OK || out->handle != LXA_HANDLE_NONE) return status;
    }
    for (;;) {
        if (++*ascents > length) return LXA_ERROR_ARGUMENT;
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                   &nodes->limit_reason);
        if (status == LXA_OK) status = lxa_dom_nodes_next_sibling(nodes, current, out);
        if (status != LXA_OK || out->handle != LXA_HANDLE_NONE) return status;
        status = lxa_dom_nodes_parent(nodes, current, &current);
        if (status != LXA_OK) return status;
        if (current.handle == root.handle) {
            *out = (lxa_dom_ref_t){0};
            return LXA_OK;
        }
        if (current.handle == LXA_HANDLE_NONE) return LXA_ERROR_ARGUMENT;
    }
}

static lxa_status_t
first_selectedcontent(lxa_dom_nodes_t *nodes, lxa_dom_ref_t select,
                        lxa_handle_t excluded, lxa_dom_ref_t *out)
{
    lxa_dom_ref_t cursor;
    size_t visits = 0, ascents = 0, length = lxa_vector_length(&nodes->records);
    lxa_status_t status = lxa_dom_nodes_first_child(nodes, select, &cursor);
    *out = (lxa_dom_ref_t){0};
    while (status == LXA_OK && cursor.handle != LXA_HANDLE_NONE) {
        select_tag_t tag;
        if (++visits > length) return LXA_ERROR_ARGUMENT;
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                   &nodes->limit_reason);
        if (status == LXA_OK) status = select_tag(nodes, cursor, &tag);
        if (status != LXA_OK) return status;
        if (tag == SELECT_SELECTEDCONTENT && cursor.handle != excluded) {
            *out = cursor;
            return LXA_OK;
        }
        status = next_descendant(nodes, select, cursor,
                                   tag != SELECT_LEAF && cursor.handle != excluded,
                                   &ascents, &cursor);
    }
    return status;
}

static lxa_status_t
enabled_selectedcontent(lxa_dom_nodes_t *nodes, lxa_dom_ref_t select,
                          lxa_handle_t excluded, lxa_dom_ref_t *out)
{
    lxa_dom_ref_t attribute;
    lxa_dom_record_t record;
    lxa_status_t status;
    *out = (lxa_dom_ref_t){0};
    status = lxa_dom_nodes_find_attribute(nodes, select,
        (lxa_span_t){(const uint8_t *) "multiple", 8}, &attribute);
    if (status != LXA_OK || attribute.handle != LXA_HANDLE_NONE) return status;
    status = first_selectedcontent(nodes, select, excluded, out);
    if (status != LXA_OK || out->handle == LXA_HANDLE_NONE) return status;
    status = lxa_dom_nodes_read(nodes, *out, &record);
    if (status != LXA_OK) return status;
    if ((record.flags & ~LXA_DOM_SELECTEDCONTENT_DISABLED) != 0)
        return LXA_ERROR_ARGUMENT;
    if ((record.flags & LXA_DOM_SELECTEDCONTENT_DISABLED) != 0)
        *out = (lxa_dom_ref_t){0};
    return LXA_OK;
}

static lxa_status_t
disable_cloned_selectedcontent(lxa_dom_nodes_t *nodes, lxa_dom_ref_t staging)
{
    lxa_dom_ref_t cursor;
    size_t visits = 0, ascents = 0, length = lxa_vector_length(&nodes->records);
    lxa_status_t status = lxa_dom_nodes_first_child(nodes, staging, &cursor);
    while (status == LXA_OK && cursor.handle != LXA_HANDLE_NONE) {
        select_tag_t tag;
        if (++visits > length) return LXA_ERROR_ARGUMENT;
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                   &nodes->limit_reason);
        if (status == LXA_OK) status = select_tag(nodes, cursor, &tag);
        if (status != LXA_OK) return status;
        if (tag == SELECT_SELECTEDCONTENT)
            ((lxa_dom_record_t *) nodes->records.bytes.data)[(lxa_id_t) cursor.handle].flags =
                LXA_DOM_SELECTEDCONTENT_DISABLED;
        status = next_descendant(nodes, staging, cursor, tag != SELECT_LEAF,
                                   &ascents, &cursor);
    }
    return status;
}

static lxa_status_t
html_option(lxa_dom_nodes_t *nodes, lxa_dom_ref_t element, bool *out)
{
    lxa_dom_namespace_t ns;
    lxa_span_t name = {0};
    lxa_status_t status = lxa_dom_nodes_element_namespace(nodes, element, &ns);
    *out = false;
    if (status == LXA_OK && ns == LXA_DOM_NS_HTML)
        status = lxa_dom_nodes_element_name(nodes, element, &name);
    if (status == LXA_OK && name.length == 6) {
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 6,
                                   &nodes->limit_reason);
        if (status == LXA_OK) *out = memcmp(name.data, "option", 6) == 0;
    }
    return status;
}

static bool
select_display_size_one(lxa_span_t value)
{
    size_t i = 0;
    unsigned size = 0;
    bool negative = false;
    while (i < value.length
           && (value.data[i] == ' ' || value.data[i] == '\t'
               || value.data[i] == '\n' || value.data[i] == '\r'
               || value.data[i] == '\f'))
        ++i;
    if (i < value.length && (value.data[i] == '+' || value.data[i] == '-'))
        negative = value.data[i++] == '-';
    if (i == value.length || value.data[i] < '0' || value.data[i] > '9')
        return true; /* Invalid size uses the single-select default. */
    while (i < value.length && value.data[i] >= '0' && value.data[i] <= '9') {
        unsigned digit = value.data[i++] - '0';
        if (size < 2) {
            size = size * 10 + digit;
            if (size > 2) size = 2;
        }
    }
    return negative && size != 0 ? true : size == 1;
}

typedef struct {
    lxa_vector_t options;
    lxa_id_t choice;
} select_reset_plan_t;

typedef enum {
    SELECT_MOVE, SELECT_REPLACE_CHILDREN, SELECT_REMOVE_PREFIX, SELECT_UNCHANGED
} select_view_kind_t;

typedef struct {
    lxa_dom_ref_t subtree, parent, before;
    lxa_dom_ref_t staging_parent;
    lxa_dom_ref_t selection_owner;
    select_view_kind_t kind;
    bool prior_collapsed;
    lxa_id_t prior_choice;
    size_t original_length;
    lxa_handle_t excluded;
    bool final_collapsed, final_chosen;
    lxa_id_t final_choice;
} select_view_t;

typedef struct {
    lxa_dom_ref_t target, staging;
    lxa_dom_record_t host, candidate;
    lxa_vector_t removals;
    select_reset_plan_t reset;
    select_view_t result;
} child_replace_plan_t;

typedef struct {
    bool reset_before;
    select_reset_plan_t reset;
    child_replace_plan_t replacement;
} selectedcontent_update_plan_t;

static lxa_status_t prepare_child_replacement(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t target, lxa_dom_ref_t source, bool selectedcontent_copy,
    const select_view_t *initial, child_replace_plan_t *plan);
static void commit_child_replacement(lxa_dom_nodes_t *nodes,
    const child_replace_plan_t *plan);
static void destroy_child_replacement(child_replace_plan_t *plan);
static lxa_status_t nearest_select(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t ancestor, const select_view_t *view, lxa_dom_ref_t *out);

/* Read the proposed placement without publishing any link changes. */
static lxa_status_t
select_parent(lxa_dom_nodes_t *nodes, lxa_dom_ref_t child,
                const select_view_t *move, lxa_dom_ref_t *out)
{
    if (move != NULL && move->excluded != LXA_HANDLE_NONE
        && child.handle == move->excluded) {
        *out = (lxa_dom_ref_t){0};
        return LXA_OK;
    }
    if (move != NULL && move->kind == SELECT_REPLACE_CHILDREN) {
        lxa_status_t status = lxa_dom_nodes_parent(nodes, child, out);
        if (status == LXA_OK && move->subtree.handle != LXA_HANDLE_NONE
            && out->handle == move->subtree.handle)
            *out = move->parent;
        return status;
    }
    if (move != NULL && move->kind == SELECT_REMOVE_PREFIX) {
        lxa_status_t status = lxa_dom_nodes_parent(nodes, child, out);
        if (status == LXA_OK && move->staging_parent.handle != LXA_HANDLE_NONE
            && out->handle == move->staging_parent.handle)
            *out = move->parent;
        return status;
    }
    if (move != NULL && move->kind == SELECT_MOVE
        && child.handle == move->subtree.handle) {
        *out = move->parent;
        return LXA_OK;
    }
    return lxa_dom_nodes_parent(nodes, child, out);
}

static lxa_status_t
select_first_child(lxa_dom_nodes_t *nodes, lxa_dom_ref_t parent,
                     const select_view_t *move, lxa_dom_ref_t *out)
{
    lxa_status_t status;
    if (move != NULL && move->kind == SELECT_REMOVE_PREFIX
        && parent.handle == move->parent.handle) {
        *out = move->subtree;
        status = LXA_OK;
    }
    else if (move != NULL && move->kind == SELECT_REPLACE_CHILDREN
        && parent.handle == move->parent.handle) {
        *out = (lxa_dom_ref_t){0};
        status = move->subtree.handle == LXA_HANDLE_NONE ? LXA_OK
            : lxa_dom_nodes_first_child(nodes, move->subtree, out);
    }
    else {
        status = lxa_dom_nodes_first_child(nodes, parent, out);
        if (status == LXA_OK && move != NULL && move->kind == SELECT_MOVE) {
            if (out->handle == move->subtree.handle)
                status = lxa_dom_nodes_next_sibling(nodes, move->subtree, out);
            if (status == LXA_OK && parent.handle == move->parent.handle
                && out->handle == move->before.handle)
                *out = move->subtree;
        }
    }
    if (status == LXA_OK && move != NULL && move->excluded != LXA_HANDLE_NONE
        && out->handle == move->excluded)
        status = lxa_dom_nodes_next_sibling(nodes, *out, out);
    return status;
}

static lxa_status_t
select_next_sibling(lxa_dom_nodes_t *nodes, lxa_dom_ref_t sibling,
                      const select_view_t *move, lxa_dom_ref_t *out)
{
    lxa_dom_ref_t parent;
    lxa_status_t status;
    if (move != NULL && move->kind == SELECT_MOVE
        && sibling.handle == move->subtree.handle) {
        *out = move->before;
        return LXA_OK;
    }
    status = lxa_dom_nodes_next_sibling(nodes, sibling, out);
    if (status == LXA_OK && move != NULL && move->kind == SELECT_MOVE) {
        if (out->handle == move->subtree.handle)
            status = lxa_dom_nodes_next_sibling(nodes, move->subtree, out);
        if (status == LXA_OK && out->handle == move->before.handle) {
            status = lxa_dom_nodes_parent(nodes, sibling, &parent);
            if (status == LXA_OK && parent.handle == move->parent.handle)
                *out = move->subtree;
        }
    }
    if (status == LXA_OK && move != NULL && move->excluded != LXA_HANDLE_NONE
        && out->handle == move->excluded)
        status = lxa_dom_nodes_next_sibling(nodes, *out, out);
    return status;
}

static lxa_status_t
option_select_at(lxa_dom_nodes_t *nodes, lxa_dom_ref_t parent,
                   const select_view_t *move, lxa_dom_ref_t *out)
{
    lxa_dom_ref_t select = parent;
    size_t length = lxa_vector_length(&nodes->records), steps = 0;
    bool grouped = false;
    lxa_status_t status;
    *out = (lxa_dom_ref_t){0};
    while (select.handle != LXA_HANDLE_NONE) {
        select_tag_t tag;
        if (++steps > length) return LXA_ERROR_ARGUMENT;
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                   &nodes->limit_reason);
        if (status == LXA_OK) status = select_tag(nodes, select, &tag);
        if (status != LXA_OK) return status;
        if (tag == SELECT_SELECT) {
            *out = select;
            return LXA_OK;
        }
        if (tag == SELECT_OPTION || tag == SELECT_HR || tag == SELECT_DATALIST
            || (tag == SELECT_OPTGROUP && grouped))
            return LXA_OK;
        if (tag == SELECT_OPTGROUP) grouped = true;
        status = select_parent(nodes, select, move, &select);
        if (status != LXA_OK) return status;
    }
    return LXA_OK;
}

static lxa_status_t
option_select(lxa_dom_nodes_t *nodes, lxa_dom_ref_t option,
                const select_view_t *move, lxa_dom_ref_t *out)
{
    lxa_dom_ref_t parent;
    lxa_status_t status = select_parent(nodes, option, move, &parent);
    if (status != LXA_OK) return status;
    return option_select_at(nodes, parent, move, out);
}

typedef enum {
    SELECT_SCAN_RESET, SELECT_SCAN_EXCLUSION, SELECT_SCAN_FIRST_SELECTED
} select_scan_kind_t;

typedef struct {
    select_scan_kind_t kind;
    bool found_excluded;
    select_reset_plan_t *plan;
    lxa_dom_ref_t last, first;
    size_t selected;
    bool size_one;
} select_reset_scan_t;

static bool
option_selected(const lxa_dom_record_t *record, lxa_id_t id,
                  const select_view_t *view)
{
    bool selected = (record->flags & LXA_DOM_OPTION_SELECTED) != 0;
    if (view == NULL) return selected;
    if (id < view->original_length) {
        /* Project preceding removals onto retained options, not new clones. */
        if (view->prior_collapsed) selected = false;
        if (id == view->prior_choice) selected = true;
    }
    if (view->final_collapsed) selected = false;
    if (view->final_chosen && id == view->final_choice)
        selected = true;
    return selected;
}

static lxa_status_t
select_scan_option(lxa_dom_nodes_t *nodes, lxa_dom_ref_t option,
                    bool disabled_group, const select_view_t *view,
                    select_reset_scan_t *scan)
{
    lxa_dom_record_t record;
    lxa_dom_ref_t attribute;
    lxa_id_t id = (lxa_id_t) option.handle;
    lxa_status_t status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                           &nodes->limit_reason);
    if (status == LXA_OK) status = lxa_dom_nodes_read(nodes, option, &record);
    if (status != LXA_OK) return status;
    if (option_selected(&record, id, view)) {
        if (scan->selected != 0) {
            if (scan->selected == 1) {
                lxa_id_t previous = (lxa_id_t) scan->last.handle;
                status = lxa_vector_push(&scan->plan->options, &previous);
            }
            if (status == LXA_OK)
                status = lxa_vector_push(&scan->plan->options, &id);
            if (status != LXA_OK) return status;
        }
        if (scan->selected < 2) ++scan->selected;
        scan->last = option;
    }
    if (scan->size_one && scan->first.handle == LXA_HANDLE_NONE && !disabled_group) {
        status = lxa_dom_nodes_find_attribute(nodes, option,
            (lxa_span_t){(const uint8_t *) "disabled", 8}, &attribute);
        if (status != LXA_OK) return status;
        if (attribute.handle == LXA_HANDLE_NONE) scan->first = option;
    }
    return LXA_OK;
}

static lxa_status_t
scan_select_options(lxa_dom_nodes_t *nodes, lxa_dom_ref_t select,
                      lxa_handle_t excluded, const select_view_t *move,
                      select_reset_scan_t *scan)
{
    lxa_dom_ref_t cursor, group = {0}, attribute;
    select_tag_t tag;
    size_t length = lxa_vector_length(&nodes->records);
    size_t steps = 0, ascents = 0;
    bool disabled_group = false, excluding = false;
    lxa_status_t status = select_first_child(nodes, select, move, &cursor);
    while (status == LXA_OK && cursor.handle != LXA_HANDLE_NONE) {
        lxa_dom_ref_t next;
        bool descend;
        if (++steps > length) { status = LXA_ERROR_ARGUMENT; break; }
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                   &nodes->limit_reason);
        if (status == LXA_OK) status = select_tag(nodes, cursor, &tag);
        if (status != LXA_OK) break;
        if (cursor.handle == excluded) excluding = true;
        descend = (scan->kind == SELECT_SCAN_EXCLUSION || cursor.handle != excluded)
            && tag != SELECT_LEAF && tag != SELECT_SELECT && tag != SELECT_OPTION
            && tag != SELECT_HR && tag != SELECT_DATALIST
            && !(tag == SELECT_OPTGROUP && group.handle != LXA_HANDLE_NONE);
        if (tag == SELECT_OPTION) {
            if (scan->kind == SELECT_SCAN_FIRST_SELECTED) {
                lxa_dom_record_t record;
                status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                           &nodes->limit_reason);
                if (status == LXA_OK) status = lxa_dom_nodes_read(nodes, cursor, &record);
                if (status != LXA_OK) break;
                if ((record.flags & ~(LXA_DOM_OPTION_SELECTED | LXA_DOM_OPTION_DIRTY)) != 0)
                    return LXA_ERROR_ARGUMENT;
                if (option_selected(&record, (lxa_id_t) cursor.handle, move)) {
                    scan->first = cursor;
                    return LXA_OK;
                }
            }
            if (scan->kind == SELECT_SCAN_EXCLUSION && excluding) {
                scan->found_excluded = true;
                return LXA_OK;
            }
            if (scan->kind == SELECT_SCAN_RESET && !excluding) {
                status = select_scan_option(nodes, cursor, disabled_group, move, scan);
                if (status != LXA_OK) break;
            }
        }
        if (descend && tag == SELECT_OPTGROUP) {
            group = cursor;
            if (scan->kind == SELECT_SCAN_RESET) {
                status = lxa_dom_nodes_find_attribute(nodes, group,
                    (lxa_span_t){(const uint8_t *) "disabled", 8}, &attribute);
                if (status != LXA_OK) break;
                disabled_group = attribute.handle != LXA_HANDLE_NONE;
            }
        }
        if (descend) {
            status = select_first_child(nodes, cursor, move, &next);
            if (status != LXA_OK) break;
            if (next.handle != LXA_HANDLE_NONE) {
                cursor = next;
                continue;
            }
        }
        for (;;) {
            if (++ascents > length) { status = LXA_ERROR_ARGUMENT; break; }
            status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                       &nodes->limit_reason);
            if (status != LXA_OK) break;
            if (cursor.handle == excluded) {
                if (scan->kind == SELECT_SCAN_EXCLUSION) return LXA_OK;
                excluding = false;
            }
            if (cursor.handle == group.handle) {
                group = (lxa_dom_ref_t){0};
                disabled_group = false;
            }
            status = select_next_sibling(nodes, cursor, move, &next);
            if (status != LXA_OK) break;
            if (next.handle != LXA_HANDLE_NONE) {
                cursor = next;
                break;
            }
            status = select_parent(nodes, cursor, move, &cursor);
            if (status != LXA_OK) break;
            if (cursor.handle == select.handle) {
                cursor = (lxa_dom_ref_t){0};
                break;
            }
            if (cursor.handle == LXA_HANDLE_NONE) {
                status = LXA_ERROR_ARGUMENT;
                break;
            }
        }
    }
    return status;
}

static lxa_status_t
plan_select_reset(lxa_dom_nodes_t *nodes, lxa_dom_ref_t select,
                   lxa_handle_t excluded, const select_view_t *move,
                   select_reset_plan_t *plan)
{
    lxa_dom_ref_t attribute;
    select_reset_scan_t scan = {.plan = plan, .size_one = true};
    lxa_status_t status;
    plan->choice = LXA_ID_NONE;
    status = lxa_vector_init(&plan->options, &nodes->allocator, sizeof(lxa_id_t));
    if (status == LXA_OK)
        status = lxa_vector_bind_budget(&plan->options, nodes->budget);
    if (status != LXA_OK) return status;
    status = lxa_dom_nodes_find_attribute(nodes, select,
        (lxa_span_t){(const uint8_t *) "multiple", 8}, &attribute);
    if (status != LXA_OK || attribute.handle != LXA_HANDLE_NONE) return status;
    status = lxa_dom_nodes_find_attribute(nodes, select,
        (lxa_span_t){(const uint8_t *) "size", 4}, &attribute);
    if (status == LXA_OK && attribute.handle != LXA_HANDLE_NONE) {
        lxa_span_t name, value;
        status = lxa_dom_nodes_attribute_read(nodes, attribute, &name, &value);
        if (status == LXA_OK)
            status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK,
                                       value.length, &nodes->limit_reason);
        if (status == LXA_OK) scan.size_one = select_display_size_one(value);
    }
    if (status != LXA_OK) return status;
    status = scan_select_options(nodes, select, excluded, move, &scan);
    if (status == LXA_OK) {
        if (scan.selected > 1) plan->choice = (lxa_id_t) scan.last.handle;
        else if (scan.selected == 0 && scan.size_one
                 && scan.first.handle != LXA_HANDLE_NONE)
            plan->choice = (lxa_id_t) scan.first.handle;
    }
    if (status == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_COUNT)
        nodes->limit_reason = LXA_BUDGET_MEMORY;
    return status;
}

static void
commit_select_reset(lxa_dom_nodes_t *nodes, const select_reset_plan_t *plan)
{
    lxa_dom_record_t *records = (lxa_dom_record_t *) nodes->records.bytes.data;
    if (plan->options.bytes.length != 0) {
        const lxa_id_t *ids = (const lxa_id_t *) plan->options.bytes.data;
        for (size_t i = 0; i < lxa_vector_length(&plan->options); ++i) {
            if (ids[i] != plan->choice)
                records[ids[i]].flags &= (uint16_t) ~LXA_DOM_OPTION_SELECTED;
        }
    }
    if (plan->choice != LXA_ID_NONE)
        records[plan->choice].flags |= LXA_DOM_OPTION_SELECTED;
}

lxa_status_t
lxa_dom_nodes_reset_option_select(lxa_dom_nodes_t *nodes, lxa_dom_ref_t option)
{
    lxa_dom_ref_t select;
    select_reset_plan_t plan = {.choice = LXA_ID_NONE};
    select_tag_t tag;
    lxa_status_t status;
    if (nodes == NULL) return LXA_ERROR_ARGUMENT;
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = select_tag(nodes, option, &tag);
    if (status != LXA_OK || tag != SELECT_OPTION)
        return status == LXA_OK ? LXA_ERROR_ARGUMENT : status;
    status = option_select(nodes, option, NULL, &select);
    if (status != LXA_OK || select.handle == LXA_HANDLE_NONE) return status;
    status = plan_select_reset(nodes, select, LXA_HANDLE_NONE, NULL, &plan);
    if (status == LXA_OK) commit_select_reset(nodes, &plan);
    lxa_vector_destroy(&plan.options);
    return status;
}

lxa_status_t
lxa_dom_nodes_clone_element(lxa_dom_nodes_t *nodes, lxa_dom_ref_t source,
                            lxa_dom_ref_t *out)
{
    lxa_dom_ref_t clone, cursor = {0};
    lxa_dom_record_t record;
    lxa_dom_namespace_t ns;
    lxa_dom_element_t element;
    lxa_span_t name;
    lxa_id_t match, tail;
    lxa_status_t status;
    if (out == NULL) return LXA_ERROR_ARGUMENT;
    memset(out, 0, sizeof(*out));
    status = lxa_dom_nodes_element_namespace(nodes, source, &ns);
    if (status == LXA_OK)
        status = lxa_dom_nodes_element_name(nodes, source, &name);
    if (status == LXA_OK)
        status = lxa_dom_nodes_read(nodes, source, &record);
    if (status != LXA_OK) return status;
    element = ((const lxa_dom_element_t *)
        nodes->elements.bytes.data)[record.payload];
    status = scan_attributes(nodes, (lxa_id_t) source.handle, element,
                              (lxa_span_t){0}, &match, &tail, NULL);
    if (status == LXA_OK)
        status = lxa_dom_nodes_append_namespaced_element(nodes, name, ns, &clone);
    for (size_t i = 0; status == LXA_OK && i < element.attr_count; ++i) {
        lxa_dom_ref_t attribute;
        lxa_dom_attr_namespace_t attr_ns;
        lxa_span_t value, local_name;
        status = lxa_dom_nodes_attribute_next(nodes, source, cursor, &cursor);
        if (status == LXA_OK)
            status = lxa_dom_nodes_attribute_namespace(nodes, cursor,
                                                       &attr_ns, &local_name);
        if (status == LXA_OK)
            status = lxa_dom_nodes_attribute_read(nodes, cursor, &name, &value);
        if (status == LXA_OK)
            status = lxa_dom_nodes_append_foreign_attribute(nodes, name, value,
                                                             attr_ns, &attribute);
        if (status == LXA_OK)
            status = lxa_dom_nodes_attach_attribute(nodes, clone, attribute);
    }
    if (status == LXA_OK) *out = clone;
    return status;
}

/* The sole allocation-free link commit for constructed children.
 * Callers finish hierarchy and budget checks before entry.
 */
static void
commit_append(lxa_dom_nodes_t *nodes, lxa_id_t parent, lxa_id_t child,
              lxa_id_t old_tail)
{
    lxa_dom_record_t *records = (lxa_dom_record_t *) nodes->records.bytes.data;
    records[child].parent = parent;
    records[child].prev = old_tail;
    if (old_tail == LXA_ID_NONE) {
        records[parent].first_child = child;
    }
    else {
        records[old_tail].next = child;
    }
    records[parent].last_child = child;
}

static void
commit_insert(lxa_dom_nodes_t *nodes, lxa_id_t parent, lxa_id_t child,
              lxa_id_t successor)
{
    lxa_dom_record_t *records = (lxa_dom_record_t *) nodes->records.bytes.data;
    lxa_id_t predecessor = records[successor].prev;
    records[child].parent = parent;
    records[child].next = successor;
    records[child].prev = predecessor;
    records[successor].prev = child;
    if (predecessor == LXA_ID_NONE) records[parent].first_child = child;
    else records[predecessor].next = child;
}

lxa_status_t
lxa_dom_nodes_append_text_child(lxa_dom_nodes_t *nodes,
                                lxa_dom_ref_t parent,
                                lxa_dom_ref_t child)
{
    lxa_dom_record_t parent_copy, child_copy;
    lxa_dom_record_t *records, *tail;
    lxa_id_t parent_id, child_id;
    lxa_status_t status;
    if (nodes == NULL || parent.handle == child.handle) {
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_dom_nodes_read(nodes, parent, &parent_copy);
    if (status == LXA_OK) {
        status = lxa_dom_nodes_read(nodes, child, &child_copy);
    }
    if (status != LXA_OK) {
        return status;
    }
    /* read verified both packed handles and their document/epoch. */
    parent_id = (lxa_id_t) parent.handle;
    child_id = (lxa_id_t) child.handle;
    if (parent_copy.kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT
        || (child_copy.kind != LXA_DOM_KIND_TEXT
            && child_copy.kind != LXA_DOM_KIND_COMMENT)
        || child_copy.payload >= lxa_vector_length(&nodes->characters)
        || child_copy.parent != LXA_ID_NONE || child_copy.prev != LXA_ID_NONE
        || child_copy.next != LXA_ID_NONE
        || (parent_copy.first_child == LXA_ID_NONE)
           != (parent_copy.last_child == LXA_ID_NONE)) {
        return LXA_ERROR_ARGUMENT;
    }
    /* Only fragment -> character is supported here: its child depth is one.
     * Do not hold a live-stack charge on a persistent tree link. */
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_budget_check(nodes->budget, LXA_BUDGET_TREE_DEPTH, 1,
                              &nodes->limit_reason);
    if (status != LXA_OK) {
        return status;
    }
    records = (lxa_dom_record_t *) nodes->records.bytes.data;
    tail = NULL;
    if (parent_copy.last_child != LXA_ID_NONE) {
        if (parent_copy.last_child < nodes->retired
            || parent_copy.last_child >= lxa_vector_length(&nodes->records)
            || parent_copy.last_child == child_id) {
            return LXA_ERROR_ARGUMENT;
        }
        tail = &records[parent_copy.last_child];
        if (tail->next != LXA_ID_NONE || tail->parent != parent_id) {
            return LXA_ERROR_ARGUMENT;
        }
    }
    /* All validation is complete. No allocation occurs below. */
    commit_append(nodes, parent_id, child_id, parent_copy.last_child);
    return LXA_OK;
}

static lxa_status_t
construct_child(lxa_dom_nodes_t *nodes, lxa_dom_ref_t parent,
                lxa_dom_ref_t child, lxa_dom_ref_t before)
{
    lxa_dom_record_t host, leaf, target;
    lxa_dom_record_t *records;
    lxa_id_t parent_id, child_id, target_id, cursor;
    size_t depth = 1, steps = 0, length;
    lxa_status_t status;
    if (nodes == NULL
        || parent.handle == child.handle) {
        return LXA_ERROR_ARGUMENT;
    }
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_dom_nodes_read(nodes, parent, &host);
    if (status == LXA_OK) {
        status = lxa_dom_nodes_read(nodes, child, &leaf);
    }
    if (status == LXA_OK && before.handle != LXA_HANDLE_NONE)
        status = lxa_dom_nodes_read(nodes, before, &target);
    if (status != LXA_OK) {
        return status;
    }
    length = lxa_vector_length(&nodes->records);
    parent_id = (lxa_id_t) parent.handle;
    child_id = (lxa_id_t) child.handle;
    target_id = before.handle == LXA_HANDLE_NONE ? LXA_ID_NONE
                : (lxa_id_t) before.handle;
    if ((host.kind != LXA_DOM_KIND_ELEMENT
         && host.kind != LXA_DOM_KIND_DOCUMENT
         && host.kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT)
        || (leaf.kind != LXA_DOM_KIND_ELEMENT
            && leaf.kind != LXA_DOM_KIND_TEXT
            && leaf.kind != LXA_DOM_KIND_COMMENT
            && leaf.kind != LXA_DOM_KIND_PROCESSING_INSTRUCTION
            && leaf.kind != LXA_DOM_KIND_DOCUMENT_TYPE)
        || (host.kind == LXA_DOM_KIND_ELEMENT
            && host.payload >= lxa_vector_length(&nodes->elements))
        || (leaf.kind == LXA_DOM_KIND_ELEMENT
            && leaf.payload >= lxa_vector_length(&nodes->elements))
        || ((leaf.kind == LXA_DOM_KIND_TEXT
             || leaf.kind == LXA_DOM_KIND_COMMENT)
            && leaf.payload >= lxa_vector_length(&nodes->characters))
        || (leaf.kind == LXA_DOM_KIND_PROCESSING_INSTRUCTION
            && leaf.payload >= lxa_vector_length(&nodes->pis))
        || (leaf.kind == LXA_DOM_KIND_DOCUMENT_TYPE
            && leaf.payload >= lxa_vector_length(&nodes->doctypes))
        || (host.kind != LXA_DOM_KIND_DOCUMENT
            && leaf.kind == LXA_DOM_KIND_DOCUMENT_TYPE)
        || (host.kind == LXA_DOM_KIND_DOCUMENT
            && leaf.kind != LXA_DOM_KIND_ELEMENT
            && leaf.kind != LXA_DOM_KIND_COMMENT
            && leaf.kind != LXA_DOM_KIND_PROCESSING_INSTRUCTION
            && leaf.kind != LXA_DOM_KIND_DOCUMENT_TYPE)
        || (target_id != LXA_ID_NONE
            && (target.parent != parent_id
                || target_id < nodes->retired || target_id >= length))
        || leaf.parent != LXA_ID_NONE || leaf.prev != LXA_ID_NONE
        || leaf.next != LXA_ID_NONE || leaf.first_child != LXA_ID_NONE
        || leaf.last_child != LXA_ID_NONE
        || (host.first_child == LXA_ID_NONE) != (host.last_child == LXA_ID_NONE)) {
        return LXA_ERROR_ARGUMENT;
    }
    status = check_template_parent(nodes, parent_id);
    if (status != LXA_OK) return status;
    if (lxa_vector_length(&nodes->templates) != 0) {
        status = check_virtual_ancestor(nodes, parent_id, child_id,
                                        &nodes->limit_reason);
        if (status != LXA_OK) return status;
    }
    records = (lxa_dom_record_t *) nodes->records.bytes.data;
    if (host.kind == LXA_DOM_KIND_DOCUMENT) {
        lxa_id_t sibling = host.first_child, previous = LXA_ID_NONE;
        size_t linked = 0;
        bool past_target = false;
        while (sibling != LXA_ID_NONE) {
            const lxa_dom_record_t *existing;
            if (sibling < nodes->retired || sibling >= length
                || ++linked > length) {
                return LXA_ERROR_ARGUMENT;
            }
            status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                        &nodes->limit_reason);
            if (status != LXA_OK) {
                return status;
            }
            existing = &records[sibling];
            if (sibling == target_id) past_target = true;
            if (existing->parent != parent_id || existing->prev != previous
                || (existing->kind != LXA_DOM_KIND_ELEMENT
                    && existing->kind != LXA_DOM_KIND_COMMENT
                    && existing->kind != LXA_DOM_KIND_PROCESSING_INSTRUCTION
                    && existing->kind != LXA_DOM_KIND_DOCUMENT_TYPE)
                || (leaf.kind == LXA_DOM_KIND_ELEMENT
                    && existing->kind == LXA_DOM_KIND_ELEMENT)
                || (leaf.kind == LXA_DOM_KIND_DOCUMENT_TYPE
                    && (existing->kind == LXA_DOM_KIND_DOCUMENT_TYPE
                        || (existing->kind == LXA_DOM_KIND_ELEMENT
                            && !past_target)))
                || (leaf.kind == LXA_DOM_KIND_ELEMENT && past_target
                    && existing->kind == LXA_DOM_KIND_DOCUMENT_TYPE)) {
                return LXA_ERROR_ARGUMENT;
            }
            previous = sibling;
            sibling = existing->next;
        }
        if (previous != host.last_child
            || (target_id != LXA_ID_NONE && !past_target)) {
            return LXA_ERROR_ARGUMENT;
        }
    }
    if (host.last_child != LXA_ID_NONE) {
        if (host.last_child < nodes->retired || host.last_child >= length
            || records[host.last_child].parent != parent_id
            || records[host.last_child].next != LXA_ID_NONE) {
            return LXA_ERROR_ARGUMENT;
        }
    }
    if (target_id != LXA_ID_NONE
        && (target.prev == LXA_ID_NONE
            ? host.first_child != target_id
            : (target.prev < nodes->retired || target.prev >= length
               || records[target.prev].next != target_id
               || records[target.prev].parent != parent_id))) {
        return LXA_ERROR_ARGUMENT;
    }
    for (cursor = parent_id; cursor != LXA_ID_NONE;) {
        lxa_dom_record_t *ancestor;
        if (cursor < nodes->retired || cursor >= length || ++steps > length) {
            return LXA_ERROR_ARGUMENT;
        }
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                    &nodes->limit_reason);
        if (status != LXA_OK) {
            return status;
        }
        ancestor = &records[cursor];
        if (ancestor->generation != nodes->generation
            || (ancestor->kind != LXA_DOM_KIND_DOCUMENT
                && ancestor->kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT
                && ancestor->kind != LXA_DOM_KIND_ELEMENT)) {
            return LXA_ERROR_ARGUMENT;
        }
        cursor = ancestor->parent;
        if (cursor != LXA_ID_NONE) {
            status = lxa_size_add(depth, 1, &depth);
            if (status != LXA_OK) {
                return status;
            }
        }
    }
    status = lxa_budget_check(nodes->budget, LXA_BUDGET_TREE_DEPTH, depth,
                              &nodes->limit_reason);
    if (status != LXA_OK) {
        return status;
    }
    if (target_id == LXA_ID_NONE)
        commit_append(nodes, parent_id, child_id, host.last_child);
    else
        commit_insert(nodes, parent_id, child_id, target_id);
    return LXA_OK;
}

lxa_status_t
lxa_dom_nodes_append_constructed_child(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t parent, lxa_dom_ref_t child)
{
    lxa_dom_ref_t end = {0};
    return construct_child(nodes, parent, child, end);
}

lxa_status_t
lxa_dom_nodes_insert_constructed_before(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t parent, lxa_dom_ref_t child, lxa_dom_ref_t before)
{
    if (before.handle == LXA_HANDLE_NONE) return LXA_ERROR_ARGUMENT;
    return construct_child(nodes, parent, child, before);
}

static lxa_status_t
attached_child_links(const lxa_dom_nodes_t *nodes, lxa_id_t child_id,
                     const lxa_dom_record_t *moved)
{
    const lxa_dom_record_t *records =
        (const lxa_dom_record_t *) nodes->records.bytes.data;
    size_t length = lxa_vector_length(&nodes->records);
    lxa_id_t old_id = moved->parent;
    const lxa_dom_record_t *old;
    if (old_id < nodes->retired || old_id >= length)
        return LXA_ERROR_ARGUMENT;
    old = &records[old_id];
    if ((old->kind != LXA_DOM_KIND_DOCUMENT
         && old->kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT
         && old->kind != LXA_DOM_KIND_ELEMENT)
        || (old->kind == LXA_DOM_KIND_ELEMENT
            && old->payload >= lxa_vector_length(&nodes->elements))
        || old->first_child == LXA_ID_NONE
        || (old->first_child == LXA_ID_NONE) != (old->last_child == LXA_ID_NONE)
        || (moved->prev == LXA_ID_NONE
            ? old->first_child != child_id
            : (moved->prev < nodes->retired || moved->prev >= length
               || records[moved->prev].next != child_id
               || records[moved->prev].parent != old_id))
        || (moved->next == LXA_ID_NONE
            ? old->last_child != child_id
            : (moved->next < nodes->retired || moved->next >= length
               || records[moved->next].prev != child_id
               || records[moved->next].parent != old_id))
        || moved->prev == child_id || moved->next == child_id
        || (old->first_child == child_id) != (moved->prev == LXA_ID_NONE)
        || (old->last_child == child_id) != (moved->next == LXA_ID_NONE))
        return LXA_ERROR_ARGUMENT;
    return LXA_OK;
}

static void
unlink_constructed(lxa_dom_record_t *records, const lxa_dom_record_t *moved)
{
    lxa_dom_record_t *old = &records[moved->parent];
    if (moved->prev == LXA_ID_NONE) old->first_child = moved->next;
    else records[moved->prev].next = moved->next;
    if (moved->next == LXA_ID_NONE) old->last_child = moved->prev;
    else records[moved->next].prev = moved->prev;
}

typedef enum { REMOVE_HOOK_NONE, REMOVE_HOOK_OPTION, REMOVE_HOOK_CONTENT } remove_hook_t;

static lxa_status_t
selectedcontent_removal_hook(lxa_dom_nodes_t *nodes, lxa_dom_ref_t cursor,
    lxa_dom_ref_t old_select, const select_view_t *view,
    select_tag_t *tag, remove_hook_t *hook)
{
    lxa_dom_ref_t ancestor, retained;
    lxa_status_t status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                            &nodes->limit_reason);
    *hook = REMOVE_HOOK_NONE;
    if (status == LXA_OK) status = select_tag(nodes, cursor, tag);
    if (status != LXA_OK) return status;
    if (*tag == SELECT_SELECTEDCONTENT) {
        lxa_dom_record_t descendant;
        status = lxa_dom_nodes_read(nodes, cursor, &descendant);
        if (status != LXA_OK) return status;
        if ((descendant.flags & ~LXA_DOM_SELECTEDCONTENT_DISABLED) != 0)
            return LXA_ERROR_ARGUMENT;
        if ((descendant.flags & LXA_DOM_SELECTEDCONTENT_DISABLED) == 0) {
            status = select_parent(nodes, cursor, view, &ancestor);
            if (status == LXA_OK) status = nearest_select(nodes, ancestor, view, &retained);
            if (status != LXA_OK) return status;
            if (retained.handle == LXA_HANDLE_NONE) *hook = REMOVE_HOOK_CONTENT;
        }
    }
    else if (*tag == SELECT_OPTION && old_select.handle != LXA_HANDLE_NONE) {
        status = option_select(nodes, cursor, NULL, &ancestor);
        if (status != LXA_OK) return status;
        if (ancestor.handle == old_select.handle) *hook = REMOVE_HOOK_OPTION;
    }
    return LXA_OK;
}

static lxa_status_t
check_selectedcontent_child_removals(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t host, lxa_dom_ref_t parent, const select_view_t *placement)
{
    lxa_dom_ref_t root, select, no_owner = {0};
    size_t visits = 0, ascents = 0, length = lxa_vector_length(&nodes->records);
    lxa_status_t status = nearest_select(nodes, parent, NULL, &select);
    if (status == LXA_OK) status = select_first_child(nodes, host, placement, &root);
    while (status == LXA_OK && root.handle != LXA_HANDLE_NONE) {
        lxa_dom_ref_t cursor = root;
        select_view_t removing = placement == NULL
            ? (select_view_t){.kind = SELECT_UNCHANGED, .prior_choice = LXA_ID_NONE} : *placement;
        removing.excluded = root.handle;
        while (status == LXA_OK && cursor.handle != LXA_HANDLE_NONE) {
            select_tag_t tag;
            remove_hook_t hook;
            if (++visits > length) return LXA_ERROR_ARGUMENT;
            if (placement != NULL && cursor.handle == placement->excluded) {
                status = next_descendant(nodes, root, cursor, false, &ascents, &cursor);
                continue;
            }
            status = selectedcontent_removal_hook(nodes, cursor, no_owner, &removing, &tag, &hook);
            if (status != LXA_OK) return status;
            if (hook == REMOVE_HOOK_CONTENT && select.handle != LXA_HANDLE_NONE) {
                lxa_dom_ref_t target;
                status = enabled_selectedcontent(nodes, select, root.handle, &target);
                if (status != LXA_OK) return status;
                if (target.handle != LXA_HANDLE_NONE) return LXA_ERROR_UNSUPPORTED;
            }
            if (cursor.handle == root.handle) {
                if (tag == SELECT_LEAF) cursor = (lxa_dom_ref_t){0};
                else status = lxa_dom_nodes_first_child(nodes, root, &cursor);
            }
            else status = next_descendant(nodes, root, cursor, tag != SELECT_LEAF,
                                            &ascents, &cursor);
        }
        if (status == LXA_OK) status = select_next_sibling(nodes, root, placement, &root);
    }
    return status;
}

static lxa_status_t
plan_selectedcontent_removal(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t element, const lxa_dom_record_t *record,
    select_reset_plan_t *before_update, child_replace_plan_t *replacement,
    lxa_vector_t *updates, select_view_t *result)
{
    lxa_dom_ref_t parent, target, cursor;
    select_tag_t tag;
    select_view_t view = {.kind = SELECT_UNCHANGED, .excluded = element.handle,
                          .prior_choice = LXA_ID_NONE};
    size_t effects = 0;
    bool pending = false, first_reset = false;
    lxa_status_t status = select_tag(nodes, element, &tag);
    if (status != LXA_OK) return status;
    if (tag == SELECT_SELECTEDCONTENT
        && (record->flags & ~LXA_DOM_SELECTEDCONTENT_DISABLED) != 0)
        return LXA_ERROR_ARGUMENT;
    if (record->first_child == LXA_ID_NONE
        && (tag != SELECT_SELECTEDCONTENT
            || (record->flags & LXA_DOM_SELECTEDCONTENT_DISABLED) != 0))
        return LXA_OK;
    status = lxa_dom_nodes_parent(nodes, element, &parent);
    if (status == LXA_OK) status = nearest_select(nodes, parent, NULL, &parent);
    if (status != LXA_OK || parent.handle == LXA_HANDLE_NONE) return status;
    size_t visits = 0, ascents = 0, length = lxa_vector_length(&nodes->records);
    cursor = element;
    while (status == LXA_OK && cursor.handle != LXA_HANDLE_NONE) {
        remove_hook_t hook;
        if (++visits > length) return LXA_ERROR_ARGUMENT;
        status = selectedcontent_removal_hook(nodes, cursor, parent, &view, &tag, &hook);
        if (status != LXA_OK) return status;
        if (hook == REMOVE_HOOK_OPTION) pending = true;
        else if (hook == REMOVE_HOOK_CONTENT) {
            if (effects == 0) first_reset = pending;
            ++effects;
            pending = false;
        }
        if (cursor.handle == element.handle)
            status = lxa_dom_nodes_first_child(nodes, element, &cursor);
        else status = next_descendant(nodes, element, cursor, tag != SELECT_LEAF,
                                        &ascents, &cursor);
    }
    if (status != LXA_OK || effects == 0) return status;
    status = enabled_selectedcontent(nodes, parent, element.handle, &target);
    if (status != LXA_OK || target.handle == LXA_HANDLE_NONE) return status;
    if (effects > 1) {
        size_t seen = 0;
        visits = ascents = 0;
        pending = false;
        cursor = element;
        status = lxa_vector_init(updates, &nodes->allocator, sizeof(selectedcontent_update_plan_t));
        if (status == LXA_OK) status = lxa_vector_bind_budget(updates, nodes->budget);
        if (status == LXA_OK) status = lxa_vector_reserve(updates, effects - 1);
        while (status == LXA_OK && cursor.handle != LXA_HANDLE_NONE) {
            remove_hook_t hook;
            if (++visits > length) { status = LXA_ERROR_ARGUMENT; break; }
            status = selectedcontent_removal_hook(nodes, cursor, parent, &view, &tag, &hook);
            if (status != LXA_OK) break;
            if (hook == REMOVE_HOOK_OPTION) pending = true;
            else if (hook == REMOVE_HOOK_CONTENT) {
                if (seen != 0) {
                    selectedcontent_update_plan_t update = {
                        .reset_before = pending, .reset.choice = LXA_ID_NONE,
                        .replacement.reset.choice = LXA_ID_NONE};
                    status = lxa_vector_push(updates, &update);
                }
                ++seen;
                pending = false;
            }
            if (status == LXA_OK) {
                if (cursor.handle == element.handle)
                    status = lxa_dom_nodes_first_child(nodes, element, &cursor);
                else status = next_descendant(nodes, element, cursor, tag != SELECT_LEAF,
                                                &ascents, &cursor);
            }
        }
        if (status == LXA_OK && seen != effects) status = LXA_ERROR_ARGUMENT;
    }
    select_view_t source_view = view;
    for (size_t effect = 0; status == LXA_OK && effect < effects; ++effect) {
        selectedcontent_update_plan_t *update = effect == 0 ? NULL
            : &((selectedcontent_update_plan_t *) updates->bytes.data)[effect - 1];
        child_replace_plan_t *current = effect == 0 ? replacement : &update->replacement;
        select_reset_plan_t *reset = effect == 0 ? before_update : &update->reset;
        select_reset_scan_t scan = {.kind = SELECT_SCAN_FIRST_SELECTED};
        if (effect != 0) {
            view = effect == 1 ? replacement->result
                : ((selectedcontent_update_plan_t *) updates->bytes.data)[effect - 2].replacement.result;
            view.prior_collapsed |= view.final_collapsed;
            if (view.final_chosen) view.prior_choice = view.final_choice;
            view.final_collapsed = view.final_chosen = false;
            view.original_length = lxa_vector_length(&nodes->records);
        }
        if (view.selection_owner.handle == LXA_HANDLE_NONE
            || view.selection_owner.handle == parent.handle)
            source_view = view;
        if (effect == 0 ? first_reset : update->reset_before) {
            status = plan_select_reset(nodes, parent, LXA_HANDLE_NONE, &source_view, reset);
            if (status == LXA_OK) {
                source_view.prior_collapsed |= reset->options.bytes.length != 0;
                if (reset->choice != LXA_ID_NONE) source_view.prior_choice = reset->choice;
                source_view.original_length = lxa_vector_length(&nodes->records);
                source_view.selection_owner = parent;
            }
        }
        if (status != LXA_OK) break;
        status = scan_select_options(nodes, parent, LXA_HANDLE_NONE, &source_view, &scan);
        if (status == LXA_OK) {
            const select_view_t *copy_view = view.selection_owner.handle != LXA_HANDLE_NONE
                && view.selection_owner.handle != parent.handle ? &view : &source_view;
            status = prepare_child_replacement(nodes, target, scan.first, true, copy_view, current);
        }
        if (status == LXA_OK && effect + 1 < effects
            && current->staging.handle != LXA_HANDLE_NONE)
            status = check_selectedcontent_child_removals(nodes, current->staging, target, NULL);
        if (status == LXA_OK) {
            if (current->result.selection_owner.handle == LXA_HANDLE_NONE
                || current->result.selection_owner.handle == parent.handle)
                source_view = current->result;
            else {
                /* Adopt replacement links without the other select's flags. */
                source_view.kind = current->result.kind;
                source_view.subtree = current->result.subtree;
                source_view.parent = current->result.parent;
            }
        }
    }
    if (status == LXA_OK) *result = source_view;
    if (status == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_COUNT)
        nodes->limit_reason = LXA_BUDGET_MEMORY;
    return status;
}

lxa_status_t
lxa_dom_nodes_detach_constructed_element(lxa_dom_nodes_t *nodes,
                                         lxa_dom_ref_t element)
{
    lxa_dom_record_t moved, *records;
    select_reset_plan_t plan = {.choice = LXA_ID_NONE};
    select_reset_plan_t before_update = {.choice = LXA_ID_NONE};
    child_replace_plan_t replacement = {.reset.choice = LXA_ID_NONE};
    lxa_vector_t updates = {0};
    select_view_t result = {0};
    lxa_dom_ref_t affected_select = {0};
    bool option;
    lxa_id_t child_id;
    lxa_status_t status;
    if (nodes == NULL) return LXA_ERROR_ARGUMENT;
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_dom_nodes_read(nodes, element, &moved);
    if (status != LXA_OK) return status;
    if (moved.kind != LXA_DOM_KIND_ELEMENT
        || moved.payload >= lxa_vector_length(&nodes->elements))
        return LXA_ERROR_ARGUMENT;
    child_id = (lxa_id_t) element.handle;
    status = attached_child_links(nodes, child_id, &moved);
    if (status == LXA_OK)
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                   &nodes->limit_reason);
    if (status != LXA_OK) return status;
    status = html_option(nodes, element, &option);
    if (status == LXA_OK) {
        lxa_dom_ref_t select;
        if (option) status = option_select(nodes, element, NULL, &select);
        else status = option_select_at(nodes, element, NULL, &select);
        if (status == LXA_OK && select.handle != LXA_HANDLE_NONE
            && select.handle != element.handle) {
            bool affected = option;
            if (!affected) {
                select_reset_scan_t scan = {.kind = SELECT_SCAN_EXCLUSION};
                status = scan_select_options(nodes, select, element.handle,
                                               NULL, &scan);
                affected = scan.found_excluded;
            }
            if (status == LXA_OK && affected) affected_select = select;
        }
    }
    if (status == LXA_OK && (!option || moved.first_child != LXA_ID_NONE))
        status = plan_selectedcontent_removal(nodes, element, &moved, &before_update, &replacement, &updates, &result);
    if (status == LXA_OK && affected_select.handle != LXA_HANDLE_NONE) {
        if (replacement.target.handle != LXA_HANDLE_NONE) {
            status = plan_select_reset(nodes, affected_select, LXA_HANDLE_NONE, &result, &plan);
        }
        else status = plan_select_reset(nodes, affected_select, element.handle, NULL, &plan);
    }
    if (status != LXA_OK) goto done;
    records = (lxa_dom_record_t *) nodes->records.bytes.data;
    unlink_constructed(records, &moved);
    records[child_id].parent = LXA_ID_NONE;
    records[child_id].prev = LXA_ID_NONE;
    records[child_id].next = LXA_ID_NONE;
    commit_select_reset(nodes, &before_update);
    if (replacement.target.handle != LXA_HANDLE_NONE)
        commit_child_replacement(nodes, &replacement);
    for (size_t i = 0; i < lxa_vector_length(&updates); ++i) {
        selectedcontent_update_plan_t *update = &((selectedcontent_update_plan_t *) updates.bytes.data)[i];
        commit_select_reset(nodes, &update->reset);
        commit_child_replacement(nodes, &update->replacement);
    }
    commit_select_reset(nodes, &plan);
done:
    for (size_t i = 0; i < lxa_vector_length(&updates); ++i) {
        selectedcontent_update_plan_t *update = &((selectedcontent_update_plan_t *) updates.bytes.data)[i];
        destroy_child_replacement(&update->replacement);
        lxa_vector_destroy(&update->reset.options);
    }
    lxa_vector_destroy(&updates);
    destroy_child_replacement(&replacement);
    lxa_vector_destroy(&before_update.options);
    lxa_vector_destroy(&plan.options);
    return status;
}

static lxa_status_t
collect_option_move_selects(lxa_dom_nodes_t *nodes, lxa_dom_ref_t ref,
                             const select_view_t *move,
                             lxa_dom_ref_t selects[2], size_t *count)
{
    lxa_dom_ref_t owners[2];
    bool option;
    lxa_status_t status = html_option(nodes, ref, &option);
    if (status != LXA_OK || !option) return status;
    status = option_select(nodes, ref, NULL, &owners[0]);
    if (status == LXA_OK) status = option_select(nodes, ref, move, &owners[1]);
    if (status != LXA_OK || owners[0].handle == owners[1].handle) return status;
    for (size_t i = 0; i < 2; ++i) {
        size_t j = 0;
        if (owners[i].handle == LXA_HANDLE_NONE) continue;
        while (j < *count && selects[j].handle != owners[i].handle) ++j;
        if (j != *count) continue;
        /* Internal select owners do not change when their subtree moves. */
        if (*count == 2) return LXA_ERROR_ARGUMENT;
        selects[(*count)++] = owners[i];
    }
    return LXA_OK;
}

lxa_status_t
reparent_constructed(lxa_dom_nodes_t *nodes, lxa_dom_ref_t parent,
                     lxa_dom_ref_t child, lxa_dom_ref_t before)
{
    lxa_dom_record_t host, moved, target, *records, *old;
    select_view_t move = {.subtree = child, .parent = parent, .before = before,
                          .kind = SELECT_MOVE};
    lxa_dom_ref_t selects[2];
    select_reset_plan_t plans[2] = {{.choice = LXA_ID_NONE}, {.choice = LXA_ID_NONE}};
    size_t select_count = 0;
    lxa_id_t host_id, child_id, old_id, target_id, cursor, predecessor;
    size_t length, depth = 1, relative = 1, max_relative = 1;
    size_t visits = 0, ascents = 0;
    size_t new_depth;
    lxa_status_t status;
    if (nodes == NULL
        || parent.handle == child.handle) {
        return LXA_ERROR_ARGUMENT;
    }
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_dom_nodes_read(nodes, parent, &host);
    if (status == LXA_OK)
        status = lxa_dom_nodes_read(nodes, child, &moved);
    if (status == LXA_OK && before.handle != LXA_HANDLE_NONE)
        status = lxa_dom_nodes_read(nodes, before, &target);
    if (status != LXA_OK) return status;
    length = lxa_vector_length(&nodes->records);
    host_id = (lxa_id_t) parent.handle;
    child_id = (lxa_id_t) child.handle;
    old_id = moved.parent;
    target_id = before.handle == LXA_HANDLE_NONE ? LXA_ID_NONE
                : (lxa_id_t) before.handle;
    if ((host.kind != LXA_DOM_KIND_ELEMENT
         && host.kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT)
        || (moved.kind != LXA_DOM_KIND_ELEMENT
            && moved.kind != LXA_DOM_KIND_TEXT
            && moved.kind != LXA_DOM_KIND_COMMENT)
        || (host.kind == LXA_DOM_KIND_ELEMENT
            && host.payload >= lxa_vector_length(&nodes->elements))
        || (moved.kind == LXA_DOM_KIND_ELEMENT
            && moved.payload >= lxa_vector_length(&nodes->elements))
        || (target_id != LXA_ID_NONE
            && (target.parent != host_id || target_id < nodes->retired
                || target_id >= length))
        || old_id < nodes->retired || old_id >= length
        || (host.first_child == LXA_ID_NONE) != (host.last_child == LXA_ID_NONE))
        return LXA_ERROR_ARGUMENT;
    status = check_template_parent(nodes, host_id);
    if (status != LXA_OK) return status;
    if (lxa_vector_length(&nodes->templates) != 0) {
        status = check_virtual_ancestor(nodes, host_id, child_id,
                                        &nodes->limit_reason);
        if (status != LXA_OK) return status;
    }
    records = (lxa_dom_record_t *) nodes->records.bytes.data;
    old = &records[old_id];
    status = attached_child_links(nodes, child_id, &moved);
    if (status != LXA_OK) return status;
    if ((host.last_child != LXA_ID_NONE
            && (host.last_child < nodes->retired || host.last_child >= length
                || records[host.last_child].next != LXA_ID_NONE
                || records[host.last_child].parent != host_id))
        || (target_id != LXA_ID_NONE
            && (target.prev == LXA_ID_NONE
                ? host.first_child != target_id
                : (target.prev < nodes->retired || target.prev >= length
                   || records[target.prev].next != target_id
                   || records[target.prev].parent != host_id))))
        return LXA_ERROR_ARGUMENT;
    for (cursor = host_id; cursor != LXA_ID_NONE;) {
        if (cursor < nodes->retired || cursor >= length
            || cursor == child_id || depth > length)
            return LXA_ERROR_ARGUMENT;
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                   &nodes->limit_reason);
        if (status != LXA_OK) return status;
        cursor = records[cursor].parent;
        if (cursor != LXA_ID_NONE) {
            status = lxa_size_add(depth, 1, &depth);
            if (status != LXA_OK) return status;
        }
    }
    /* Iterative subtree walk. Visit each member once, and charge ascents.
     * Broken child/sibling rings stop at the number of allocated slots. */
    cursor = child_id;
    for (;;) {
        lxa_dom_record_t entry;
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                   &nodes->limit_reason);
        if (status != LXA_OK) return status;
        if (cursor < nodes->retired || cursor >= length || ++visits > length)
            return LXA_ERROR_ARGUMENT;
        entry = records[cursor];
        if (relative > max_relative) max_relative = relative;
        if (entry.kind != LXA_DOM_KIND_ELEMENT
            && entry.kind != LXA_DOM_KIND_TEXT
            && entry.kind != LXA_DOM_KIND_COMMENT)
            return LXA_ERROR_ARGUMENT;
        if (entry.kind == LXA_DOM_KIND_ELEMENT) {
            lxa_dom_ref_t ref = {.document = nodes, .epoch = nodes->epoch};
            status = lxa_handle_pack(cursor, nodes->generation, &ref.handle);
            if (status == LXA_OK)
                status = collect_option_move_selects(nodes, ref, &move,
                                                       selects, &select_count);
            if (status != LXA_OK) return status;
        }
        if ((entry.first_child == LXA_ID_NONE)
            != (entry.last_child == LXA_ID_NONE))
            return LXA_ERROR_ARGUMENT;
        if (entry.first_child != LXA_ID_NONE) {
            lxa_id_t next = entry.first_child;
            if (next < nodes->retired || next >= length
                || records[next].parent != cursor
                || records[next].prev != LXA_ID_NONE)
                return LXA_ERROR_ARGUMENT;
            cursor = next;
            ++relative;
            continue;
        }
        while (cursor != child_id && records[cursor].next == LXA_ID_NONE) {
            lxa_id_t up = records[cursor].parent;
            status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                       &nodes->limit_reason);
            if (status != LXA_OK) return status;
            if (up < nodes->retired || up >= length || ++ascents > length
                || records[up].last_child != cursor)
                return LXA_ERROR_ARGUMENT;
            cursor = up;
            --relative;
        }
        if (cursor == child_id) break;
        {
            lxa_id_t next = records[cursor].next;
            if (next < nodes->retired || next >= length
                || records[next].parent != records[cursor].parent
                || records[next].prev != cursor)
                return LXA_ERROR_ARGUMENT;
            cursor = next;
        }
    }
    status = lxa_size_add(depth, max_relative - 1, &new_depth);
    if (status == LXA_OK)
        status = lxa_budget_check(nodes->budget, LXA_BUDGET_TREE_DEPTH,
                                  new_depth, &nodes->limit_reason);
    if (status != LXA_OK) return status;
    if (host_id == old_id
        && (target_id == child_id
            || (target_id == LXA_ID_NONE && old->last_child == child_id)
            || target_id == moved.next))
        return LXA_OK;
    for (size_t i = 0; i < select_count; ++i) {
        status = plan_select_reset(nodes, selects[i], LXA_HANDLE_NONE, &move, &plans[i]);
        if (status != LXA_OK) goto done;
    }
    /* All remaining writes are allocation-free and cannot fail. */
    unlink_constructed(records, &moved);
    predecessor = target_id == LXA_ID_NONE
                  ? records[host_id].last_child : records[target_id].prev;
    records[child_id].parent = host_id;
    records[child_id].prev = predecessor;
    records[child_id].next = target_id;
    if (predecessor == LXA_ID_NONE) records[host_id].first_child = child_id;
    else records[predecessor].next = child_id;
    if (target_id == LXA_ID_NONE) records[host_id].last_child = child_id;
    else records[target_id].prev = child_id;
    for (size_t i = 0; i < select_count; ++i) commit_select_reset(nodes, &plans[i]);
done:
    for (size_t i = 0; i < 2; ++i) lxa_vector_destroy(&plans[i].options);
    return status;
}

lxa_status_t
lxa_dom_nodes_reparent_constructed_child(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t parent, lxa_dom_ref_t child)
{
    lxa_dom_ref_t end = {0};
    return reparent_constructed(nodes, parent, child, end);
}

lxa_status_t
lxa_dom_nodes_reparent_constructed_before(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t parent, lxa_dom_ref_t child, lxa_dom_ref_t before)
{
    if (before.handle == LXA_HANDLE_NONE) return LXA_ERROR_ARGUMENT;
    return reparent_constructed(nodes, parent, child, before);
}

typedef struct {
    lxa_dom_ref_t source, destination;
    lxa_id_t next, previous, last;
    size_t depth;
} clone_children_frame_t;

static lxa_status_t
clone_children_frame(lxa_dom_nodes_t *nodes, lxa_vector_t *frames,
                     lxa_dom_ref_t source, lxa_dom_ref_t destination,
                     size_t depth)
{
    lxa_dom_record_t record;
    clone_children_frame_t frame;
    lxa_status_t status = lxa_dom_nodes_read(nodes, source, &record);
    if (status != LXA_OK) return status;
    if ((record.kind != LXA_DOM_KIND_ELEMENT
         && record.kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT)
        || (record.first_child == LXA_ID_NONE)
           != (record.last_child == LXA_ID_NONE))
        return LXA_ERROR_ARGUMENT;
    frame = (clone_children_frame_t){source, destination, record.first_child,
                                    LXA_ID_NONE, record.last_child, depth};
    return lxa_vector_push(frames, &frame);
}

static lxa_status_t
clone_child_lists(lxa_dom_nodes_t *nodes, lxa_dom_ref_t source,
                  lxa_dom_ref_t staging, size_t target_depth, size_t length,
                  size_t *roots, bool option_lifecycle, lxa_handle_t excluded)
{
    lxa_vector_t frames;
    lxa_buffer_t text = {0};
    size_t visits = 0;
    lxa_status_t status = lxa_vector_init(&frames, &nodes->allocator,
                                          sizeof(clone_children_frame_t));
    if (status != LXA_OK) return status;
    status = lxa_buffer_init(&text, &nodes->allocator);
    if (status == LXA_OK) status = lxa_vector_bind_budget(&frames, nodes->budget);
    if (status == LXA_OK) status = lxa_buffer_bind_budget(&text, nodes->budget);
    if (status == LXA_OK)
        status = clone_children_frame(nodes, &frames, source, staging, 0);
    while (status == LXA_OK && lxa_vector_length(&frames) != 0) {
        size_t top = lxa_vector_length(&frames) - 1, depth;
        clone_children_frame_t frame =
            ((clone_children_frame_t *) frames.bytes.data)[top];
        lxa_dom_record_t child_record;
        lxa_dom_ref_t child = {nodes, nodes->epoch, 0}, clone, contents = {0};
        lxa_span_t bytes, pi_target;
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                    &nodes->limit_reason);
        if (status != LXA_OK) break;
        if (frame.next == LXA_ID_NONE) {
            if (frame.previous != frame.last) status = LXA_ERROR_ARGUMENT;
            else lxa_vector_erase(&frames, top, 1);
            continue;
        }
        if (frame.next < nodes->retired || frame.next >= length
            || ++visits > length) {
            status = LXA_ERROR_ARGUMENT;
            break;
        }
        status = lxa_handle_pack(frame.next, nodes->generation, &child.handle);
        if (status == LXA_OK)
            status = lxa_dom_nodes_read(nodes, child, &child_record);
        if (status != LXA_OK) break;
        if (child_record.parent != (lxa_id_t) frame.source.handle
            || child_record.prev != frame.previous
            || (child_record.first_child == LXA_ID_NONE)
               != (child_record.last_child == LXA_ID_NONE)
            || (child_record.kind != LXA_DOM_KIND_ELEMENT
                && (child_record.first_child != LXA_ID_NONE
                    || child_record.last_child != LXA_ID_NONE))) {
            status = LXA_ERROR_ARGUMENT;
            break;
        }
        ((clone_children_frame_t *) frames.bytes.data)[top].next =
            child_record.next;
        ((clone_children_frame_t *) frames.bytes.data)[top].previous = frame.next;
        if (child.handle == excluded) continue;
        status = lxa_size_add(frame.depth, 1, &depth);
        if (status == LXA_OK) {
            size_t absolute;
            status = lxa_size_add(target_depth, depth, &absolute);
            if (status == LXA_OK)
                status = lxa_budget_check(nodes->budget, LXA_BUDGET_TREE_DEPTH,
                                           absolute, &nodes->limit_reason);
        }
        if (status != LXA_OK) break;
        switch (child_record.kind) {
        case LXA_DOM_KIND_ELEMENT:
            status = lxa_dom_nodes_clone_element(nodes, child, &clone);
            if (status == LXA_OK)
                status = lxa_dom_nodes_template_contents(nodes, child, &contents);
            break;
        case LXA_DOM_KIND_TEXT:
        case LXA_DOM_KIND_COMMENT:
            status = lxa_dom_nodes_character_read(nodes, child, &bytes);
            if (status == LXA_OK)
                status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK,
                                            bytes.length, &nodes->limit_reason);
            lxa_buffer_truncate(&text, 0);
            if (status == LXA_OK) status = lxa_buffer_append(&text, bytes);
            if (status == LXA_OK)
                status = lxa_dom_nodes_append_character(nodes,
                    (lxa_dom_kind_t) child_record.kind,
                    (lxa_span_t){text.data, text.length}, &clone);
            break;
        case LXA_DOM_KIND_PROCESSING_INSTRUCTION:
            status = lxa_dom_nodes_pi_read(nodes, child, &pi_target, &bytes);
            if (status == LXA_OK)
                status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK,
                                            bytes.length, &nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_append_pi(nodes, pi_target, bytes, &clone);
            break;
        default:
            status = LXA_ERROR_ARGUMENT;
            break;
        }
        if (status == LXA_OK)
            status = lxa_dom_nodes_append_constructed_child(nodes,
                frame.destination, clone);
        if (status == LXA_OK && option_lifecycle
            && child_record.kind == LXA_DOM_KIND_ELEMENT) {
            bool option;
            status = html_option(nodes, clone, &option);
            if (status == LXA_OK && option)
                status = lxa_dom_nodes_reset_option_select(nodes, clone);
        }
        if (status == LXA_OK && top == 0) ++*roots;
        if (status != LXA_OK) break;
        if (contents.handle != LXA_HANDLE_NONE) {
            lxa_dom_record_t record;
            lxa_dom_ref_t fragment;
            status = lxa_dom_nodes_read(nodes, contents, &record);
            if (status == LXA_OK
                && (child_record.first_child != LXA_ID_NONE
                    || record.kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT
                    || record.parent != LXA_ID_NONE || record.prev != LXA_ID_NONE
                    || record.next != LXA_ID_NONE || ++visits > length))
                status = LXA_ERROR_ARGUMENT;
            if (status == LXA_OK) status = lxa_size_add(depth, 1, &depth);
            if (status == LXA_OK) {
                size_t absolute;
                status = lxa_size_add(target_depth, depth, &absolute);
                if (status == LXA_OK)
                    status = lxa_budget_check(nodes->budget,
                        LXA_BUDGET_TREE_DEPTH, absolute, &nodes->limit_reason);
            }
            if (status == LXA_OK)
                status = lxa_dom_nodes_append_draft(nodes,
                    LXA_DOM_KIND_DOCUMENT_FRAGMENT, &fragment);
            if (status == LXA_OK)
                status = lxa_dom_nodes_bind_template(nodes, clone, fragment);
            if (status == LXA_OK)
                status = clone_children_frame(nodes, &frames, contents,
                                               fragment, depth);
        }
        else if (child_record.kind == LXA_DOM_KIND_ELEMENT)
            status = clone_children_frame(nodes, &frames, child, clone, depth);
    }
    if (status == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_COUNT)
        nodes->limit_reason = LXA_BUDGET_MEMORY;
    lxa_buffer_destroy(&text);
    lxa_vector_destroy(&frames);
    return status;
}

static lxa_status_t
prepare_child_replacement(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t target, lxa_dom_ref_t source, bool selectedcontent_copy,
    const select_view_t *initial, child_replace_plan_t *plan)
{
    lxa_dom_record_t host, original, candidate = {0}, *records;
    lxa_dom_ref_t staging = {0}, ancestor = target;
    lxa_id_t cursor, previous = LXA_ID_NONE;
    size_t length, old_count = 0, roots = 0, depth = 0, work;
    bool copy_source, connected = false, inert = false;
    lxa_vector_t removals = {0};
    select_reset_plan_t reset = {.choice = LXA_ID_NONE};
    lxa_status_t status;
    if (nodes == NULL) return LXA_ERROR_ARGUMENT;
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_dom_nodes_read(nodes, target, &host);
    if (status != LXA_OK) return status;
    if ((host.kind != LXA_DOM_KIND_ELEMENT
         && host.kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT)
        || (host.kind == LXA_DOM_KIND_ELEMENT
            && host.payload >= lxa_vector_length(&nodes->elements))
        || (host.first_child == LXA_ID_NONE) != (host.last_child == LXA_ID_NONE))
        return LXA_ERROR_ARGUMENT;
    status = check_template_parent(nodes, (lxa_id_t) target.handle);
    if (status != LXA_OK) return status;
    length = lxa_vector_length(&nodes->records);
    lxa_id_t host_parent = (lxa_id_t) target.handle;
    if (initial != NULL && initial->kind == SELECT_REPLACE_CHILDREN
        && initial->parent.handle == target.handle) {
        lxa_dom_record_t proposed = {0};
        if (initial->subtree.handle != LXA_HANDLE_NONE) {
            status = lxa_dom_nodes_read(nodes, initial->subtree, &proposed);
            if (status != LXA_OK) return status;
            host_parent = (lxa_id_t) initial->subtree.handle;
        }
        else proposed.first_child = proposed.last_child = LXA_ID_NONE;
        host.first_child = proposed.first_child;
        host.last_child = proposed.last_child;
    }
    records = (lxa_dom_record_t *) nodes->records.bytes.data;
    for (cursor = host.first_child; cursor != LXA_ID_NONE;) {
        lxa_dom_record_t child;
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                    &nodes->limit_reason);
        if (status != LXA_OK) return status;
        if (cursor < nodes->retired || cursor >= length || ++old_count > length)
            return LXA_ERROR_ARGUMENT;
        child = records[cursor];
        if (child.generation != nodes->generation
            || (child.kind != LXA_DOM_KIND_ELEMENT
                && child.kind != LXA_DOM_KIND_TEXT
                && child.kind != LXA_DOM_KIND_COMMENT
                && child.kind != LXA_DOM_KIND_PROCESSING_INSTRUCTION)
            || child.parent != host_parent || child.prev != previous)
            return LXA_ERROR_ARGUMENT;
        previous = cursor;
        cursor = child.next;
    }
    if (previous != host.last_child) return LXA_ERROR_ARGUMENT;
    if (initial != NULL && initial->excluded != LXA_HANDLE_NONE) {
        lxa_dom_ref_t excluded = {nodes, nodes->epoch, initial->excluded};
        lxa_dom_record_t removed;
        status = lxa_dom_nodes_read(nodes, excluded, &removed);
        if (status != LXA_OK) return status;
        if (removed.parent == (lxa_id_t) target.handle) {
            if (host.first_child == (lxa_id_t) excluded.handle)
                host.first_child = removed.next;
            if (host.last_child == (lxa_id_t) excluded.handle)
                host.last_child = removed.prev;
        }
    }
    if (selectedcontent_copy) {
        status = check_selectedcontent_child_removals(nodes, target, target, initial);
        if (status != LXA_OK) return status;
    }
    if (source.handle != LXA_HANDLE_NONE) {
        status = lxa_dom_nodes_read(nodes, source, &original);
        if (status != LXA_OK) return status;
        if ((original.kind != LXA_DOM_KIND_ELEMENT
             && original.kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT)
            || (original.kind == LXA_DOM_KIND_ELEMENT
                && original.payload >= lxa_vector_length(&nodes->elements))
            || (original.first_child == LXA_ID_NONE)
               != (original.last_child == LXA_ID_NONE))
            return LXA_ERROR_ARGUMENT;
        copy_source = original.first_child != LXA_ID_NONE;
        while (copy_source && ancestor.handle != LXA_HANDLE_NONE) {
            if (++depth > length) return LXA_ERROR_ARGUMENT;
            status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                        &nodes->limit_reason);
            if (status == LXA_OK)
                status = lxa_dom_nodes_read(nodes, ancestor, &original);
            if (status != LXA_OK) return status;
            if (original.kind != LXA_DOM_KIND_ELEMENT
                && original.kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT
                && original.kind != LXA_DOM_KIND_DOCUMENT)
                return LXA_ERROR_ARGUMENT;
            if (original.kind == LXA_DOM_KIND_DOCUMENT) connected = !inert;
            if (original.parent != LXA_ID_NONE)
                status = lxa_dom_nodes_parent(nodes, ancestor, &ancestor);
            else if (original.kind == LXA_DOM_KIND_DOCUMENT_FRAGMENT) {
                inert = true;
                status = lxa_dom_nodes_template_owner(nodes, ancestor, &ancestor);
            }
            else ancestor = (lxa_dom_ref_t){0};
            if (status != LXA_OK) return status;
        }
        if (copy_source) {
            status = lxa_dom_nodes_append_draft(nodes,
                LXA_DOM_KIND_DOCUMENT_FRAGMENT, &staging);
            if (status == LXA_OK)
                status = clone_child_lists(nodes, source, staging, depth, length,
                                            &roots, selectedcontent_copy,
                                            initial == NULL ? LXA_HANDLE_NONE : initial->excluded);
            if (status == LXA_OK)
                status = lxa_dom_nodes_read(nodes, staging, &candidate);
            if (status == LXA_OK && selectedcontent_copy && connected)
                status = disable_cloned_selectedcontent(nodes, staging);
            if (status != LXA_OK) return status;
        }
    }
    status = lxa_size_add(old_count, roots, &work);
    if (status == LXA_OK) status = lxa_size_add(work, 1, &work);
    if (status == LXA_OK)
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, work,
                                    &nodes->limit_reason);
    if (status != LXA_OK) return status;
    if (selectedcontent_copy) {
        lxa_dom_ref_t select;
        select_view_t base = initial == NULL
            ? (select_view_t){.kind = SELECT_UNCHANGED, .prior_choice = LXA_ID_NONE}
            : *initial;
        select_view_t view = base;
        view.subtree = staging;
        view.parent = target;
        view.kind = SELECT_REPLACE_CHILDREN;
        view.original_length = length;
        select_reset_scan_t old_scan = {.kind = SELECT_SCAN_EXCLUSION};
        select_reset_scan_t new_scan = {.kind = SELECT_SCAN_EXCLUSION};
        status = option_select_at(nodes, target, &base, &select);
        if (status == LXA_OK && select.handle != LXA_HANDLE_NONE) {
            if (view.selection_owner.handle != LXA_HANDLE_NONE
                && view.selection_owner.handle != select.handle) {
                view.prior_collapsed = false;
                view.prior_choice = LXA_ID_NONE;
                view.final_collapsed = view.final_chosen = false;
            }
            view.selection_owner = select;
        }
        if (status == LXA_OK && select.handle != LXA_HANDLE_NONE)
            status = scan_select_options(nodes, select, target.handle, &base, &old_scan);
        if (status == LXA_OK && select.handle != LXA_HANDLE_NONE)
            status = scan_select_options(nodes, select, target.handle, &view, &new_scan);
        if (status == LXA_OK && old_scan.found_excluded) {
            lxa_dom_ref_t child;
            status = lxa_vector_init(&removals, &nodes->allocator,
                                     sizeof(select_reset_plan_t));
            if (status == LXA_OK) status = lxa_vector_bind_budget(&removals, nodes->budget);
            if (status == LXA_OK) status = select_first_child(nodes, target, &base, &child);
            /* Each root disconnects before its descendant option resets run. */
            while (status == LXA_OK && child.handle != LXA_HANDLE_NONE) {
                lxa_dom_ref_t next = {0};
                select_reset_scan_t probe = {.kind = SELECT_SCAN_EXCLUSION};
                status = select_next_sibling(nodes, child, &base, &next);
                if (status == LXA_OK)
                    status = scan_select_options(nodes, select, child.handle, &base, &probe);
                if (status == LXA_OK && probe.found_excluded) {
                    select_reset_plan_t plan = {.choice = LXA_ID_NONE};
                    select_view_t prefix = view;
                    prefix.kind = SELECT_REMOVE_PREFIX;
                    prefix.subtree = next;
                    prefix.staging_parent = base.kind == SELECT_REPLACE_CHILDREN
                        ? base.subtree : (lxa_dom_ref_t){0};
                    status = plan_select_reset(nodes, select, LXA_HANDLE_NONE, &prefix, &plan);
                    if (status == LXA_OK) status = lxa_vector_push(&removals, &plan);
                    if (status != LXA_OK) lxa_vector_destroy(&plan.options);
                    else {
                        if (plan.options.bytes.length != 0) view.prior_collapsed = true;
                        if (plan.choice != LXA_ID_NONE) view.prior_choice = plan.choice;
                    }
                }
                child = next;
            }
        }
        if (status == LXA_OK && new_scan.found_excluded)
            status = plan_select_reset(nodes, select, LXA_HANDLE_NONE, &view, &reset);
        if (status != LXA_OK) goto done;
        view.final_collapsed = reset.options.bytes.length != 0;
        view.final_chosen = reset.choice != LXA_ID_NONE;
        view.final_choice = reset.choice;
        plan->result = view;
    }
    plan->target = target;
    plan->staging = staging;
    plan->host = host;
    plan->candidate = candidate;
done:
    plan->removals = removals;
    plan->reset = reset;
    if (status == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_COUNT)
        nodes->limit_reason = LXA_BUDGET_MEMORY;
    return status;
}

static void
commit_child_replacement(lxa_dom_nodes_t *nodes, const child_replace_plan_t *plan)
{
    lxa_dom_ref_t target = plan->target, staging = plan->staging;
    lxa_dom_record_t host = plan->host, candidate = plan->candidate;
    lxa_dom_record_t *records = (lxa_dom_record_t *) nodes->records.bytes.data;
    lxa_id_t cursor;
    /* All source reads and resource checks precede the link transfer. */
    for (cursor = host.first_child; cursor != LXA_ID_NONE;) {
        lxa_id_t next = records[cursor].next;
        records[cursor].parent = records[cursor].prev = records[cursor].next =
            LXA_ID_NONE;
        cursor = next;
    }
    for (cursor = staging.handle == LXA_HANDLE_NONE ? LXA_ID_NONE
                  : candidate.first_child; cursor != LXA_ID_NONE;
         cursor = records[cursor].next)
        records[cursor].parent = (lxa_id_t) target.handle;
    records[(lxa_id_t) target.handle].first_child =
        staging.handle == LXA_HANDLE_NONE ? LXA_ID_NONE : candidate.first_child;
    records[(lxa_id_t) target.handle].last_child =
        staging.handle == LXA_HANDLE_NONE ? LXA_ID_NONE : candidate.last_child;
    if (staging.handle != LXA_HANDLE_NONE)
        records[(lxa_id_t) staging.handle].first_child =
            records[(lxa_id_t) staging.handle].last_child = LXA_ID_NONE;
    if (plan->removals.bytes.length != 0) {
        const select_reset_plan_t *plans = (const select_reset_plan_t *) plan->removals.bytes.data;
        for (size_t i = 0; i < lxa_vector_length(&plan->removals); ++i)
            commit_select_reset(nodes, &plans[i]);
    }
    commit_select_reset(nodes, &plan->reset);
}

static void
destroy_child_replacement(child_replace_plan_t *plan)
{
    if (plan->removals.bytes.length != 0) {
        select_reset_plan_t *plans = (select_reset_plan_t *) plan->removals.bytes.data;
        for (size_t i = 0; i < lxa_vector_length(&plan->removals); ++i)
            lxa_vector_destroy(&plans[i].options);
    }
    lxa_vector_destroy(&plan->removals);
    lxa_vector_destroy(&plan->reset.options);
}

static lxa_status_t
replace_children_with_clones(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t target, lxa_dom_ref_t source, bool selectedcontent_copy)
{
    child_replace_plan_t plan = {.reset.choice = LXA_ID_NONE};
    lxa_status_t status = prepare_child_replacement(nodes, target, source,
                                                      selectedcontent_copy,
                                                      NULL, &plan);
    if (status == LXA_OK) commit_child_replacement(nodes, &plan);
    destroy_child_replacement(&plan);
    return status;
}

lxa_status_t
lxa_dom_nodes_replace_children_with_clones(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t target, lxa_dom_ref_t source)
{
    return replace_children_with_clones(nodes, target, source, false);
}

lxa_status_t
lxa_dom_nodes_maybe_clone_selected_option(lxa_dom_nodes_t *nodes,
                                         lxa_dom_ref_t element)
{
    lxa_dom_ref_t select, target;
    lxa_dom_record_t record;
    select_tag_t tag;
    lxa_status_t status;
    if (nodes == NULL) return LXA_ERROR_ARGUMENT;
    status = select_tag(nodes, element, &tag);
    if (status != LXA_OK || tag != SELECT_OPTION) return status;
    status = lxa_dom_nodes_read(nodes, element, &record);
    if (status != LXA_OK) return status;
    if ((record.flags & ~(LXA_DOM_OPTION_SELECTED | LXA_DOM_OPTION_DIRTY)) != 0)
        return LXA_ERROR_ARGUMENT;
    if ((record.flags & LXA_DOM_OPTION_SELECTED) == 0) return LXA_OK;
    status = option_select(nodes, element, NULL, &select);
    if (status != LXA_OK || select.handle == LXA_HANDLE_NONE) return status;
    status = enabled_selectedcontent(nodes, select, LXA_HANDLE_NONE, &target);
    if (status != LXA_OK || target.handle == LXA_HANDLE_NONE) return status;
    return replace_children_with_clones(nodes, target, element, true);
}

lxa_status_t
lxa_dom_nodes_update_selectedcontent(lxa_dom_nodes_t *nodes,
                                     lxa_dom_ref_t select)
{
    lxa_dom_ref_t target;
    select_tag_t tag;
    select_reset_scan_t scan = {.kind = SELECT_SCAN_FIRST_SELECTED};
    lxa_status_t status;
    if (nodes == NULL) return LXA_ERROR_ARGUMENT;
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = select_tag(nodes, select, &tag);
    if (status != LXA_OK || tag != SELECT_SELECT)
        return status == LXA_OK ? LXA_ERROR_ARGUMENT : status;
    status = enabled_selectedcontent(nodes, select, LXA_HANDLE_NONE, &target);
    if (status != LXA_OK || target.handle == LXA_HANDLE_NONE) return status;
    status = scan_select_options(nodes, select, LXA_HANDLE_NONE, NULL, &scan);
    return status != LXA_OK ? status
        : replace_children_with_clones(nodes, target, scan.first, true);
}

static lxa_status_t
nearest_select(lxa_dom_nodes_t *nodes, lxa_dom_ref_t ancestor,
                 const select_view_t *view, lxa_dom_ref_t *out)
{
    size_t steps = 0, length = lxa_vector_length(&nodes->records);
    *out = (lxa_dom_ref_t){0};
    while (ancestor.handle != LXA_HANDLE_NONE) {
        select_tag_t tag;
        lxa_status_t status;
        if (++steps > length) return LXA_ERROR_ARGUMENT;
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                   &nodes->limit_reason);
        if (status == LXA_OK) status = select_tag(nodes, ancestor, &tag);
        if (status != LXA_OK) return status;
        if (tag == SELECT_SELECT) {
            *out = ancestor;
            return LXA_OK;
        }
        status = select_parent(nodes, ancestor, view, &ancestor);
        if (status != LXA_OK) return status;
    }
    return LXA_OK;
}

lxa_status_t
lxa_dom_nodes_removed_selectedcontent(lxa_dom_nodes_t *nodes,
    lxa_dom_ref_t element, lxa_dom_ref_t old_parent)
{
    lxa_dom_ref_t parent, select;
    lxa_dom_record_t record;
    select_tag_t tag;
    lxa_status_t status;
    if (nodes == NULL) return LXA_ERROR_ARGUMENT;
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = select_tag(nodes, element, &tag);
    if (status != LXA_OK || tag != SELECT_SELECTEDCONTENT)
        return status == LXA_OK ? LXA_ERROR_ARGUMENT : status;
    status = lxa_dom_nodes_read(nodes, element, &record);
    if (status != LXA_OK) return status;
    if ((record.flags & ~LXA_DOM_SELECTEDCONTENT_DISABLED) != 0)
        return LXA_ERROR_ARGUMENT;
    if ((record.flags & LXA_DOM_SELECTEDCONTENT_DISABLED) != 0) return LXA_OK;
    status = lxa_dom_nodes_parent(nodes, element, &parent);
    if (status == LXA_OK) status = nearest_select(nodes, parent, NULL, &select);
    if (status != LXA_OK || select.handle != LXA_HANDLE_NONE) return status;
    status = nearest_select(nodes, old_parent, NULL, &select);
    if (status != LXA_OK || select.handle == LXA_HANDLE_NONE) return status;
    return lxa_dom_nodes_update_selectedcontent(nodes, select);
}

lxa_status_t
lxa_dom_nodes_clear_nonprimary_selectedcontent(lxa_dom_nodes_t *nodes,
                                               lxa_dom_ref_t select)
{
    lxa_vector_t targets = {0};
    lxa_dom_ref_t cursor;
    select_tag_t tag;
    size_t visits = 0, ascents = 0, length;
    bool first = true;
    lxa_status_t status;
    if (nodes == NULL) return LXA_ERROR_ARGUMENT;
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = select_tag(nodes, select, &tag);
    if (status != LXA_OK || tag != SELECT_SELECT)
        return status == LXA_OK ? LXA_ERROR_ARGUMENT : status;
    length = lxa_vector_length(&nodes->records);
    status = lxa_vector_init(&targets, &nodes->allocator, sizeof(lxa_dom_ref_t));
    if (status == LXA_OK) status = lxa_vector_bind_budget(&targets, nodes->budget);
    if (status == LXA_OK) status = lxa_dom_nodes_first_child(nodes, select, &cursor);
    while (status == LXA_OK && cursor.handle != LXA_HANDLE_NONE) {
        if (++visits > length) { status = LXA_ERROR_ARGUMENT; break; }
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                   &nodes->limit_reason);
        if (status == LXA_OK) status = select_tag(nodes, cursor, &tag);
        if (status != LXA_OK) break;
        if (tag == SELECT_SELECTEDCONTENT) {
            lxa_dom_record_t record;
            status = lxa_dom_nodes_read(nodes, cursor, &record);
            if (status != LXA_OK) break;
            if ((record.flags & ~LXA_DOM_SELECTEDCONTENT_DISABLED) != 0) {
                status = LXA_ERROR_ARGUMENT;
                break;
            }
            if (first) first = false;
            else status = lxa_vector_push(&targets, &cursor);
            if (status != LXA_OK) break;
        }
        status = next_descendant(nodes, select, cursor, tag != SELECT_LEAF,
                                   &ascents, &cursor);
    }
    /* Collect before clearing: an earlier target can detach a later one. */
    for (size_t i = 0; status == LXA_OK && i < lxa_vector_length(&targets); ++i) {
        const lxa_dom_ref_t *refs = (const lxa_dom_ref_t *) targets.bytes.data;
        status = replace_children_with_clones(nodes, refs[i], (lxa_dom_ref_t){0}, true);
    }
    if (status == LXA_ERROR_LIMIT && nodes->limit_reason == LXA_BUDGET_COUNT)
        nodes->limit_reason = LXA_BUDGET_MEMORY;
    lxa_vector_destroy(&targets);
    return status;
}

lxa_status_t
lxa_dom_nodes_text_content(lxa_dom_nodes_t *nodes, lxa_dom_ref_t root,
    lxa_buffer_t *output, size_t max_output_bytes)
{
    const lxa_dom_record_t *records;
    lxa_dom_record_t record;
    lxa_id_t root_id, current;
    size_t start, visits = 0, length, max_visits;
    lxa_status_t status;
    if (nodes == NULL || output == NULL || output->length > max_output_bytes) {
        return LXA_ERROR_ARGUMENT;
    }
    nodes->limit_reason = LXA_BUDGET_COUNT;
    status = lxa_dom_nodes_read(nodes, root, &record);
    if (status != LXA_OK) {
        return status;
    }
    start = output->length;
    length = lxa_vector_length(&nodes->records);
    status = lxa_size_mul(length, 2, &max_visits);
    if (status != LXA_OK) {
        return status;
    }
    records = (const lxa_dom_record_t *) nodes->records.bytes.data;
    root_id = (lxa_id_t) root.handle;
    current = root_id;
    for (;;) {
        const lxa_dom_record_t *node;
        if (visits++ >= max_visits || current < nodes->retired
            || current >= length) {
            status = LXA_ERROR_ARGUMENT;
            break;
        }
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                    &nodes->limit_reason);
        if (status != LXA_OK) {
            break;
        }
        node = &records[current];
        if (node->generation != nodes->generation
            || !valid_kind((lxa_dom_kind_t) node->kind)) {
            status = LXA_ERROR_ARGUMENT;
            break;
        }
        if (node->kind == LXA_DOM_KIND_TEXT
            || (current == root_id && node->kind == LXA_DOM_KIND_COMMENT)) {
            lxa_dom_ref_t ref = {nodes, nodes->epoch, LXA_HANDLE_NONE};
            lxa_span_t text;
            size_t next;
            status = lxa_handle_pack(current, nodes->generation, &ref.handle);
            if (status == LXA_OK) {
                status = lxa_dom_nodes_character_read(nodes, ref, &text);
            }
            if (status == LXA_OK) {
                status = lxa_size_add(output->length, text.length, &next);
            }
            if (status != LXA_OK) {
                break;
            }
            if (next > max_output_bytes) {
                status = LXA_ERROR_LIMIT;
                break;
            }
            status = lxa_buffer_append(output, text);
            if (status != LXA_OK) {
                break;
            }
        }
        if (node->first_child != LXA_ID_NONE) {
            if (node->first_child >= length
                || records[node->first_child].parent != current) {
                status = LXA_ERROR_ARGUMENT;
                break;
            }
            current = node->first_child;
            continue;
        }
        while (current != root_id) {
            node = &records[current];
            if (node->next != LXA_ID_NONE) {
                if (node->next >= length
                    || records[node->next].parent != node->parent
                    || records[node->next].prev != current) {
                    status = LXA_ERROR_ARGUMENT;
                    goto done;
                }
                current = node->next;
                break;
            }
            current = node->parent;
            if (current < nodes->retired || current >= length
                || visits++ >= max_visits) {
                status = LXA_ERROR_ARGUMENT;
                goto done;
            }
            status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, 1,
                                        &nodes->limit_reason);
            if (status != LXA_OK) {
                goto done;
            }
        }
        if (current == root_id) {
            status = LXA_OK;
            break;
        }
    }
done:
    if (status != LXA_OK) {
        lxa_buffer_truncate(output, start);
    }
    return status;
}

lxa_status_t
lxa_dom_nodes_validate(const lxa_dom_nodes_t *nodes)
{
    const lxa_dom_record_t *records;
    const lxa_dom_character_t *payloads;
    const lxa_dom_pi_t *pis;
    const lxa_dom_element_t *elements;
    const lxa_dom_attr_t *attributes;
    const lxa_dom_doctype_t *doctypes;
    lxa_dom_atom_t *const *atoms;
    uint8_t *seen = NULL;
    size_t length, characters, element_count, attribute_count, atom_count;
    size_t doctype_count, doctype_seen, pi_count, pi_seen, scratch, i;
    lxa_status_t status;
    lxa_budget_t *budget;
    if (nodes == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    length = lxa_vector_length(&nodes->records);
    characters = lxa_vector_length(&nodes->characters);
    element_count = lxa_vector_length(&nodes->elements);
    attribute_count = lxa_vector_length(&nodes->attributes);
    atom_count = lxa_vector_length(&nodes->atoms);
    doctype_count = lxa_vector_length(&nodes->doctypes);
    pi_count = lxa_vector_length(&nodes->pis);
    if (nodes->retired > length) {
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_size_add(length, characters, &scratch);
    if (status == LXA_OK) {
        status = lxa_size_add(scratch, element_count, &scratch);
    }
    if (status == LXA_OK) {
        status = lxa_size_add(scratch, attribute_count, &scratch);
    }
    if (status == LXA_OK) {
        status = lxa_size_add(scratch, atom_count, &scratch);
    }
    if (status == LXA_OK) {
        status = lxa_size_add(scratch, attribute_count, &scratch);
    }
    doctype_seen = scratch;
    if (status == LXA_OK) {
        status = lxa_size_add(scratch, doctype_count, &scratch);
    }
    pi_seen = scratch;
    if (status == LXA_OK) {
        status = lxa_size_add(scratch, pi_count, &scratch);
    }
    if (status != LXA_OK) {
        return status;
    }
    budget = nodes->budget;
    status = lxa_budget_charge(budget, LXA_BUDGET_MEMORY, scratch, NULL);
    if (status != LXA_OK) {
        return status;
    }
    if (scratch != 0) {
        status = lxa_calloc(&nodes->allocator, scratch, 1, (void **) &seen);
        if (status != LXA_OK) {
            lxa_budget_release(budget, LXA_BUDGET_MEMORY, scratch);
            return status;
        }
    }
    records = (const lxa_dom_record_t *) nodes->records.bytes.data;
    payloads = (const lxa_dom_character_t *) nodes->characters.bytes.data;
    pis = (const lxa_dom_pi_t *) nodes->pis.bytes.data;
    elements = (const lxa_dom_element_t *) nodes->elements.bytes.data;
    attributes = (const lxa_dom_attr_t *) nodes->attributes.bytes.data;
    doctypes = (const lxa_dom_doctype_t *) nodes->doctypes.bytes.data;
    atoms = (lxa_dom_atom_t *const *) nodes->atoms.bytes.data;
    status = LXA_ERROR_ARGUMENT;
    for (i = 0; i < lxa_vector_length(&nodes->templates); ++i) {
        const lxa_dom_template_t *entries =
            (const lxa_dom_template_t *) nodes->templates.bytes.data;
        const lxa_dom_template_t *pair = &entries[i];
        const lxa_dom_record_t *host, *fragment;
        const lxa_dom_element_t *element;
        const lxa_dom_atom_t *atom;
        size_t j;
        lxa_status_t visit = lxa_budget_charge(budget, LXA_BUDGET_WORK, 1,
                                                NULL);
        if (visit != LXA_OK) { status = visit; goto done; }
        if (pair->element < nodes->retired || pair->element >= length
            || pair->fragment < nodes->retired || pair->fragment >= length)
            goto done;
        host = &records[pair->element];
        fragment = &records[pair->fragment];
        if (host->kind != LXA_DOM_KIND_ELEMENT
            || host->payload >= element_count
            || host->first_child != LXA_ID_NONE
            || host->last_child != LXA_ID_NONE
            || fragment->kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT
            || fragment->parent != LXA_ID_NONE
            || fragment->prev != LXA_ID_NONE
            || fragment->next != LXA_ID_NONE)
            goto done;
        element = &elements[host->payload];
        if (element->namespace_id != LXA_DOM_NS_HTML
            || element->atom >= atom_count
            || (atom = atoms[element->atom]) == NULL
            || atom->offset > nodes->strings.length
            || atom->length != sizeof("template") - 1
            || atom->length > nodes->strings.length - atom->offset
            || memcmp(nodes->strings.data + atom->offset, "template",
                      sizeof("template") - 1) != 0)
            goto done;
        for (j = 0; j < i; ++j) {
            visit = lxa_budget_charge(budget, LXA_BUDGET_WORK, 1, NULL);
            if (visit != LXA_OK) { status = visit; goto done; }
            if (entries[j].element == pair->element
                || entries[j].fragment == pair->fragment)
                goto done;
        }
    }
    for (i = 0; i < lxa_vector_length(&nodes->templates); ++i) {
        const lxa_dom_template_t *entries =
            (const lxa_dom_template_t *) nodes->templates.bytes.data;
        status = check_virtual_ancestor(nodes, entries[i].element,
                                        entries[i].fragment, NULL);
        if (status != LXA_OK) goto done;
    }
    for (i = nodes->retired; i < length; ++i) {
        const lxa_dom_record_t *node = &records[i];
        size_t steps = 0, document_elements = 0, document_doctypes = 0;
        lxa_id_t child, previous = LXA_ID_NONE;
        lxa_status_t visit = lxa_budget_charge(budget, LXA_BUDGET_WORK, 1, NULL);
        if (visit != LXA_OK) {
            status = visit;
            goto done;
        }
        if (node->generation != nodes->generation
            || !valid_kind((lxa_dom_kind_t) node->kind)
            || (node->flags & ~(LXA_DOM_OPTION_SELECTED | LXA_DOM_OPTION_DIRTY
                                | LXA_DOM_SELECTEDCONTENT_DISABLED))
               != 0
            || (node->flags != 0
                && (node->kind != LXA_DOM_KIND_ELEMENT
                    || node->payload == LXA_ID_NONE))) {
            goto done;
        }
        if (node->payload != LXA_ID_NONE) {
            const lxa_dom_character_t *payload;
            if (node->kind == LXA_DOM_KIND_ELEMENT) {
                const lxa_dom_element_t *element;
                if (node->payload >= element_count
                    || seen[length + characters + node->payload] != 0) {
                    goto done;
                }
                seen[length + characters + node->payload] = 1;
                element = &elements[node->payload];
                if (element->atom >= atom_count
                    || atoms[element->atom] == NULL
                    || element->namespace_id < LXA_DOM_NS_HTML
                    || element->namespace_id > LXA_DOM_NS_SVG
                    || (element->first_attr == LXA_ID_NONE)
                       != (element->attr_count == 0)) {
                    goto done;
                }
                if (node->flags != 0) {
                    const lxa_dom_atom_t *atom = atoms[element->atom];
                    if (element->namespace_id != LXA_DOM_NS_HTML
                        || atom->offset > nodes->strings.length
                        || atom->length > nodes->strings.length - atom->offset)
                        goto done;
                    if (atom->length == 6
                        && memcmp(nodes->strings.data + atom->offset, "option", 6) == 0) {
                        if ((node->flags & ~(LXA_DOM_OPTION_SELECTED | LXA_DOM_OPTION_DIRTY))
                            != 0) goto done;
                    }
                    else if (atom->length != 15
                        || memcmp(nodes->strings.data + atom->offset, "selectedcontent", 15)
                           != 0 || node->flags != LXA_DOM_SELECTEDCONTENT_DISABLED)
                        goto done;
                }
                seen[length + characters + element_count + attribute_count
                     + element->atom] = 1;
                {
                    lxa_id_t cursor = element->first_attr;
                    size_t linked = 0;
                    while (cursor != LXA_ID_NONE) {
                        const lxa_dom_record_t *ref;
                        const lxa_dom_attr_t *attr;
                        visit = lxa_budget_charge(budget, LXA_BUDGET_WORK,
                                                   1, NULL);
                        if (visit != LXA_OK) {
                            status = visit;
                            goto done;
                        }
                        if (linked >= element->attr_count
                            || cursor < nodes->retired || cursor >= length) {
                            goto done;
                        }
                        ref = &records[cursor];
                        if (ref->kind != LXA_DOM_KIND_ATTRIBUTE
                            || ref->payload >= attribute_count
                            || seen[length + characters + element_count
                                    + attribute_count + atom_count
                                    + ref->payload] != 0) {
                            goto done;
                        }
                        attr = &attributes[ref->payload];
                        if (attr->owner != i) {
                            goto done;
                        }
                        seen[length + characters + element_count
                             + attribute_count + atom_count
                             + ref->payload] = 1;
                        cursor = attr->next;
                        ++linked;
                    }
                    if (linked != element->attr_count) {
                        goto done;
                    }
                }
            }
            else if (node->kind == LXA_DOM_KIND_ATTRIBUTE) {
                const lxa_dom_attr_t *attr;
                if (node->payload >= attribute_count
                    || seen[length + characters + element_count
                            + node->payload] != 0) {
                    goto done;
                }
                seen[length + characters + element_count
                     + node->payload] = 1;
                attr = &attributes[node->payload];
                if (attr->name_length == 0
                    || attr->namespace_id > LXA_DOM_ATTR_NS_XMLNS
                    || (attr->owner == LXA_ID_NONE
                        ? attr->next != LXA_ID_NONE
                        : (attr->owner < nodes->retired
                           || attr->owner >= length
                           || records[attr->owner].kind
                              != LXA_DOM_KIND_ELEMENT))
                    || attr->name_offset > nodes->strings.length
                    || attr->name_length > nodes->strings.length - attr->name_offset
                    || attr->value_offset > nodes->strings.length
                    || attr->value_length > nodes->strings.length - attr->value_offset) {
                    goto done;
                }
            }
            else if (node->kind == LXA_DOM_KIND_DOCUMENT_TYPE) {
                const lxa_dom_doctype_t *doctype;
                if (node->payload >= doctype_count
                    || seen[doctype_seen + node->payload] != 0) {
                    goto done;
                }
                seen[doctype_seen + node->payload] = 1;
                doctype = &doctypes[node->payload];
                if (doctype->name_offset > nodes->strings.length
                    || doctype->name_length > nodes->strings.length
                                            - doctype->name_offset
                    || doctype->public_offset > nodes->strings.length
                    || doctype->public_length > nodes->strings.length
                                              - doctype->public_offset
                    || doctype->system_offset > nodes->strings.length
                    || doctype->system_length > nodes->strings.length
                                              - doctype->system_offset
                    || (!doctype->has_public && doctype->public_length != 0)
                    || (!doctype->has_system && doctype->system_length != 0)) {
                    goto done;
                }
            }
            else if (node->kind == LXA_DOM_KIND_TEXT
                     || node->kind == LXA_DOM_KIND_COMMENT) {
                if (node->payload >= characters
                    || seen[length + node->payload] != 0) {
                    goto done;
                }
                seen[length + node->payload] = 1;
                payload = &payloads[node->payload];
                if (payload->offset > nodes->strings.length
                    || payload->length > nodes->strings.length - payload->offset) {
                    goto done;
                }
            }
            else if (node->kind == LXA_DOM_KIND_PROCESSING_INSTRUCTION) {
                const lxa_dom_pi_t *pi;
                if (node->payload >= pi_count
                    || seen[pi_seen + node->payload] != 0)
                    goto done;
                seen[pi_seen + node->payload] = 1;
                pi = &pis[node->payload];
                if (pi->target.length == 0
                    || pi->target.offset > nodes->strings.length
                    || pi->target.length > nodes->strings.length
                                               - pi->target.offset
                    || pi->data.offset > nodes->strings.length
                    || pi->data.length > nodes->strings.length
                                             - pi->data.offset)
                    goto done;
            }
            else {
                goto done;
            }
        }
        if (node->parent == LXA_ID_NONE) {
            if (node->prev != LXA_ID_NONE || node->next != LXA_ID_NONE) {
                goto done;
            }
        }
        else if (node->parent < nodes->retired || node->parent >= length
                 || (node->kind != LXA_DOM_KIND_TEXT
                     && node->kind != LXA_DOM_KIND_COMMENT
                     && node->kind != LXA_DOM_KIND_PROCESSING_INSTRUCTION
                     && node->kind != LXA_DOM_KIND_ELEMENT
                     && node->kind != LXA_DOM_KIND_DOCUMENT_TYPE)
                 || node->payload == LXA_ID_NONE
                 || (records[node->parent].kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT
                     && records[node->parent].kind != LXA_DOM_KIND_ELEMENT
                     && records[node->parent].kind != LXA_DOM_KIND_DOCUMENT)
                 || (records[node->parent].kind == LXA_DOM_KIND_DOCUMENT
                     && node->kind != LXA_DOM_KIND_ELEMENT
                     && node->kind != LXA_DOM_KIND_COMMENT
                     && node->kind != LXA_DOM_KIND_PROCESSING_INSTRUCTION
                     && node->kind != LXA_DOM_KIND_DOCUMENT_TYPE)
                 || (node->kind == LXA_DOM_KIND_DOCUMENT_TYPE
                     && records[node->parent].kind != LXA_DOM_KIND_DOCUMENT)
                 || (records[node->parent].kind == LXA_DOM_KIND_ELEMENT
                     && records[node->parent].payload == LXA_ID_NONE)) {
            goto done;
        }
        if ((node->kind == LXA_DOM_KIND_ELEMENT
             && node->payload == LXA_ID_NONE
             && node->first_child != LXA_ID_NONE)
            || (node->kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT
                && node->kind != LXA_DOM_KIND_DOCUMENT
                && node->kind != LXA_DOM_KIND_ELEMENT
                && (node->first_child != LXA_ID_NONE
                    || node->last_child != LXA_ID_NONE))) {
            goto done;
        }
        if (node->kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT
            && node->kind != LXA_DOM_KIND_DOCUMENT
            && node->kind != LXA_DOM_KIND_ELEMENT) {
            continue;
        }
        if ((node->first_child == LXA_ID_NONE)
            != (node->last_child == LXA_ID_NONE)) {
            goto done;
        }
        child = node->first_child;
        while (child != LXA_ID_NONE) {
            const lxa_dom_record_t *entry;
            visit = lxa_budget_charge(budget, LXA_BUDGET_WORK, 1, NULL);
            if (visit != LXA_OK) {
                status = visit;
                goto done;
            }
            if (child < nodes->retired || child >= length
                || seen[child] != 0 || ++steps > length - nodes->retired) {
                goto done;
            }
            seen[child] = 1;
            entry = &records[child];
            if (node->kind == LXA_DOM_KIND_DOCUMENT
                && ((entry->kind != LXA_DOM_KIND_ELEMENT
                     && entry->kind != LXA_DOM_KIND_COMMENT
                     && entry->kind != LXA_DOM_KIND_PROCESSING_INSTRUCTION
                     && entry->kind != LXA_DOM_KIND_DOCUMENT_TYPE)
                    || (entry->kind == LXA_DOM_KIND_ELEMENT
                        && ++document_elements > 1)
                    || (entry->kind == LXA_DOM_KIND_DOCUMENT_TYPE
                        && (document_elements != 0
                            || ++document_doctypes > 1)))) {
                goto done;
            }
            if (entry->parent != i
                || entry->prev != previous
                || (entry->next == LXA_ID_NONE) != (node->last_child == child)) {
                goto done;
            }
            if (entry->prev != LXA_ID_NONE
                && (entry->prev < nodes->retired || entry->prev >= length
                    || records[entry->prev].next != child)) {
                goto done;
            }
            if (entry->next != LXA_ID_NONE
                && (entry->next < nodes->retired || entry->next >= length
                    || records[entry->next].prev != child)) {
                goto done;
            }
            previous = child;
            child = entry->next;
        }
    }
    for (i = nodes->retired; i < length; ++i) {
        lxa_status_t visit = lxa_budget_charge(budget, LXA_BUDGET_WORK, 1, NULL);
        if (visit != LXA_OK) {
            status = visit;
            goto done;
        }
        if ((records[i].parent != LXA_ID_NONE) != (seen[i] != 0)) {
            goto done;
        }
    }
    for (i = 0; i < characters; ++i) {
        lxa_status_t visit = lxa_budget_charge(budget, LXA_BUDGET_WORK, 1, NULL);
        if (visit != LXA_OK) {
            status = visit;
            goto done;
        }
        if (seen[length + i] == 0) {
            goto done;
        }
    }
    for (i = 0; i < pi_count; ++i) {
        lxa_status_t visit = lxa_budget_charge(budget, LXA_BUDGET_WORK, 1, NULL);
        if (visit != LXA_OK) {
            status = visit;
            goto done;
        }
        if (seen[pi_seen + i] == 0) goto done;
    }
    for (i = 0; i < element_count; ++i) {
        lxa_status_t visit = lxa_budget_charge(budget, LXA_BUDGET_WORK, 1, NULL);
        if (visit != LXA_OK) {
            status = visit;
            goto done;
        }
        if (seen[length + characters + i] == 0) {
            goto done;
        }
    }
    for (i = 0; i < attribute_count; ++i) {
        lxa_status_t visit = lxa_budget_charge(budget, LXA_BUDGET_WORK, 1, NULL);
        if (visit != LXA_OK) {
            status = visit;
            goto done;
        }
        if (seen[length + characters + element_count + i] == 0) {
            goto done;
        }
        if ((attributes[i].owner != LXA_ID_NONE)
            != (seen[length + characters + element_count + attribute_count
                     + atom_count + i] != 0)) {
            goto done;
        }
    }
    for (i = 0; i < atom_count; ++i) {
        bool found;
        void *value;
        lxa_span_t key;
        lxa_status_t visit = lxa_budget_charge(budget, LXA_BUDGET_WORK, 1, NULL);
        if (visit != LXA_OK) {
            status = visit;
            goto done;
        }
        if (seen[length + characters + element_count + attribute_count + i] == 0
            || atoms[i] == NULL || atoms[i]->id != i
            || atoms[i]->offset > nodes->strings.length
            || atoms[i]->length == 0
            || atoms[i]->length > nodes->strings.length - atoms[i]->offset) {
            goto done;
        }
        key.data = nodes->strings.data + atoms[i]->offset;
        key.length = atoms[i]->length;
        visit = lxa_table_find(nodes->atom_lookup, key, &found, &value);
        if (visit != LXA_OK) {
            status = visit;
            goto done;
        }
        if (!found || value != atoms[i]) {
            goto done;
        }
    }
    for (i = 0; i < doctype_count; ++i) {
        lxa_status_t visit = lxa_budget_charge(budget, LXA_BUDGET_WORK, 1, NULL);
        if (visit != LXA_OK) {
            status = visit;
            goto done;
        }
        if (seen[doctype_seen + i] == 0) {
            goto done;
        }
    }
    status = LXA_OK;
done:
    lxa_free(&nodes->allocator, seen);
    lxa_budget_release(budget, LXA_BUDGET_MEMORY, scratch);
    return status;
}
