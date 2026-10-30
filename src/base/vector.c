#include <string.h>

#include "liblexa/base.h"
#include "base/vector_internal.h"

lxa_status_t
lxa_vector_init(lxa_vector_t *vector, const lxa_allocator_t *allocator,
    size_t element_size)
{
    lxa_status_t status;
    if (vector == NULL || element_size == 0) {
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_buffer_init(&vector->bytes, allocator);
    if (status == LXA_OK) {
        vector->element_size = element_size;
    }
    return status;
}

void
lxa_vector_destroy(lxa_vector_t *vector)
{
    if (vector != NULL) {
        lxa_buffer_destroy(&vector->bytes);
        vector->element_size = 0;
    }
}

lxa_status_t
lxa_vector_bind_budget(lxa_vector_t *vector, lxa_budget_t *budget)
{
    return vector == NULL || vector->element_size == 0
        ? LXA_ERROR_ARGUMENT
        : lxa_buffer_bind_budget(&vector->bytes, budget);
}

void
lxa_vector_clear(lxa_vector_t *vector)
{
    if (vector != NULL) {
        lxa_buffer_clear(&vector->bytes);
    }
}

size_t
lxa_vector_length(const lxa_vector_t *vector)
{
    return vector == NULL || vector->element_size == 0 ? 0
                                                       : vector->bytes.length / vector->element_size;
}

lxa_status_t
lxa_vector_reserve(lxa_vector_t *vector, size_t element_count)
{
    size_t bytes;
    lxa_status_t status;
    if (vector == NULL || vector->element_size == 0) {
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_size_mul(element_count, vector->element_size, &bytes);
    return status == LXA_OK ? lxa_buffer_reserve(&vector->bytes, bytes)
                            : status;
}

lxa_status_t
lxa_vector_insert(lxa_vector_t *vector, size_t index, const void *value)
{
    size_t length, offset, total;
    lxa_status_t status;
    if (vector == NULL || vector->element_size == 0 || value == NULL
        || index > lxa_vector_length(vector)) {
        return LXA_ERROR_ARGUMENT;
    }
    length = vector->bytes.length;
    status = lxa_size_add(length, vector->element_size, &total);
    if (status != LXA_OK) {
        return status;
    }
    /* index <= length / element_size, so multiplication cannot overflow. */
    offset = index * vector->element_size;
    status = lxa_buffer_reserve(&vector->bytes, total);
    if (status != LXA_OK) {
        return status;
    }
    memmove(vector->bytes.data + offset + vector->element_size,
        vector->bytes.data + offset, length - offset);
    memcpy(vector->bytes.data + offset, value, vector->element_size);
    vector->bytes.length = total;
    return LXA_OK;
}

lxa_status_t
lxa_vector_push(lxa_vector_t *vector, const void *value)
{
    return lxa_vector_push_impl(vector, value,
        vector == NULL ? 0 : vector->element_size);
}

lxa_status_t
lxa_vector_at(lxa_vector_t *vector, size_t index, void **out)
{
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = NULL;
    if (vector == NULL || vector->element_size == 0
        || index >= lxa_vector_length(vector)) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = vector->bytes.data + index * vector->element_size;
    return LXA_OK;
}

lxa_status_t
lxa_vector_erase(lxa_vector_t *vector, size_t index, size_t count)
{
    size_t length, offset, removed;
    if (vector == NULL || vector->element_size == 0) {
        return LXA_ERROR_ARGUMENT;
    }
    length = lxa_vector_length(vector);
    if (index > length || count > length - index) {
        return LXA_ERROR_ARGUMENT;
    }
    if (count == 0) {
        return LXA_OK;
    }
    offset = index * vector->element_size;
    removed = count * vector->element_size;
    memmove(vector->bytes.data + offset, vector->bytes.data + offset + removed,
        vector->bytes.length - offset - removed);
    vector->bytes.length -= removed;
    return LXA_OK;
}
