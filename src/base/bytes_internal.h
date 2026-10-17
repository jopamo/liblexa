#ifndef LIBLEXA_BASE_BYTES_INTERNAL_H
#define LIBLEXA_BASE_BYTES_INTERNAL_H

#include <string.h>
#include "liblexa/base.h"

static inline int
lxa_buffer_allocator_valid(const lxa_allocator_t *allocator)
{
    return allocator != NULL && allocator->allocate != NULL
           && allocator->resize != NULL && allocator->release != NULL;
}

static inline lxa_status_t
lxa_buffer_reserve_impl(lxa_buffer_t *buffer, size_t minimum_capacity)
{
    size_t capacity;
    if (buffer == NULL || !lxa_buffer_allocator_valid(&buffer->allocator)) {
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

static inline lxa_status_t
lxa_buffer_append_impl(lxa_buffer_t *buffer, lxa_span_t bytes)
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
    status = lxa_buffer_reserve_impl(buffer, length);
    if (status != LXA_OK) {
        return status;
    }
    memcpy(buffer->data + buffer->length, bytes.data, bytes.length);
    buffer->length = length;
    return LXA_OK;
}

#endif
