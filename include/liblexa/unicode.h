/* Copyright (C) 2026 Paul Moses */
#ifndef LIBLEXA_UNICODE_H
#define LIBLEXA_UNICODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "liblexa/base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* U is the implicit default for code points absent from the UCD table. */
typedef enum {
    LXA_JOINING_U = 'U',
    LXA_JOINING_C = 'C',
    LXA_JOINING_D = 'D',
    LXA_JOINING_L = 'L',
    LXA_JOINING_R = 'R',
    LXA_JOINING_T = 'T'
} lxa_joining_type_t;

typedef enum {
    LXA_IDNA_VALID,
    LXA_IDNA_IGNORED,
    LXA_IDNA_MAPPED,
    LXA_IDNA_DEVIATION,
    LXA_IDNA_DISALLOWED
} lxa_idna_status_t;

typedef struct {
    lxa_idna_status_t status;
    const uint32_t *mapping; /* immutable static data; NULL when length is 0 */
    uint8_t length;
    uint8_t idna2008; /* 0 = default, 1 = NV8, 2 = XV8 */
} lxa_idna_entry_t;

/* Outputs are initialized on entry. Code points outside Unicode's range
 * return ARGUMENT. Surrogates are classified by the pinned UCD/UTS #46
 * tables as disallowed/non-joining rather than being silently normalized.
 */
lxa_status_t lxa_unicode_joining_type(uint32_t cp, lxa_joining_type_t *out);
lxa_status_t lxa_unicode_idna_entry(uint32_t cp, lxa_idna_entry_t *out);

/* Quick-check bits: NFC, NFD, NFKC, NFKD at shifts 0, 2, 4, 6.
 * In each pair 0=Yes, 1=No, 2=Maybe. This is a property lookup only:
 * quick-check Yes does not replace canonical-order verification, and Hangul
 * decomposition/composition require their separate algorithmic rules.
 */
typedef struct {
    uint8_t combining_class;
    uint8_t quick_checks;
} lxa_normalization_properties_t;
lxa_status_t lxa_unicode_normalization_properties(
    uint32_t cp, lxa_normalization_properties_t *out);

/* One decomposition step from the pinned UCD (or the algorithmic Hangul
 * step). Canonical mode leaves compatibility-only characters unchanged.
 * On insufficient capacity, required receives the exact size and output
 * stays untouched. Recurse and reorder combining marks to normalize a string;
 * this primitive does neither.
 */
lxa_status_t lxa_unicode_decompose_once(uint32_t cp, bool compatibility,
                                         uint32_t *output, size_t capacity,
                                         size_t *required);
/* Canonical pair composition, including algorithmic Hangul. UINT32_MAX
 * means no composition; valid scalar pairs still return LXA_OK.
 */
lxa_status_t lxa_unicode_compose_pair(uint32_t starter, uint32_t second,
                                       uint32_t *composed);

#ifdef __cplusplus
}
#endif
#endif /* LIBLEXA_UNICODE_H */
