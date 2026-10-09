#ifndef LIBLEXA_BASE_VECTOR_INTERNAL_H
#define LIBLEXA_BASE_VECTOR_INTERNAL_H

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

#endif
