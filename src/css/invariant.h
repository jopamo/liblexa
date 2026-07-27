#ifndef LIBLEXA_CSS_INVARIANT_H
#define LIBLEXA_CSS_INVARIANT_H

#include <assert.h>
#include "liblexa/base.h"

static inline lxa_status_t
lxa_css_invariant(bool valid)
{
    assert(valid && "CSS internal invariant");
    return valid ? LXA_OK : LXA_ERROR_INTERNAL;
}

/* Use only after caller arguments and index bounds have been checked. */
static inline lxa_status_t
lxa_css_internal_status(lxa_status_t status)
{
    if (status == LXA_ERROR_ARGUMENT || status == LXA_ERROR_INTERNAL)
        return lxa_css_invariant(false);
    return status;
}

static inline lxa_status_t
lxa_css_vector_check(const lxa_vector_t *vector, size_t element_size)
{
    return lxa_css_invariant(vector->element_size == element_size
        && vector->bytes.length % element_size == 0
        && vector->bytes.length <= vector->bytes.capacity
        && (vector->bytes.capacity == 0 || vector->bytes.data != NULL));
}

static inline lxa_status_t
lxa_css_vector_at(lxa_vector_t *vector, size_t index, void **out)
{
    return lxa_css_internal_status(lxa_vector_at(vector, index, out));
}

#endif
