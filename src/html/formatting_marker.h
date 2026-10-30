#ifndef LIBLEXA_HTML_FORMATTING_MARKER_H
#define LIBLEXA_HTML_FORMATTING_MARKER_H

#include <assert.h>
#include "liblexa/base.h"

typedef struct {
    size_t keep, previous, removed;
} lxa_html_formatting_clear_plan_t;

static inline bool
lxa_html_formatting_marker_valid(size_t length, size_t marker)
{
    return marker == SIZE_MAX || marker < length;
}

static inline bool
lxa_html_formatting_entry_removable(size_t length, size_t marker, size_t index)
{
    return index < length && (marker == SIZE_MAX || index > marker);
}

static inline lxa_status_t
lxa_html_formatting_clear_plan(size_t length, size_t marker, size_t previous,
    uint64_t active, lxa_html_formatting_clear_plan_t *out)
{
    if (!lxa_html_formatting_marker_valid(length, marker)
        || (marker == SIZE_MAX ? previous != SIZE_MAX
                               : previous != SIZE_MAX && previous >= marker)) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    size_t keep = marker == SIZE_MAX ? 0 : marker;
    if (length - keep > active) {
        assert(false);
        return LXA_ERROR_INTERNAL;
    }
    *out = (lxa_html_formatting_clear_plan_t) {keep, previous, length - keep};
    return LXA_OK;
}

#endif
