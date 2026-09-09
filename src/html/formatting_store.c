#include <assert.h>
#include <string.h>
#include "html/formatting_store.h"
#include "html/formatting_list.h"
#include "html/formatting_marker.h"
#include "html/tree_categories.h"

static lxa_status_t
internal(lxa_status_t status)
{
    if (status != LXA_OK) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    return LXA_OK;
}

lxa_status_t
lxa_html_formatting_store_init(lxa_html_formatting_store_t *store,
    const lxa_allocator_t *allocator, lxa_budget_t *budget)
{
    if (store == NULL) return LXA_ERROR_ARGUMENT;
    memset(store, 0, sizeof(*store));
    store->limit_reason = LXA_BUDGET_COUNT;
    if (budget == NULL) return LXA_ERROR_ARGUMENT;
    lxa_status_t status = lxa_vector_init(&store->snapshots, allocator,
                                         sizeof(lxa_html_formatting_snapshot_t));
    if (status == LXA_OK)
        status = lxa_vector_init(&store->attributes, allocator,
                                 sizeof(lxa_html_formatting_attribute_t));
    if (status == LXA_OK) status = lxa_buffer_init(&store->bytes, allocator);
    if (status == LXA_OK) status = lxa_vector_bind_budget(&store->snapshots, budget);
    if (status == LXA_OK) status = lxa_vector_bind_budget(&store->attributes, budget);
    if (status == LXA_OK) status = lxa_buffer_bind_budget(&store->bytes, budget);
    if (status != LXA_OK) {
        lxa_html_formatting_store_destroy(store);
        return status;
    }
    store->budget = budget;
    return LXA_OK;
}

void
lxa_html_formatting_store_destroy(lxa_html_formatting_store_t *store)
{
    if (store == NULL) return;
    lxa_vector_destroy(&store->snapshots);
    lxa_vector_destroy(&store->attributes);
    lxa_buffer_destroy(&store->bytes);
    memset(store, 0, sizeof(*store));
    store->limit_reason = LXA_BUDGET_COUNT;
}

static bool
valid_vector(const lxa_vector_t *vector, size_t element_size, lxa_budget_t *budget)
{
    return vector->element_size == element_size
        && vector->bytes.length <= vector->bytes.capacity
        && vector->bytes.length % element_size == 0
        && vector->bytes.budget == budget
        && (vector->bytes.length == 0 || vector->bytes.data != NULL);
}

static lxa_status_t
begin(lxa_html_formatting_store_t *store)
{
    if (store == NULL) return LXA_ERROR_ARGUMENT;
    store->limit_reason = LXA_BUDGET_COUNT;
    if (store->budget == NULL) return LXA_ERROR_ARGUMENT;
    if (!valid_vector(&store->snapshots, sizeof(lxa_html_formatting_snapshot_t), store->budget)
        || !valid_vector(&store->attributes, sizeof(lxa_html_formatting_attribute_t), store->budget)
        || store->bytes.length > store->bytes.capacity
        || store->bytes.budget != store->budget
        || (store->bytes.length != 0 && store->bytes.data == NULL))
        return internal(LXA_ERROR_INTERNAL);
    return LXA_OK;
}

static bool
overlaps_buffer(lxa_span_t span, const lxa_buffer_t *buffer)
{
    if (span.length == 0 || buffer->data == NULL || buffer->capacity == 0) return false;
    uintptr_t source = (uintptr_t) span.data, base = (uintptr_t) buffer->data;
    return source >= base ? source - base < buffer->capacity
                          : base - source < span.length;
}

static bool
overlaps_store(lxa_span_t span, const lxa_html_formatting_store_t *store)
{
    return overlaps_buffer(span, &store->bytes)
        || overlaps_buffer(span, &store->snapshots.bytes)
        || overlaps_buffer(span, &store->attributes.bytes);
}

static lxa_status_t
copy_work(lxa_html_formatting_store_t *store, size_t length)
{
    return lxa_budget_charge(store->budget, LXA_BUDGET_WORK,
        length / 8 + (length % 8 != 0), &store->limit_reason);
}

static lxa_status_t
reserve_status(lxa_html_formatting_store_t *store, lxa_status_t status)
{
    if (status == LXA_ERROR_LIMIT) store->limit_reason = LXA_BUDGET_MEMORY;
    return status;
}

static lxa_status_t
rollback(lxa_html_formatting_store_t *store, size_t snapshots, size_t attributes,
          size_t bytes)
{
    lxa_status_t a = lxa_buffer_truncate(&store->snapshots.bytes, snapshots);
    lxa_status_t b = lxa_buffer_truncate(&store->attributes.bytes, attributes);
    lxa_status_t c = lxa_buffer_truncate(&store->bytes, bytes);
    return internal(a != LXA_OK ? a : b != LXA_OK ? b : c);
}

lxa_status_t
lxa_html_formatting_store_capture(lxa_html_formatting_store_t *store,
    const lxa_html_token_t *token, size_t *reference)
{
    if (reference != NULL) *reference = 0;
    lxa_status_t status = begin(store);
    if (status != LXA_OK) return status;
    if (reference == NULL || !lxa_html_formatting_token_shape_valid(token)
        || !lxa_html_tree_formatting_tag(token->name))
        return LXA_ERROR_ARGUMENT;
    lxa_span_t descriptors = {(const uint8_t *) token->attributes,
        token->attribute_count * sizeof(*token->attributes)};
    lxa_span_t token_storage = {(const uint8_t *) token, sizeof(*token)};
    if (overlaps_store(token_storage, store) || overlaps_store(token->name, store)
        || overlaps_store(descriptors, store))
        return LXA_ERROR_ARGUMENT;
    status = lxa_budget_check_item(store->budget, LXA_BUDGET_NAME_BYTES,
                                   token->name.length, &store->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_check_item(store->budget, LXA_BUDGET_ATTRS_PER_ELEMENT,
                                       token->attribute_count, &store->limit_reason);
    if (status != LXA_OK) return status;
    size_t count, attributes, total;
    size_t old_snapshots = store->snapshots.bytes.length;
    size_t old_attributes = store->attributes.bytes.length;
    size_t old_bytes = store->bytes.length;
    status = lxa_size_add(lxa_vector_length(&store->snapshots), 1, &count);
    if (status == LXA_OK)
        status = lxa_size_add(lxa_vector_length(&store->attributes),
                              token->attribute_count, &attributes);
    if (status == LXA_OK) status = lxa_size_add(old_bytes, token->name.length, &total);
    if (status == LXA_OK) status = copy_work(store, token->name.length);
    if (status != LXA_OK) return status;
    for (size_t i = 0; i < token->attribute_count; ++i) {
        const lxa_html_attribute_t *attribute = &token->attributes[i];
        status = lxa_budget_charge(store->budget, LXA_BUDGET_WORK, 1, &store->limit_reason);
        if (status != LXA_OK) return status;
        if (!lxa_dom_attribute_shape_valid(attribute->name, attribute->value))
            return LXA_ERROR_ARGUMENT;
        status = lxa_budget_check_item(store->budget, LXA_BUDGET_NAME_BYTES,
                                       attribute->name.length, &store->limit_reason);
        if (status == LXA_OK) status = lxa_size_add(total, attribute->name.length, &total);
        if (status == LXA_OK) status = lxa_size_add(total, attribute->value.length, &total);
        if (status != LXA_OK) return status;
        if (overlaps_store(attribute->name, store) || overlaps_store(attribute->value, store))
            return LXA_ERROR_ARGUMENT;
        status = copy_work(store, attribute->name.length);
        if (status == LXA_OK) status = copy_work(store, attribute->value.length);
        if (status != LXA_OK) return status;
    }
    status = lxa_budget_check(store->budget, LXA_BUDGET_STRINGS,
                              total - old_bytes, &store->limit_reason);
    if (status != LXA_OK) return status;
    status = reserve_status(store, lxa_vector_reserve(&store->snapshots, count));
    if (status == LXA_OK)
        status = reserve_status(store, lxa_vector_reserve(&store->attributes, attributes));
    if (status == LXA_OK)
        status = reserve_status(store, lxa_buffer_reserve(&store->bytes, total));
    if (status == LXA_OK)
        status = lxa_budget_check(store->budget, LXA_BUDGET_STRINGS,
                                  total - old_bytes, &store->limit_reason);
    if (status != LXA_OK) return status;
    lxa_html_formatting_snapshot_t snapshot = {
        old_bytes, token->name.length, lxa_vector_length(&store->attributes),
        token->attribute_count
    };
    status = lxa_buffer_append(&store->bytes, token->name);
    for (size_t i = 0; status == LXA_OK && i < token->attribute_count; ++i) {
        const lxa_html_attribute_t *attribute = &token->attributes[i];
        lxa_html_formatting_attribute_t stored = {
            store->bytes.length, attribute->name.length, 0, attribute->value.length
        };
        status = lxa_buffer_append(&store->bytes, attribute->name);
        stored.value_offset = store->bytes.length;
        if (status == LXA_OK) status = lxa_buffer_append(&store->bytes, attribute->value);
        if (status == LXA_OK) status = lxa_vector_push(&store->attributes, &stored);
    }
    if (status == LXA_OK) status = lxa_vector_push(&store->snapshots, &snapshot);
    if (status == LXA_OK)
        status = lxa_budget_charge(store->budget, LXA_BUDGET_STRINGS,
                                   total - old_bytes, &store->limit_reason);
    if (status != LXA_OK) {
        (void) rollback(store, old_snapshots, old_attributes, old_bytes);
        return internal(status);
    }
    *reference = count;
    return LXA_OK;
}

static lxa_status_t
snapshot_get(const lxa_html_formatting_store_t *store, size_t reference,
              const lxa_html_formatting_snapshot_t **out)
{
    if (reference == 0 || reference > lxa_vector_length(&store->snapshots))
        return LXA_ERROR_ARGUMENT;
    const lxa_html_formatting_snapshot_t *snapshot =
        (const lxa_html_formatting_snapshot_t *) store->snapshots.bytes.data + reference - 1;
    size_t count = lxa_vector_length(&store->attributes);
    if (snapshot->name_length == 0 || snapshot->name_offset > store->bytes.length
        || snapshot->name_length > store->bytes.length - snapshot->name_offset
        || snapshot->attribute_start > count
        || snapshot->attribute_count > count - snapshot->attribute_start)
        return internal(LXA_ERROR_INTERNAL);
    *out = snapshot;
    return LXA_OK;
}

static lxa_status_t
attribute_get(const lxa_html_formatting_store_t *store,
    const lxa_html_formatting_snapshot_t *snapshot, size_t index, lxa_html_attribute_t *out)
{
    if (index >= snapshot->attribute_count) return LXA_ERROR_ARGUMENT;
    const lxa_html_formatting_attribute_t *attribute =
        (const lxa_html_formatting_attribute_t *) store->attributes.bytes.data
        + snapshot->attribute_start + index;
    if (attribute->name_length == 0 || attribute->name_offset > store->bytes.length
        || attribute->name_length > store->bytes.length - attribute->name_offset
        || attribute->value_offset > store->bytes.length
        || attribute->value_length > store->bytes.length - attribute->value_offset)
        return internal(LXA_ERROR_INTERNAL);
    *out = (lxa_html_attribute_t){
        {store->bytes.data + attribute->name_offset, attribute->name_length},
        {store->bytes.data + attribute->value_offset, attribute->value_length}
    };
    return LXA_OK;
}

lxa_status_t
lxa_html_formatting_store_get(lxa_html_formatting_store_t *store,
    size_t reference, lxa_span_t *name, size_t *attribute_count)
{
    if (name != NULL) *name = (lxa_span_t){NULL, 0};
    if (attribute_count != NULL) *attribute_count = 0;
    lxa_status_t status = begin(store);
    if (status != LXA_OK) return status;
    if (name == NULL || attribute_count == NULL) return LXA_ERROR_ARGUMENT;
    const lxa_html_formatting_snapshot_t *snapshot;
    status = snapshot_get(store, reference, &snapshot);
    if (status == LXA_OK)
        status = lxa_budget_charge(store->budget, LXA_BUDGET_WORK, 1, &store->limit_reason);
    if (status == LXA_OK) {
        *name = (lxa_span_t){store->bytes.data + snapshot->name_offset, snapshot->name_length};
        *attribute_count = snapshot->attribute_count;
    }
    return status;
}

lxa_status_t
lxa_html_formatting_store_attribute(lxa_html_formatting_store_t *store,
    size_t reference, size_t index, lxa_html_attribute_t *out)
{
    if (out != NULL) memset(out, 0, sizeof(*out));
    lxa_status_t status = begin(store);
    if (status != LXA_OK) return status;
    if (out == NULL) return LXA_ERROR_ARGUMENT;
    const lxa_html_formatting_snapshot_t *snapshot;
    status = snapshot_get(store, reference, &snapshot);
    if (status != LXA_OK) return status;
    if (index >= snapshot->attribute_count) return LXA_ERROR_ARGUMENT;
    status = lxa_budget_charge(store->budget, LXA_BUDGET_WORK, 1, &store->limit_reason);
    return status == LXA_OK ? attribute_get(store, snapshot, index, out) : status;
}

typedef struct {
    lxa_html_formatting_store_t *store;
    const lxa_html_formatting_snapshot_t *snapshot;
} formatting_store_view_t;

static inline lxa_status_t
store_view_name(const formatting_store_view_t *view, lxa_span_t *name)
{
    *name = (lxa_span_t){view->store->bytes.data + view->snapshot->name_offset,
                       view->snapshot->name_length};
    return LXA_OK;
}

static inline lxa_status_t
store_view_next(const formatting_store_view_t *view, size_t cursor, size_t *out)
{
    *out = cursor == view->snapshot->attribute_count ? 0 : cursor + 1;
    return LXA_OK;
}

static inline lxa_status_t
store_view_read(const formatting_store_view_t *view, size_t cursor,
                 lxa_span_t *name, lxa_span_t *value)
{
    lxa_html_attribute_t attribute;
    lxa_status_t status = attribute_get(view->store, view->snapshot, cursor - 1, &attribute);
    if (status == LXA_OK) {
        *name = attribute.name;
        *value = attribute.value;
    }
    return status;
}

#define LXA_FORMATTING_EQUAL_NAME store_view_equal
#define LXA_FORMATTING_VIEW formatting_store_view_t
#define LXA_FORMATTING_CURSOR size_t
#define LXA_FORMATTING_DONE(cursor) ((cursor) == 0)
#define LXA_FORMATTING_NAME store_view_name
#define LXA_FORMATTING_NEXT store_view_next
#define LXA_FORMATTING_READ store_view_read
#define LXA_FORMATTING_BUDGET(view) ((view)->store->budget)
#define LXA_FORMATTING_REASON(view) (&(view)->store->limit_reason)
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

lxa_status_t
lxa_html_formatting_store_equal(lxa_html_formatting_store_t *store,
    size_t reference, const lxa_html_token_t *token, bool *out)
{
    lxa_status_t status = begin(store);
    if (status != LXA_OK) return status;
    if (out == NULL) return LXA_ERROR_ARGUMENT;
    const lxa_html_formatting_snapshot_t *snapshot;
    status = snapshot_get(store, reference, &snapshot);
    if (status != LXA_OK) return status;
    const formatting_store_view_t view = {store, snapshot};
    return store_view_equal(&view, token, out);
}

typedef struct {
    lxa_html_formatting_store_t *store;
    const size_t *references;
    size_t length;
} formatting_store_list_view_t;

static lxa_status_t
store_list_view_init(lxa_html_formatting_store_t *store, const size_t *references,
    size_t length, size_t start, size_t *out, formatting_store_list_view_t *view)
{
    lxa_status_t status = begin(store);
    if (status != LXA_OK) return status;
    if (out == NULL || (length != 0 && references == NULL)
        || length > SIZE_MAX / sizeof(*references) || start > length)
        return LXA_ERROR_ARGUMENT;
    *view = (formatting_store_list_view_t){store, references, length};
    return LXA_OK;
}

static inline lxa_status_t
store_list_name(const formatting_store_list_view_t *view, size_t index, lxa_span_t *name)
{
    const lxa_html_formatting_snapshot_t *snapshot;
    lxa_status_t status = snapshot_get(view->store, view->references[index], &snapshot);
    if (status == LXA_OK)
        *name = (lxa_span_t){view->store->bytes.data + snapshot->name_offset,
                           snapshot->name_length};
    return status;
}

static inline lxa_status_t
store_list_equal(const formatting_store_list_view_t *view, size_t index,
                  const lxa_html_token_t *token, bool *out)
{
    const lxa_html_formatting_snapshot_t *snapshot;
    lxa_status_t status = snapshot_get(view->store, view->references[index], &snapshot);
    if (status != LXA_OK) return status;
    const formatting_store_view_t item = {view->store, snapshot};
    return store_view_equal(&item, token, out);
}

#define LXA_NOAH_PLAN_NAME store_noah_plan
#define LXA_NOAH_VIEW formatting_store_list_view_t
#define LXA_NOAH_LENGTH(view) ((view)->length)
#define LXA_NOAH_EQUAL store_list_equal
#define LXA_NOAH_BUDGET(view) ((view)->store->budget)
#define LXA_NOAH_REASON(view) (&(view)->store->limit_reason)
#include "html/formatting_noah.inc"
#undef LXA_NOAH_PLAN_NAME
#undef LXA_NOAH_VIEW
#undef LXA_NOAH_LENGTH
#undef LXA_NOAH_EQUAL
#undef LXA_NOAH_BUDGET
#undef LXA_NOAH_REASON

#define LXA_FORMATTING_FIND_NAME store_find
#define LXA_FORMATTING_FIND_VIEW formatting_store_list_view_t
#define LXA_FORMATTING_FIND_LENGTH(view) ((view)->length)
#define LXA_FORMATTING_FIND_READ store_list_name
#define LXA_FORMATTING_FIND_BUDGET(view) ((view)->store->budget)
#define LXA_FORMATTING_FIND_REASON(view) (&(view)->store->limit_reason)
#include "html/formatting_find.inc"
#undef LXA_FORMATTING_FIND_NAME
#undef LXA_FORMATTING_FIND_VIEW
#undef LXA_FORMATTING_FIND_LENGTH
#undef LXA_FORMATTING_FIND_READ
#undef LXA_FORMATTING_FIND_BUDGET
#undef LXA_FORMATTING_FIND_REASON

lxa_status_t
lxa_html_formatting_store_noah_plan(lxa_html_formatting_store_t *store,
    const size_t *references, size_t length, size_t start,
    const lxa_html_token_t *token, size_t *drop)
{
    formatting_store_list_view_t view;
    lxa_status_t status = store_list_view_init(store, references, length, start, drop, &view);
    return status == LXA_OK ? store_noah_plan(&view, start, token, drop) : status;
}

lxa_status_t
lxa_html_formatting_store_find(lxa_html_formatting_store_t *store,
    const size_t *references, size_t length, size_t start, lxa_span_t target,
    size_t *index)
{
    formatting_store_list_view_t view;
    lxa_status_t status = store_list_view_init(store, references, length, start, index, &view);
    if (status != LXA_OK) return status;
    if (target.data == NULL || target.length == 0) return LXA_ERROR_ARGUMENT;
    return store_find(&view, start, target, index);
}

lxa_status_t
lxa_html_formatting_list_init(lxa_html_formatting_list_t *list,
    lxa_html_formatting_store_t *store)
{
    if (list == NULL) return LXA_ERROR_ARGUMENT;
    memset(list, 0, sizeof(*list));
    list->marker = SIZE_MAX;
    lxa_status_t status = begin(store);
    if (status != LXA_OK) return status;
    status = lxa_vector_init(&list->entries, &store->bytes.allocator,
                             sizeof(lxa_html_formatting_item_t));
    if (status == LXA_OK) status = lxa_vector_bind_budget(&list->entries, store->budget);
    if (status != LXA_OK) {
        lxa_html_formatting_list_destroy(list);
        return status;
    }
    list->store = store;
    return LXA_OK;
}

void
lxa_html_formatting_list_destroy(lxa_html_formatting_list_t *list)
{
    if (list == NULL) return;
    if (list->store != NULL)
        (void) internal(lxa_budget_release(list->store->budget, LXA_BUDGET_ACTIVE_FORMATTING,
                                           lxa_vector_length(&list->entries)));
    lxa_vector_destroy(&list->entries);
    memset(list, 0, sizeof(*list));
    list->marker = SIZE_MAX;
}

static const lxa_html_formatting_item_t *
list_items(const lxa_html_formatting_list_t *list)
{
    return (const lxa_html_formatting_item_t *) list->entries.bytes.data;
}

static lxa_status_t
list_begin(lxa_html_formatting_list_t *list)
{
    if (list == NULL) return LXA_ERROR_ARGUMENT;
    lxa_status_t status = begin(list->store);
    if (status != LXA_OK) return status;
    size_t length = lxa_vector_length(&list->entries);
    if (!valid_vector(&list->entries, sizeof(lxa_html_formatting_item_t), list->store->budget)
        || !lxa_html_formatting_marker_valid(length, list->marker)
        || length > list->store->budget->used[LXA_BUDGET_ACTIVE_FORMATTING])
        return internal(LXA_ERROR_INTERNAL);
    if (list->marker != SIZE_MAX) {
        const lxa_html_formatting_item_t *item = &list_items(list)[list->marker];
        if (item->snapshot != 0 || item->identity == 0
            || !lxa_html_formatting_marker_valid(list->marker, item->previous_marker)
            || (item->previous_marker != SIZE_MAX
                && list_items(list)[item->previous_marker].snapshot != 0))
            return internal(LXA_ERROR_INTERNAL);
    }
    return LXA_OK;
}

static size_t
list_start(const lxa_html_formatting_list_t *list)
{
    return list->marker == SIZE_MAX ? 0 : list->marker + 1;
}

static lxa_status_t
list_append(lxa_html_formatting_list_t *list, size_t snapshot, uint64_t identity, bool marker)
{
    lxa_status_t status = list_begin(list);
    if (status != LXA_OK) return status;
    if (identity == 0) return LXA_ERROR_ARGUMENT;
    if (!marker) {
        const lxa_html_formatting_snapshot_t *stored;
        status = snapshot_get(list->store, snapshot, &stored);
        if (status != LXA_OK) return status;
    }
    size_t index = lxa_vector_length(&list->entries), required;
    status = lxa_size_add(index, 1, &required);
    if (status == LXA_OK)
        status = lxa_budget_check(list->store->budget, LXA_BUDGET_ACTIVE_FORMATTING, 1,
                                  &list->store->limit_reason);
    if (status == LXA_OK)
        status = reserve_status(list->store, lxa_vector_reserve(&list->entries, required));
    if (status == LXA_OK)
        status = lxa_budget_charge(list->store->budget, LXA_BUDGET_ACTIVE_FORMATTING, 1,
                                   &list->store->limit_reason);
    if (status != LXA_OK) return status;
    lxa_html_formatting_item_t item = {snapshot, identity, list->marker};
    status = lxa_vector_push(&list->entries, &item);
    if (status != LXA_OK) {
        (void) internal(lxa_budget_release(list->store->budget, LXA_BUDGET_ACTIVE_FORMATTING, 1));
        return internal(status);
    }
    if (marker) list->marker = index;
    return LXA_OK;
}

lxa_status_t
lxa_html_formatting_list_append(lxa_html_formatting_list_t *list,
    size_t snapshot, uint64_t identity)
{
    return list_append(list, snapshot, identity, false);
}

lxa_status_t
lxa_html_formatting_list_push_marker(lxa_html_formatting_list_t *list, uint64_t identity)
{
    return list_append(list, 0, identity, true);
}

lxa_status_t
lxa_html_formatting_list_remove(lxa_html_formatting_list_t *list, size_t index)
{
    lxa_status_t status = list_begin(list);
    if (status != LXA_OK) return status;
    size_t length = lxa_vector_length(&list->entries);
    if (!lxa_html_formatting_entry_removable(length, list->marker, index))
        return LXA_ERROR_ARGUMENT;
    status = lxa_budget_charge(list->store->budget, LXA_BUDGET_WORK, length - index,
                               &list->store->limit_reason);
    if (status != LXA_OK) return status;
    status = lxa_vector_erase(&list->entries, index, 1);
    if (status == LXA_OK)
        status = lxa_budget_release(list->store->budget, LXA_BUDGET_ACTIVE_FORMATTING, 1);
    return internal(status);
}

lxa_status_t
lxa_html_formatting_list_move(lxa_html_formatting_list_t *list, size_t from, size_t to)
{
    lxa_status_t status = list_begin(list);
    if (status != LXA_OK) return status;
    size_t length = lxa_vector_length(&list->entries);
    if (!lxa_html_formatting_entry_removable(length, list->marker, from)
        || !lxa_html_formatting_entry_removable(length, list->marker, to))
        return LXA_ERROR_ARGUMENT;
    if (from == to) return LXA_OK;
    size_t low = from < to ? from : to, high = from < to ? to : from;
    status = lxa_budget_charge(list->store->budget, LXA_BUDGET_WORK, high - low + 1,
                               &list->store->limit_reason);
    if (status != LXA_OK) return status;
    lxa_html_formatting_item_t *items =
        (lxa_html_formatting_item_t *) list->entries.bytes.data;
    for (size_t i = low; i <= high; ++i)
        if (items[i].identity == 0 || items[i].snapshot == 0
            || items[i].snapshot > lxa_vector_length(&list->store->snapshots))
            return internal(LXA_ERROR_INTERNAL);
    lxa_html_formatting_item_t moved = items[from];
    /* Checked vector geometry bounds the shifted byte count. */
    if (from < to)
        memmove(items + from, items + from + 1, (to - from) * sizeof(*items));
    else
        memmove(items + to + 1, items + to, (from - to) * sizeof(*items));
    items[to] = moved;
    return LXA_OK;
}

lxa_status_t
lxa_html_formatting_list_clear(lxa_html_formatting_list_t *list)
{
    lxa_status_t status = list_begin(list);
    if (status != LXA_OK) return status;
    size_t previous = list->marker == SIZE_MAX ? SIZE_MAX
        : list_items(list)[list->marker].previous_marker;
    lxa_html_formatting_clear_plan_t plan;
    status = lxa_html_formatting_clear_plan(lxa_vector_length(&list->entries), list->marker,
        previous, list->store->budget->used[LXA_BUDGET_ACTIVE_FORMATTING], &plan);
    if (status != LXA_OK) return status;
    status = lxa_vector_erase(&list->entries, plan.keep, plan.removed);
    if (status == LXA_OK)
        status = lxa_budget_release(list->store->budget, LXA_BUDGET_ACTIVE_FORMATTING, plan.removed);
    if (status != LXA_OK) return internal(status);
    list->marker = plan.previous;
    return LXA_OK;
}

lxa_status_t
lxa_html_formatting_list_get(lxa_html_formatting_list_t *list,
    size_t index, lxa_html_formatting_item_t *out)
{
    if (out != NULL) memset(out, 0, sizeof(*out));
    lxa_status_t status = list_begin(list);
    if (status != LXA_OK) return status;
    if (out == NULL || index >= lxa_vector_length(&list->entries)) return LXA_ERROR_ARGUMENT;
    status = lxa_budget_charge(list->store->budget, LXA_BUDGET_WORK, 1,
                               &list->store->limit_reason);
    if (status != LXA_OK) return status;
    const lxa_html_formatting_item_t *item = &list_items(list)[index];
    if (item->identity == 0
        || (item->snapshot == 0 && !lxa_html_formatting_marker_valid(index, item->previous_marker)))
        return internal(LXA_ERROR_INTERNAL);
    if (item->snapshot != 0) {
        const lxa_html_formatting_snapshot_t *snapshot;
        status = snapshot_get(list->store, item->snapshot, &snapshot);
        if (status != LXA_OK) return internal(status);
    }
    *out = *item;
    return LXA_OK;
}

static inline lxa_status_t
list_name(const lxa_html_formatting_list_t *list, size_t index, lxa_span_t *name)
{
    const lxa_html_formatting_snapshot_t *snapshot;
    lxa_status_t status = snapshot_get(list->store, list_items(list)[index].snapshot, &snapshot);
    if (status != LXA_OK) return internal(status);
    *name = (lxa_span_t){list->store->bytes.data + snapshot->name_offset, snapshot->name_length};
    return LXA_OK;
}

static inline lxa_status_t
list_equal(const lxa_html_formatting_list_t *list, size_t index,
             const lxa_html_token_t *token, bool *out)
{
    const lxa_html_formatting_snapshot_t *snapshot;
    lxa_status_t status = snapshot_get(list->store, list_items(list)[index].snapshot, &snapshot);
    if (status != LXA_OK) return internal(status);
    const formatting_store_view_t view = {list->store, snapshot};
    return store_view_equal(&view, token, out);
}

static inline lxa_status_t
list_reference(const lxa_html_formatting_list_t *list, size_t index, uint64_t *identity)
{
    uint64_t current = list_items(list)[index].identity;
    if (current == 0) return internal(LXA_ERROR_INTERNAL);
    *identity = current;
    return LXA_OK;
}

#define LXA_FORMATTING_REFERENCE_NAME list_find_reference
#define LXA_FORMATTING_REFERENCE_VIEW lxa_html_formatting_list_t
#define LXA_FORMATTING_REFERENCE_LENGTH(view) lxa_vector_length(&(view)->entries)
#define LXA_FORMATTING_REFERENCE_READ list_reference
#define LXA_FORMATTING_REFERENCE_BUDGET(view) ((view)->store->budget)
#define LXA_FORMATTING_REFERENCE_REASON(view) (&(view)->store->limit_reason)
#include "html/formatting_reference.inc"
#undef LXA_FORMATTING_REFERENCE_NAME
#undef LXA_FORMATTING_REFERENCE_VIEW
#undef LXA_FORMATTING_REFERENCE_LENGTH
#undef LXA_FORMATTING_REFERENCE_READ
#undef LXA_FORMATTING_REFERENCE_BUDGET
#undef LXA_FORMATTING_REFERENCE_REASON

#define LXA_FORMATTING_FIND_NAME list_find
#define LXA_FORMATTING_FIND_VIEW lxa_html_formatting_list_t
#define LXA_FORMATTING_FIND_LENGTH(view) lxa_vector_length(&(view)->entries)
#define LXA_FORMATTING_FIND_READ list_name
#define LXA_FORMATTING_FIND_BUDGET(view) ((view)->store->budget)
#define LXA_FORMATTING_FIND_REASON(view) (&(view)->store->limit_reason)
#include "html/formatting_find.inc"
#undef LXA_FORMATTING_FIND_NAME
#undef LXA_FORMATTING_FIND_VIEW
#undef LXA_FORMATTING_FIND_LENGTH
#undef LXA_FORMATTING_FIND_READ
#undef LXA_FORMATTING_FIND_BUDGET
#undef LXA_FORMATTING_FIND_REASON

#define LXA_NOAH_PLAN_NAME list_noah_plan
#define LXA_NOAH_VIEW lxa_html_formatting_list_t
#define LXA_NOAH_LENGTH(view) lxa_vector_length(&(view)->entries)
#define LXA_NOAH_EQUAL list_equal
#define LXA_NOAH_BUDGET(view) ((view)->store->budget)
#define LXA_NOAH_REASON(view) (&(view)->store->limit_reason)
#include "html/formatting_noah.inc"
#undef LXA_NOAH_PLAN_NAME
#undef LXA_NOAH_VIEW
#undef LXA_NOAH_LENGTH
#undef LXA_NOAH_EQUAL
#undef LXA_NOAH_BUDGET
#undef LXA_NOAH_REASON

lxa_status_t
lxa_html_formatting_list_find(lxa_html_formatting_list_t *list, lxa_span_t target, size_t *index)
{
    lxa_status_t status = list_begin(list);
    if (status != LXA_OK) return status;
    if (index == NULL || target.data == NULL || target.length == 0) return LXA_ERROR_ARGUMENT;
    return list_find(list, list_start(list), target, index);
}

lxa_status_t
lxa_html_formatting_list_find_reference(lxa_html_formatting_list_t *list,
    uint64_t identity, size_t *index)
{
    lxa_status_t status = list_begin(list);
    if (status != LXA_OK) return status;
    if (index == NULL || identity == 0) return LXA_ERROR_ARGUMENT;
    return list_find_reference(list, list_start(list), identity, index);
}

lxa_status_t
lxa_html_formatting_list_noah_plan(lxa_html_formatting_list_t *list,
    const lxa_html_token_t *token, size_t *drop)
{
    lxa_status_t status = list_begin(list);
    if (status != LXA_OK) return status;
    if (drop == NULL) return LXA_ERROR_ARGUMENT;
    return list_noah_plan(list, list_start(list), token, drop);
}

typedef struct {
    lxa_html_formatting_list_t *list;
    lxa_html_control_stack_t *stack;
    size_t boundary;
} formatting_stack_view_t;

static lxa_status_t
stack_view_init(lxa_html_formatting_list_t *list, lxa_html_control_stack_t *stack,
    size_t boundary, formatting_stack_view_t *view)
{
    lxa_status_t status = list_begin(list);
    if (status != LXA_OK) return status;
    if (stack == NULL || stack->budget != list->store->budget
        || boundary > lxa_vector_length(&stack->elements))
        return LXA_ERROR_ARGUMENT;
    *view = (formatting_stack_view_t){list, stack, boundary};
    return LXA_OK;
}

static inline lxa_status_t
adopt_stack_status(const formatting_stack_view_t *, lxa_status_t);

#define LXA_TABLE_POP_NAME list_table_recovery
#define LXA_TABLE_POP_VIEW formatting_stack_view_t
#define LXA_TABLE_POP_HAS_MARKER(view) ((view)->list->marker != SIZE_MAX)
#define LXA_TABLE_POP_PLAN(view, keep, out) adopt_stack_status(view, \
    lxa_html_control_stack_table_markers_plan((view)->stack, keep, out))
#define LXA_TABLE_POP_ELEMENTS(view, keep, template_context) adopt_stack_status(view, \
    lxa_html_control_stack_pop_to((view)->stack, keep))
#define LXA_TABLE_POP_CLEAR(view) lxa_html_formatting_list_clear((view)->list)
#include "html/table_pop.inc"
#undef LXA_TABLE_POP_NAME
#undef LXA_TABLE_POP_VIEW
#undef LXA_TABLE_POP_HAS_MARKER
#undef LXA_TABLE_POP_PLAN
#undef LXA_TABLE_POP_ELEMENTS
#undef LXA_TABLE_POP_CLEAR

lxa_status_t
lxa_html_formatting_list_pop_table(lxa_html_formatting_list_t *list,
    lxa_html_control_stack_t *stack, size_t keep, bool template_context)
{
    formatting_stack_view_t view;
    lxa_status_t status = stack_view_init(list, stack, keep, &view);
    if (status != LXA_OK) return status;
    return list_table_recovery(&view, keep, template_context);
}

static inline lxa_status_t
reconstruct_open(const formatting_stack_view_t *view, size_t index, bool *open)
{
    uint64_t identity = list_items(view->list)[index].identity;
    if (identity == 0) return internal(LXA_ERROR_INTERNAL);
    size_t found;
    lxa_status_t status = lxa_html_control_stack_open_reference_find(view->stack,
        view->boundary, identity, &found);
    if (status == LXA_ERROR_LIMIT) view->list->store->limit_reason = view->stack->limit_reason;
    if (status == LXA_OK) *open = found != lxa_vector_length(&view->stack->elements);
    return status;
}

#define LXA_RECONSTRUCT_PLAN_NAME list_reconstruct_plan
#define LXA_RECONSTRUCT_VIEW formatting_stack_view_t
#define LXA_RECONSTRUCT_LENGTH(view) lxa_vector_length(&(view)->list->entries)
#define LXA_RECONSTRUCT_OPEN reconstruct_open
#include "html/formatting_reconstruct.inc"
#undef LXA_RECONSTRUCT_PLAN_NAME
#undef LXA_RECONSTRUCT_VIEW
#undef LXA_RECONSTRUCT_LENGTH
#undef LXA_RECONSTRUCT_OPEN

lxa_status_t
lxa_html_formatting_list_reconstruct_plan(lxa_html_formatting_list_t *list,
    lxa_html_control_stack_t *stack, size_t boundary, size_t *start)
{
    formatting_stack_view_t view;
    lxa_status_t status = stack_view_init(list, stack, boundary, &view);
    if (status != LXA_OK) return status;
    if (start == NULL) return LXA_ERROR_ARGUMENT;
    return list_reconstruct_plan(&view, list_start(list), start);
}

static lxa_status_t
reconstruct_suffix(const formatting_stack_view_t *view, size_t start)
{
    lxa_html_formatting_list_t *list = view->list;
    lxa_html_control_stack_t *stack = view->stack;
    size_t length = lxa_vector_length(&list->entries);
    for (size_t i = start; i < length; ++i) {
        lxa_html_formatting_item_t *item =
            (lxa_html_formatting_item_t *) list->entries.bytes.data + i;
        if (item->identity == 0) return internal(LXA_ERROR_INTERNAL);
        const lxa_html_formatting_snapshot_t *snapshot;
        lxa_status_t status = snapshot_get(list->store, item->snapshot, &snapshot);
        if (status != LXA_OK) return internal(status);
        lxa_span_t name = {list->store->bytes.data + snapshot->name_offset, snapshot->name_length};
        uint64_t identity;
        status = lxa_html_control_stack_push(stack, name, LXA_DOM_NS_HTML, false, &identity);
        if (status == LXA_ERROR_LIMIT) list->store->limit_reason = stack->limit_reason;
        if (status != LXA_OK) return status;
        item->identity = identity;
    }
    return LXA_OK;
}

lxa_status_t
lxa_html_formatting_list_reconstruct_planned(lxa_html_formatting_list_t *list,
    lxa_html_control_stack_t *stack, size_t boundary, size_t start)
{
    formatting_stack_view_t view;
    lxa_status_t status = stack_view_init(list, stack, boundary, &view);
    if (status != LXA_OK) return status;
    if (start < list_start(list) || start > lxa_vector_length(&list->entries))
        return LXA_ERROR_ARGUMENT;
    return reconstruct_suffix(&view, start);
}

lxa_status_t
lxa_html_formatting_list_reconstruct(lxa_html_formatting_list_t *list,
    lxa_html_control_stack_t *stack, size_t boundary)
{
    formatting_stack_view_t view;
    lxa_status_t status = stack_view_init(list, stack, boundary, &view);
    if (status != LXA_OK) return status;
    size_t start;
    status = list_reconstruct_plan(&view, list_start(list), &start);
    return status == LXA_OK ? reconstruct_suffix(&view, start) : status;
}

static inline lxa_status_t
adopt_stack_status(const formatting_stack_view_t *view, lxa_status_t status)
{
    if (status == LXA_ERROR_LIMIT)
        view->list->store->limit_reason = view->stack->limit_reason;
    return status;
}

static inline lxa_status_t
adopt_reference(const formatting_stack_view_t *view, size_t index, uint64_t *identity)
{
    return list_reference(view->list, index, identity);
}

#define LXA_ADOPT_NAME list_adopt_plan
#define LXA_ADOPT_VIEW formatting_stack_view_t
#define LXA_ADOPT_FORMATTING_LENGTH(view) lxa_vector_length(&(view)->list->entries)
#define LXA_ADOPT_OPEN_LENGTH(view) lxa_vector_length(&(view)->stack->elements)
#define LXA_ADOPT_FIND(view, subject, index) \
    list_find((view)->list, list_start((view)->list), subject, index)
#define LXA_ADOPT_REFERENCE adopt_reference
#define LXA_ADOPT_OPEN(view, identity, index) adopt_stack_status(view, \
    lxa_html_control_stack_open_reference_find((view)->stack, (view)->boundary, identity, index))
#define LXA_ADOPT_SCOPED(view, identity, index) adopt_stack_status(view, \
    lxa_html_control_stack_scoped_reference_find((view)->stack, (view)->boundary, identity, index))
#define LXA_ADOPT_SPECIAL(view, start, index) adopt_stack_status(view, \
    lxa_html_control_stack_special_find((view)->stack, start, index))
#include "html/formatting_adopt.inc"
#undef LXA_ADOPT_NAME
#undef LXA_ADOPT_VIEW
#undef LXA_ADOPT_FORMATTING_LENGTH
#undef LXA_ADOPT_OPEN_LENGTH
#undef LXA_ADOPT_FIND
#undef LXA_ADOPT_REFERENCE
#undef LXA_ADOPT_OPEN
#undef LXA_ADOPT_SCOPED
#undef LXA_ADOPT_SPECIAL

lxa_status_t
lxa_html_formatting_list_adopt_plan(lxa_html_formatting_list_t *list,
    lxa_html_control_stack_t *stack, size_t boundary, lxa_span_t subject,
    lxa_html_formatting_adopt_plan_t *out)
{
    formatting_stack_view_t view;
    lxa_status_t status = stack_view_init(list, stack, boundary, &view);
    if (status != LXA_OK) return status;
    if (out == NULL || subject.data == NULL || subject.length == 0)
        return LXA_ERROR_ARGUMENT;
    return list_adopt_plan(&view, subject, out);
}

lxa_status_t
lxa_html_formatting_list_replace_open(lxa_html_formatting_list_t *list,
    lxa_html_control_stack_t *stack, size_t formatting_index, size_t open_index,
    uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    formatting_stack_view_t view;
    lxa_status_t status = stack_view_init(list, stack, 0, &view);
    if (status != LXA_OK) return status;
    if (identity == NULL
        || !lxa_html_formatting_entry_removable(lxa_vector_length(&list->entries),
                                                list->marker, formatting_index)
        || open_index >= lxa_vector_length(&stack->elements))
        return LXA_ERROR_ARGUMENT;
    lxa_html_formatting_item_t item;
    status = lxa_html_formatting_list_get(list, formatting_index, &item);
    if (status != LXA_OK) return status;
    uint64_t replacement;
    status = lxa_html_control_stack_replace_identity(stack, open_index, item.identity, &replacement);
    if (status == LXA_ERROR_LIMIT) list->store->limit_reason = stack->limit_reason;
    if (status != LXA_OK) return status;
    ((lxa_html_formatting_item_t *) list->entries.bytes.data)[formatting_index].identity = replacement;
    *identity = replacement;
    return LXA_OK;
}

static lxa_status_t
adoption_read(const formatting_stack_view_t *view, size_t index,
    lxa_span_t *name, lxa_dom_namespace_t *ns, uint64_t *identity)
{
    lxa_html_scope_entry_t entry;
    lxa_status_t status = adopt_stack_status(view,
        lxa_html_control_stack_get(view->stack, index, &entry, NULL));
    if (status == LXA_OK) {
        *name = entry.name;
        *ns = entry.namespace_id;
        *identity = entry.identity;
    }
    return status;
}

static lxa_status_t
adoption_open_identity(const formatting_stack_view_t *view, size_t index, uint64_t *identity)
{
    lxa_span_t name;
    lxa_dom_namespace_t ns;
    return adoption_read(view, index, &name, &ns, identity);
}

static lxa_status_t
adoption_ordinary(const formatting_stack_view_t *view, lxa_span_t subject)
{
    size_t index;
    lxa_status_t status = adopt_stack_status(view,
        lxa_html_control_stack_ordinary_end_find(view->stack, view->boundary, subject, &index));
    return status == LXA_OK ? adopt_stack_status(view,
        lxa_html_control_stack_pop_to(view->stack, index)) : status;
}

static lxa_status_t
adoption_begin(const formatting_stack_view_t *view, size_t index)
{
    /* The engine validates root/virtual-context geometry before dispatch. */
    (void) view;
    (void) index;
    return LXA_OK;
}

static lxa_status_t
adoption_inner(const formatting_stack_view_t *view, size_t entry, size_t node,
    uint64_t last, uint64_t *identity)
{
    (void) last;
    return lxa_html_formatting_list_replace_open(view->list, view->stack, entry, node, identity);
}

static lxa_status_t
adoption_final(const formatting_stack_view_t *view, uint64_t last, uint64_t formatting,
    uint64_t furthest, uint64_t *identity)
{
    /* Metadata has no parent/child effects; assign the final key at commit. */
    (void) view;
    (void) last;
    (void) formatting;
    (void) furthest;
    *identity = 0;
    return LXA_OK;
}

static lxa_status_t
adoption_commit(const formatting_stack_view_t *view, uint64_t clone, size_t formatting_index,
    size_t bookmark, size_t stack_index, size_t furthest_index)
{
    (void) clone;
    uint64_t identity;
    lxa_status_t status = lxa_html_formatting_list_replace_open(view->list, view->stack,
        formatting_index, stack_index, &identity);
    if (status == LXA_OK)
        status = lxa_html_formatting_list_move(view->list, formatting_index, bookmark);
    if (status == LXA_OK) status = adopt_stack_status(view,
        lxa_html_control_stack_move(view->stack, stack_index, furthest_index));
    return status;
}

#define LXA_ADOPTION_NAME list_adoption
#define LXA_ADOPTION_VIEW formatting_stack_view_t
#define LXA_ADOPTION_REFERENCE uint64_t
#define LXA_ADOPTION_SAME(a, b) ((a) == (b))
#define LXA_ADOPTION_LENGTH(view) lxa_vector_length(&(view)->stack->elements)
#define LXA_ADOPTION_BOUNDARY(view) ((view)->boundary)
#define LXA_ADOPTION_FORMATTING_LENGTH(view) lxa_vector_length(&(view)->list->entries)
#define LXA_ADOPTION_READ adoption_read
#define LXA_ADOPTION_OPEN_IDENTITY adoption_open_identity
#define LXA_ADOPTION_IDENTITY adopt_reference
#define LXA_ADOPTION_FIND(view, identity, index) \
    list_find_reference((view)->list, list_start((view)->list), identity, index)
#define LXA_ADOPTION_PLAN list_adopt_plan
#define LXA_ADOPTION_ORDINARY adoption_ordinary
#define LXA_ADOPTION_POP(view, keep) adopt_stack_status(view, lxa_html_control_stack_pop_to((view)->stack, keep))
#define LXA_ADOPTION_REMOVE_FORMATTING(view, index) lxa_html_formatting_list_remove((view)->list, index)
#define LXA_ADOPTION_REMOVE_OPEN(view, index) adopt_stack_status(view, lxa_html_control_stack_remove((view)->stack, index))
#define LXA_ADOPTION_BEGIN adoption_begin
#define LXA_ADOPTION_INNER adoption_inner
#define LXA_ADOPTION_FINAL adoption_final
#define LXA_ADOPTION_COMMIT adoption_commit
#define LXA_ADOPTION_INTERNAL(view) internal(LXA_ERROR_INTERNAL)
#include "html/formatting_adoption.inc"
#undef LXA_ADOPTION_NAME
#undef LXA_ADOPTION_VIEW
#undef LXA_ADOPTION_REFERENCE
#undef LXA_ADOPTION_SAME
#undef LXA_ADOPTION_LENGTH
#undef LXA_ADOPTION_BOUNDARY
#undef LXA_ADOPTION_FORMATTING_LENGTH
#undef LXA_ADOPTION_READ
#undef LXA_ADOPTION_OPEN_IDENTITY
#undef LXA_ADOPTION_IDENTITY
#undef LXA_ADOPTION_FIND
#undef LXA_ADOPTION_PLAN
#undef LXA_ADOPTION_ORDINARY
#undef LXA_ADOPTION_POP
#undef LXA_ADOPTION_REMOVE_FORMATTING
#undef LXA_ADOPTION_REMOVE_OPEN
#undef LXA_ADOPTION_BEGIN
#undef LXA_ADOPTION_INNER
#undef LXA_ADOPTION_FINAL
#undef LXA_ADOPTION_COMMIT
#undef LXA_ADOPTION_INTERNAL

lxa_status_t
lxa_html_formatting_list_adopt(lxa_html_formatting_list_t *list,
    lxa_html_control_stack_t *stack, size_t boundary, lxa_span_t subject)
{
    formatting_stack_view_t view;
    lxa_status_t status = stack_view_init(list, stack, boundary, &view);
    if (status != LXA_OK) return status;
    const lxa_buffer_t stack_header = {.data = (uint8_t *) stack, .capacity = sizeof(*stack)};
    const lxa_buffer_t list_header = {.data = (uint8_t *) list, .capacity = sizeof(*list)};
    if (subject.data == NULL || subject.length == 0
        || !lxa_html_tree_formatting_tag(subject)
        || overlaps_buffer(subject, &stack_header)
        || overlaps_buffer(subject, &list_header)
        || overlaps_buffer(subject, &stack->names)
        || overlaps_buffer(subject, &stack->elements.bytes)
        || overlaps_buffer(subject, &list->entries.bytes))
        return LXA_ERROR_ARGUMENT;
    return list_adoption(&view, subject);
}

static lxa_status_t
list_admit_planned(lxa_html_formatting_list_t *list, lxa_html_control_stack_t *stack,
    const lxa_html_token_t *token, size_t drop, uint64_t *identity)
{
    size_t required, length = lxa_vector_length(&list->entries);
    size_t additional = drop == SIZE_MAX ? 1 : 0;
    lxa_status_t status = lxa_size_add(length, additional, &required);
    if (status == LXA_OK)
        status = lxa_budget_check(list->store->budget, LXA_BUDGET_ACTIVE_FORMATTING,
                                  additional, &list->store->limit_reason);
    if (status == LXA_OK && drop != SIZE_MAX)
        status = lxa_budget_charge(list->store->budget, LXA_BUDGET_WORK, length - drop,
                                   &list->store->limit_reason);
    if (status != LXA_OK) return status;
    size_t old_snapshots = list->store->snapshots.bytes.length;
    size_t old_attributes = list->store->attributes.bytes.length;
    size_t old_bytes = list->store->bytes.length;
    size_t snapshot;
    status = lxa_html_formatting_store_capture(list->store, token, &snapshot);
    if (status != LXA_OK) return status;
    /* Copy token views before either membership or open storage can move. */
    status = reserve_status(list->store, lxa_vector_reserve(&list->entries, required));
    if (status == LXA_OK)
        status = lxa_budget_charge(list->store->budget, LXA_BUDGET_ACTIVE_FORMATTING,
                                   additional, &list->store->limit_reason);
    bool charged = status == LXA_OK;
    uint64_t opened;
    if (status == LXA_OK) {
        const lxa_html_formatting_snapshot_t *stored;
        status = snapshot_get(list->store, snapshot, &stored);
        if (status == LXA_OK) {
            lxa_span_t name = {list->store->bytes.data + stored->name_offset, stored->name_length};
            status = lxa_html_control_stack_push(stack, name, LXA_DOM_NS_HTML, false, &opened);
            if (status == LXA_ERROR_LIMIT) list->store->limit_reason = stack->limit_reason;
        }
        else status = internal(status);
    }
    if (status != LXA_OK) {
        if (charged)
            (void) internal(lxa_budget_release(list->store->budget, LXA_BUDGET_ACTIVE_FORMATTING,
                                               additional));
        lxa_status_t restored = rollback(list->store, old_snapshots, old_attributes, old_bytes);
        return restored == LXA_OK ? status : restored;
    }
    bool removed = false;
    if (drop != SIZE_MAX) {
        status = lxa_vector_erase(&list->entries, drop, 1);
        removed = status == LXA_OK;
    }
    lxa_html_formatting_item_t item = {snapshot, opened, list->marker};
    if (status == LXA_OK) status = lxa_vector_push(&list->entries, &item);
    if (status != LXA_OK) {
        (void) internal(lxa_budget_release(list->store->budget, LXA_BUDGET_ACTIVE_FORMATTING,
                                           additional + removed));
        return internal(status);
    }
    *identity = opened;
    return LXA_OK;
}

lxa_status_t
lxa_html_formatting_list_admit(lxa_html_formatting_list_t *list,
    lxa_html_control_stack_t *stack, const lxa_html_token_t *token, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    lxa_status_t status = list_begin(list);
    if (status != LXA_OK) return status;
    if (identity == NULL || stack == NULL || stack->budget != list->store->budget
        || !lxa_html_formatting_token_shape_valid(token)
        || !lxa_html_tree_formatting_tag(token->name))
        return LXA_ERROR_ARGUMENT;
    size_t drop;
    status = list_noah_plan(list, list_start(list), token, &drop);
    return status == LXA_OK ? list_admit_planned(list, stack, token, drop, identity) : status;
}

static lxa_status_t
list_plan_add(const formatting_stack_view_t *view, const lxa_html_token_t *token, size_t *drop)
{
    lxa_html_formatting_list_t *list = view->list;
    size_t planned, required;
    lxa_status_t status = list_noah_plan(list, list_start(list), token, &planned);
    size_t additional = status == LXA_OK && planned == SIZE_MAX ? 1 : 0;
    if (status == LXA_OK)
        status = lxa_size_add(lxa_vector_length(&list->entries), additional, &required);
    if (status == LXA_OK)
        status = lxa_budget_check(list->store->budget, LXA_BUDGET_ACTIVE_FORMATTING,
                                  additional, &list->store->limit_reason);
    if (status == LXA_OK)
        status = reserve_status(list->store, lxa_vector_reserve(&list->entries, required));
    if (status == LXA_OK) *drop = planned;
    return status;
}

#define LXA_FORMATTING_START_NAME list_prepare_start
#define LXA_FORMATTING_START_VIEW formatting_stack_view_t
#define LXA_FORMATTING_START_REFERENCE uint64_t
#define LXA_FORMATTING_START_FIND(view, name, index) \
    list_find((view)->list, list_start((view)->list), name, index)
#define LXA_FORMATTING_START_IDENTITY adopt_reference
#define LXA_FORMATTING_START_ADOPT list_adoption
#define LXA_FORMATTING_START_FIND_REFERENCE(view, identity, index) \
    list_find_reference((view)->list, list_start((view)->list), identity, index)
#define LXA_FORMATTING_START_FORMATTING_LENGTH(view) lxa_vector_length(&(view)->list->entries)
#define LXA_FORMATTING_START_REMOVE_FORMATTING(view, index) \
    lxa_html_formatting_list_remove((view)->list, index)
#define LXA_FORMATTING_START_OPEN(view, identity, index) adopt_stack_status(view, \
    lxa_html_control_stack_open_reference_find((view)->stack, (view)->boundary, identity, index))
#define LXA_FORMATTING_START_OPEN_LENGTH(view) lxa_vector_length(&(view)->stack->elements)
#define LXA_FORMATTING_START_REMOVE_OPEN(view, index) adopt_stack_status(view, \
    lxa_html_control_stack_remove((view)->stack, index))
#define LXA_FORMATTING_START_RECONSTRUCT(view) \
    lxa_html_formatting_list_reconstruct((view)->list, (view)->stack, (view)->boundary)
#define LXA_FORMATTING_START_SCOPED(view, name, index) adopt_stack_status(view, \
    lxa_html_control_stack_scope_find((view)->stack, (view)->boundary, name, LXA_HTML_SCOPE_NORMAL, index))
#define LXA_FORMATTING_START_PLAN list_plan_add
#include "html/formatting_start.inc"
#undef LXA_FORMATTING_START_NAME
#undef LXA_FORMATTING_START_VIEW
#undef LXA_FORMATTING_START_REFERENCE
#undef LXA_FORMATTING_START_FIND
#undef LXA_FORMATTING_START_IDENTITY
#undef LXA_FORMATTING_START_ADOPT
#undef LXA_FORMATTING_START_FIND_REFERENCE
#undef LXA_FORMATTING_START_FORMATTING_LENGTH
#undef LXA_FORMATTING_START_REMOVE_FORMATTING
#undef LXA_FORMATTING_START_OPEN
#undef LXA_FORMATTING_START_OPEN_LENGTH
#undef LXA_FORMATTING_START_REMOVE_OPEN
#undef LXA_FORMATTING_START_RECONSTRUCT
#undef LXA_FORMATTING_START_SCOPED
#undef LXA_FORMATTING_START_PLAN

static bool
start_alias(lxa_span_t span, const formatting_stack_view_t *view)
{
    const lxa_buffer_t stack = {.data = (uint8_t *) view->stack, .capacity = sizeof(*view->stack)};
    const lxa_buffer_t list = {.data = (uint8_t *) view->list, .capacity = sizeof(*view->list)};
    const lxa_buffer_t store = {.data = (uint8_t *) view->list->store,
                               .capacity = sizeof(*view->list->store)};
    return overlaps_buffer(span, &stack) || overlaps_buffer(span, &list)
        || overlaps_buffer(span, &store) || overlaps_store(span, view->list->store)
        || overlaps_buffer(span, &view->stack->names)
        || overlaps_buffer(span, &view->stack->elements.bytes)
        || overlaps_buffer(span, &view->list->entries.bytes);
}

static lxa_status_t
reserve_marker(lxa_html_formatting_list_t *list, bool open)
{
    size_t required;
    lxa_status_t status = lxa_size_add(lxa_vector_length(&list->entries), 1, &required);
    if (status == LXA_OK)
        status = lxa_budget_check(list->store->budget, LXA_BUDGET_ACTIVE_FORMATTING, 1,
                                  &list->store->limit_reason);
    if (status == LXA_OK && open)
        status = lxa_budget_check(list->store->budget, LXA_BUDGET_OPEN_ELEMENTS, 1,
                                  &list->store->limit_reason);
    if (status == LXA_OK)
        status = reserve_status(list->store, lxa_vector_reserve(&list->entries, required));
    return status;
}

lxa_status_t
lxa_html_formatting_list_reserve_marker(lxa_html_formatting_list_t *list)
{
    lxa_status_t status = list_begin(list);
    return status == LXA_OK ? reserve_marker(list, false) : status;
}

lxa_status_t
lxa_html_formatting_list_admit_marker(lxa_html_formatting_list_t *list,
    lxa_html_control_stack_t *stack, lxa_span_t name, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    formatting_stack_view_t view;
    lxa_status_t status = stack_view_init(list, stack, 0, &view);
    if (status != LXA_OK) return status;
    if (identity == NULL || name.data == NULL || name.length == 0
        || start_alias(name, &view)
        || (!lxa_html_tree_formatting_marker_tag(name)
            && !lxa_html_tree_name_equals(name, "template", 8)))
        return LXA_ERROR_ARGUMENT;
    size_t index = lxa_vector_length(&list->entries);
    status = reserve_marker(list, true);
    if (status == LXA_OK)
        status = lxa_budget_charge(list->store->budget, LXA_BUDGET_ACTIVE_FORMATTING, 1,
                                   &list->store->limit_reason);
    if (status != LXA_OK) return status;
    uint64_t opened;
    status = lxa_html_control_stack_push(stack, name, LXA_DOM_NS_HTML, false, &opened);
    if (status == LXA_ERROR_LIMIT) list->store->limit_reason = stack->limit_reason;
    if (status != LXA_OK) {
        (void) internal(lxa_budget_release(list->store->budget, LXA_BUDGET_ACTIVE_FORMATTING, 1));
        return status;
    }
    lxa_html_formatting_item_t item = {0, opened, list->marker};
    status = lxa_vector_push(&list->entries, &item);
    if (status != LXA_OK) {
        (void) internal(lxa_budget_release(list->store->budget, LXA_BUDGET_ACTIVE_FORMATTING, 1));
        return internal(status);
    }
    list->marker = index;
    *identity = opened;
    return LXA_OK;
}

lxa_status_t
lxa_html_formatting_list_start_intake(lxa_html_formatting_list_t *list,
    lxa_html_control_stack_t *stack, size_t boundary,
    const lxa_html_token_t *token)
{
    formatting_stack_view_t view;
    lxa_status_t status = stack_view_init(list, stack, boundary, &view);
    if (status != LXA_OK) return status;
    if (!lxa_html_formatting_token_shape_valid(token)
        || !lxa_html_tree_formatting_tag(token->name))
        return LXA_ERROR_ARGUMENT;
    lxa_span_t descriptors = {(const uint8_t *) token->attributes,
                              token->attribute_count * sizeof(*token->attributes)};
    if (start_alias((lxa_span_t){(const uint8_t *) token, sizeof(*token)}, &view)
        || start_alias(token->name, &view) || start_alias(descriptors, &view))
        return LXA_ERROR_ARGUMENT;
    status = lxa_budget_check_item(list->store->budget, LXA_BUDGET_NAME_BYTES,
                                   token->name.length, &list->store->limit_reason);
    if (status == LXA_OK)
        status = lxa_budget_check_item(list->store->budget, LXA_BUDGET_ATTRS_PER_ELEMENT,
                                       token->attribute_count, &list->store->limit_reason);
    const lxa_html_formatting_view_t attributes = {
        token->name, token->attributes, token->attribute_count,
        list->store->budget, &list->store->limit_reason
    };
    for (size_t i = 0; status == LXA_OK && i < token->attribute_count; ++i) {
        lxa_span_t name, value;
        status = lxa_budget_charge(list->store->budget, LXA_BUDGET_WORK, 1,
                                   &list->store->limit_reason);
        if (status == LXA_OK)
            status = lxa_html_formatting_view_read(&attributes, i + 1, &name, &value);
        if (status == LXA_OK && (start_alias(name, &view) || start_alias(value, &view)))
            status = LXA_ERROR_ARGUMENT;
    }
    return status;
}

lxa_status_t
lxa_html_formatting_list_plan_add(lxa_html_formatting_list_t *list,
    lxa_html_control_stack_t *stack, size_t boundary,
    const lxa_html_token_t *token, size_t *drop)
{
    formatting_stack_view_t view;
    lxa_status_t status = stack_view_init(list, stack, boundary, &view);
    if (status != LXA_OK) return status;
    if (drop == NULL || !lxa_html_formatting_token_shape_valid(token)
        || !lxa_html_tree_formatting_tag(token->name)) return LXA_ERROR_ARGUMENT;
    return list_plan_add(&view, token, drop);
}

lxa_status_t
lxa_html_formatting_list_admit_planned(lxa_html_formatting_list_t *list,
    lxa_html_control_stack_t *stack, const lxa_html_token_t *token,
    size_t drop, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    formatting_stack_view_t view;
    lxa_status_t status = stack_view_init(list, stack, 0, &view);
    if (status != LXA_OK) return status;
    if (identity == NULL || !lxa_html_formatting_token_shape_valid(token)
        || !lxa_html_tree_formatting_tag(token->name)
        || (drop != SIZE_MAX && (drop < list_start(list)
            || drop >= lxa_vector_length(&list->entries)))) return LXA_ERROR_ARGUMENT;
    return list_admit_planned(list, stack, token, drop, identity);
}

lxa_status_t
lxa_html_formatting_list_start(lxa_html_formatting_list_t *list,
    lxa_html_control_stack_t *stack, size_t boundary,
    const lxa_html_token_t *token, uint64_t *identity)
{
    if (identity != NULL) *identity = 0;
    if (identity == NULL) return LXA_ERROR_ARGUMENT;
    lxa_status_t status = lxa_html_formatting_list_start_intake(list, stack, boundary, token);
    formatting_stack_view_t view = {list, stack, boundary};
    size_t drop;
    if (status == LXA_OK) status = list_prepare_start(&view, token, &drop);
    return status == LXA_OK ? list_admit_planned(list, stack, token, drop, identity) : status;
}
