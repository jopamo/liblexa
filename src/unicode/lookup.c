#include <stddef.h>
#include <string.h>

#include "liblexa/unicode.h"
#include "idna-table.h"
#include "joining-types.h"
#include "normalization-table.h"

lxa_status_t
lxa_unicode_joining_type(uint32_t cp, lxa_joining_type_t *out)
{
    size_t lo = 0, hi = LIBLEXA_JOINING_RANGE_COUNT;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = LXA_JOINING_U;
    if (cp > 0x10ffff) {
        return LXA_ERROR_ARGUMENT;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const liblexa_joining_range *row = &liblexa_joining_ranges[mid];
        if (cp < row->first) {
            hi = mid;
        } else if (cp > row->last) {
            lo = mid + 1;
        } else {
            *out = (lxa_joining_type_t) row->type;
            break;
        }
    }
    return LXA_OK;
}

lxa_status_t
lxa_unicode_idna_entry(uint32_t cp, lxa_idna_entry_t *out)
{
    size_t lo = 0, hi = LIBLEXA_IDNA_RANGE_COUNT;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = (lxa_idna_entry_t){0};
    if (cp > 0x10ffff) {
        return LXA_ERROR_ARGUMENT;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const liblexa_idna_range *row = &liblexa_idna_ranges[mid];
        if (cp < row->first) {
            hi = mid;
        } else if (cp > row->last) {
            lo = mid + 1;
        } else {
            out->status = (lxa_idna_status_t) row->status;
            out->length = row->count;
            out->idna2008 = row->idna2008;
            if (row->count != 0) {
                out->mapping = liblexa_idna_mappings + row->offset;
            }
            return LXA_OK;
        }
    }
    /* The pinned IDNA table partitions 0..10FFFF; this is unreachable
     * unless the generated table is incomplete. */
    return LXA_ERROR_ARGUMENT;
}

lxa_status_t
lxa_unicode_normalization_properties(
    uint32_t cp, lxa_normalization_properties_t *out)
{
    size_t lo = 0, hi = LIBLEXA_NORMALIZATION_COUNT;
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = (lxa_normalization_properties_t){0};
    if (cp > 0x10ffff) {
        return LXA_ERROR_ARGUMENT;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        uint32_t current = liblexa_normalization[mid].cp;
        if (cp < current) {
            hi = mid;
        } else if (cp > current) {
            lo = mid + 1;
        } else {
            out->combining_class = liblexa_normalization[mid].ccc;
            break;
        }
    }
    lo = 0;
    hi = LIBLEXA_QUICK_CHECK_COUNT;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        uint32_t current = liblexa_quick_checks[mid].cp;
        if (cp < current) {
            hi = mid;
        } else if (cp > current) {
            lo = mid + 1;
        } else {
            out->quick_checks = liblexa_quick_checks[mid].checks;
            break;
        }
    }
    return LXA_OK;
}

lxa_status_t
lxa_unicode_decompose_once(uint32_t cp, bool compatibility, uint32_t *output,
    size_t capacity, size_t *required)
{
    const uint32_t *mapping = &cp;
    uint32_t hangul[2];
    size_t length = 1, lo = 0, hi = LIBLEXA_NORMALIZATION_COUNT;
    if (required == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *required = 0;
    if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)
        || (output == NULL && capacity != 0)) {
        return LXA_ERROR_ARGUMENT;
    }
    if (cp >= 0xac00 && cp < 0xac00 + 11172) {
        uint32_t s = cp - 0xac00;
        uint32_t t = s % 28;
        hangul[0] = t != 0 ? cp - t : 0x1100 + s / 588;
        hangul[1] = t != 0 ? 0x11a7 + t : 0x1161 + (s % 588) / 28;
        mapping = hangul;
        length = 2;
    } else {
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            const liblexa_normalization_entry *row = &liblexa_normalization[mid];
            if (cp < row->cp) {
                hi = mid;
            } else if (cp > row->cp) {
                lo = mid + 1;
            } else {
                if (row->length != 0 && (compatibility || !row->compatibility)) {
                    mapping = liblexa_decompositions + row->offset;
                    length = row->length;
                }
                break;
            }
        }
    }
    *required = length;
    if (capacity < length) {
        return LXA_ERROR_OVERFLOW;
    }
    if (output == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    memcpy(output, mapping, length * sizeof(*output));
    return LXA_OK;
}

lxa_status_t
lxa_unicode_compose_pair(uint32_t starter, uint32_t second,
    uint32_t *composed)
{
    size_t lo = 0, hi = LIBLEXA_COMPOSITION_COUNT;
    if (composed == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *composed = UINT32_MAX;
    if (starter > 0x10ffff || second > 0x10ffff
        || (starter >= 0xd800 && starter <= 0xdfff)
        || (second >= 0xd800 && second <= 0xdfff)) {
        return LXA_ERROR_ARGUMENT;
    }
    if (starter >= 0x1100 && starter < 0x1100 + 19
        && second >= 0x1161 && second < 0x1161 + 21) {
        *composed = 0xac00 + (starter - 0x1100) * 588
            + (second - 0x1161) * 28;
        return LXA_OK;
    }
    if (starter >= 0xac00 && starter < 0xac00 + 11172
        && (starter - 0xac00) % 28 == 0
        && second >= 0x11a8 && second < 0x11a8 + 27) {
        *composed = starter + second - 0x11a7;
        return LXA_OK;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const liblexa_composition *row = &liblexa_compositions[mid];
        if (starter < row->starter
            || (starter == row->starter && second < row->second)) {
            hi = mid;
        } else if (starter > row->starter
            || (starter == row->starter && second > row->second)) {
            lo = mid + 1;
        } else {
            *composed = row->composed;
            break;
        }
    }
    return LXA_OK;
}
