#ifndef LIBLEXA_DOM_SHAPE_H
#define LIBLEXA_DOM_SHAPE_H

#include "liblexa/dom.h"

static inline bool
lxa_dom_attribute_shape_valid(lxa_span_t name, lxa_span_t value)
{
    return name.data != NULL && name.length != 0
        && (value.data != NULL || value.length == 0);
}

/* Check borrowed DOCTYPE fields before classification or construction. */
static inline bool
lxa_dom_doctype_shape_valid(const lxa_dom_doctype_data_t *data)
{
    return data != NULL
        && (data->name.data != NULL || data->name.length == 0)
        && (data->public_id.data != NULL || data->public_id.length == 0)
        && (data->system_id.data != NULL || data->system_id.length == 0)
        && (data->has_public_id || data->public_id.length == 0)
        && (data->has_system_id || data->system_id.length == 0);
}

#endif
