#ifndef LIBLEXA_BASE_VECTOR_INTERNAL_H
#define LIBLEXA_BASE_VECTOR_INTERNAL_H

#include <string.h>

#include "liblexa/base.h"

/* Use the known stride only when it matches the vector's current stride. */
static inline size_t
lxa_vector_length_sized(const lxa_vector_t *vector, size_t element_size)
{
    if (vector != NULL && element_size != 0
        && vector->element_size == element_size)
        return vector->bytes.length / element_size;
    return lxa_vector_length(vector);
}

/* element_size is the vector's current stride. */
static inline lxa_status_t
lxa_vector_push_impl(lxa_vector_t *vector, const void *value, size_t element_size)
{
    if (vector != NULL && element_size != 0 && value != NULL
        && vector->bytes.length <= vector->bytes.capacity
        && element_size <= vector->bytes.capacity - vector->bytes.length) {
        memcpy(vector->bytes.data + vector->bytes.length, value, element_size);
        vector->bytes.length += element_size;
        return LXA_OK;
    }
    return lxa_vector_insert(vector, lxa_vector_length(vector), value);
}

static inline lxa_status_t
lxa_vector_push_sized(lxa_vector_t *vector, const void *value, size_t element_size)
{
    if (vector != NULL && element_size != 0
        && vector->element_size == element_size)
        return lxa_vector_push_impl(vector, value, element_size);
    return lxa_vector_push(vector, value);
}

#endif
