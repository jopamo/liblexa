#include <assert.h>
#include <string.h>
#include "html/formatting_store.h"
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
        if (attribute->name.data == NULL || attribute->name.length == 0
            || (attribute->value.data == NULL && attribute->value.length != 0))
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
