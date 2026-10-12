#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "liblexa/base.h"
#include "base/budget_internal.h"

static int
valid_allocator(const lxa_allocator_t *allocator)
{
    return allocator != NULL && allocator->allocate != NULL
           && allocator->resize != NULL && allocator->release != NULL;
}

lxa_status_t
lxa_buffer_init(lxa_buffer_t *buffer, const lxa_allocator_t *allocator)
{
    if (buffer == NULL || !valid_allocator(allocator)) {
        return LXA_ERROR_ARGUMENT;
    }
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
    buffer->allocator = *allocator;
    buffer->budget = NULL;
    return LXA_OK;
}

lxa_status_t
lxa_buffer_bind_budget(lxa_buffer_t *buffer, lxa_budget_t *budget)
{
    if (buffer == NULL || budget == NULL
        || !valid_allocator(&buffer->allocator)
        || buffer->budget != NULL || buffer->data != NULL
        || buffer->capacity != 0 || buffer->length != 0) {
        return LXA_ERROR_ARGUMENT;
    }
    buffer->budget = budget;
    return LXA_OK;
}

void
lxa_buffer_destroy(lxa_buffer_t *buffer)
{
    if (buffer != NULL) {
        size_t capacity = buffer->capacity;
        lxa_free(&buffer->allocator, buffer->data);
        if (buffer->budget != NULL && capacity != 0) {
            lxa_status_t status = lxa_budget_release(buffer->budget,
                                                      LXA_BUDGET_MEMORY,
                                                      capacity);
            assert(status == LXA_OK);
            (void) status;
        }
        buffer->data = NULL;
        buffer->length = 0;
        buffer->capacity = 0;
        buffer->budget = NULL;
    }
}

void
lxa_buffer_clear(lxa_buffer_t *buffer)
{
    if (buffer != NULL) {
        buffer->length = 0;
    }
}

lxa_status_t
lxa_buffer_reserve_exact(lxa_buffer_t *buffer, size_t capacity)
{
    void *storage;
    lxa_status_t status;
    if (buffer == NULL || !valid_allocator(&buffer->allocator)) {
        return LXA_ERROR_ARGUMENT;
    }
    if (capacity <= buffer->capacity) {
        return LXA_OK;
    }
    if (buffer->budget != NULL) {
        status = lxa_budget_charge_impl(buffer->budget, LXA_BUDGET_MEMORY,
                                        capacity - buffer->capacity, NULL);
        if (status != LXA_OK) {
            return status;
        }
    }
    storage = buffer->data;
    status = lxa_resize(&buffer->allocator, &storage, capacity);
    if (status != LXA_OK) {
        if (buffer->budget != NULL) {
            lxa_status_t canceled = lxa_budget_release(buffer->budget,
                                   LXA_BUDGET_MEMORY,
                                   capacity - buffer->capacity);
            assert(canceled == LXA_OK);
            (void) canceled;
        }
        return status;
    }
    buffer->data = storage;
    buffer->capacity = capacity;
    return LXA_OK;
}

lxa_status_t
lxa_buffer_reserve(lxa_buffer_t *buffer, size_t minimum_capacity)
{
    size_t capacity;
    if (buffer == NULL || !valid_allocator(&buffer->allocator)) {
        return LXA_ERROR_ARGUMENT;
    }
    if (minimum_capacity <= buffer->capacity) {
        return LXA_OK;
    }
    capacity = buffer->capacity > 0 ? buffer->capacity : 16;
    while (capacity < minimum_capacity) {
        if (capacity > SIZE_MAX / 2) {
            capacity = minimum_capacity;
            break;
        }
        capacity *= 2;
    }
    return lxa_buffer_reserve_exact(buffer, capacity);
}

lxa_status_t
lxa_buffer_append(lxa_buffer_t *buffer, lxa_span_t bytes)
{
    size_t length;
    lxa_status_t status;
    if (buffer == NULL || (bytes.length != 0 && bytes.data == NULL)) {
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_size_add(buffer->length, bytes.length, &length);
    if (status != LXA_OK || bytes.length == 0) {
        return status;
    }
    status = lxa_buffer_reserve(buffer, length);
    if (status != LXA_OK) {
        return status;
    }
    memcpy(buffer->data + buffer->length, bytes.data, bytes.length);
    buffer->length = length;
    return LXA_OK;
}

lxa_status_t
lxa_buffer_append_self(lxa_buffer_t *buffer, size_t offset,
                       size_t length)
{
    size_t total;
    lxa_status_t status;
    if (buffer == NULL || offset > buffer->length
        || length > buffer->length - offset) {
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_size_add(buffer->length, length, &total);
    if (status != LXA_OK || length == 0) {
        return status;
    }
    status = lxa_buffer_reserve(buffer, total);
    if (status != LXA_OK) {
        return status;
    }
    memcpy(buffer->data + buffer->length, buffer->data + offset, length);
    buffer->length = total;
    return LXA_OK;
}

lxa_status_t
lxa_buffer_truncate(lxa_buffer_t *buffer, size_t length)
{
    if (buffer == NULL || length > buffer->length) {
        return LXA_ERROR_ARGUMENT;
    }
    buffer->length = length;
    return LXA_OK;
}

lxa_status_t
lxa_buffer_terminate(lxa_buffer_t *buffer)
{
    size_t size;
    lxa_status_t status;
    if (buffer == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    status = lxa_size_add(buffer->length, 1, &size);
    if (status != LXA_OK) {
        return status;
    }
    status = lxa_buffer_reserve(buffer, size);
    if (status == LXA_OK) {
        buffer->data[buffer->length] = 0;
    }
    return status;
}
