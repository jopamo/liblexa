/* Copyright (C) 2026 Paul Moses */
#include <stdio.h>
#include <stdlib.h>

#include "liblexa/unicode.h"
#include "idna-table.h"
#include "joining-types.h"
#include "normalization-table.h"

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
        abort(); \
    } \
} while (0)

int
main(void)
{
    lxa_joining_type_t joining;
    lxa_idna_entry_t entry;
    lxa_normalization_properties_t properties;
    uint32_t decomposed[256];
    size_t required;
    uint32_t composed;
    size_t i;
    for (i = 0; i < LIBLEXA_JOINING_RANGE_COUNT; i++) {
        const liblexa_joining_range *row = &liblexa_joining_ranges[i];
        CHECK(lxa_unicode_joining_type(row->first, &joining) == LXA_OK
              && joining == (lxa_joining_type_t) row->type);
        CHECK(lxa_unicode_joining_type(row->last, &joining) == LXA_OK
              && joining == (lxa_joining_type_t) row->type);
    }
    for (i = 0; i < LIBLEXA_IDNA_RANGE_COUNT; i++) {
        const liblexa_idna_range *row = &liblexa_idna_ranges[i];
        uint32_t boundary[2] = {row->first, row->last};
        size_t j;
        for (j = 0; j < 2; j++) {
            CHECK(lxa_unicode_idna_entry(boundary[j], &entry) == LXA_OK
                  && entry.status == (lxa_idna_status_t) row->status
                  && entry.length == row->count
                  && entry.idna2008 == row->idna2008
                  && (entry.length == 0 ? entry.mapping == NULL
                      : entry.mapping != NULL
                        && entry.mapping[0] == liblexa_idna_mappings[row->offset]));
        }
    }
    CHECK(lxa_unicode_joining_type('a', &joining) == LXA_OK
          && joining == LXA_JOINING_U);
    CHECK(lxa_unicode_joining_type(0x0640, &joining) == LXA_OK
          && joining == LXA_JOINING_C);
    CHECK(lxa_unicode_joining_type(0x0620, &joining) == LXA_OK
          && joining == LXA_JOINING_D);
    CHECK(lxa_unicode_joining_type(0x200c, &joining) == LXA_OK
          && joining == LXA_JOINING_U);
    CHECK(lxa_unicode_joining_type(0x200d, &joining) == LXA_OK
          && joining == LXA_JOINING_C);
    CHECK(lxa_unicode_joining_type(0x110000, &joining) == LXA_ERROR_ARGUMENT
          && joining == LXA_JOINING_U);
    CHECK(lxa_unicode_joining_type('a', NULL) == LXA_ERROR_ARGUMENT);

    CHECK(lxa_unicode_idna_entry('A', &entry) == LXA_OK
          && entry.status == LXA_IDNA_MAPPED && entry.length == 1
          && entry.mapping[0] == 'a');
    CHECK(lxa_unicode_idna_entry('a', &entry) == LXA_OK
          && entry.status == LXA_IDNA_VALID && entry.length == 0
          && entry.mapping == NULL);
    CHECK(lxa_unicode_idna_entry(0x00df, &entry) == LXA_OK
          && entry.status == LXA_IDNA_DEVIATION && entry.length == 2
          && entry.mapping[0] == 's' && entry.mapping[1] == 's');
    CHECK(lxa_unicode_idna_entry(0xd800, &entry) == LXA_OK
          && entry.status == LXA_IDNA_DISALLOWED);
    CHECK(lxa_unicode_idna_entry(0x10ffff, &entry) == LXA_OK
          && entry.status == LXA_IDNA_DISALLOWED);
    CHECK(lxa_unicode_idna_entry(0x110000, &entry) == LXA_ERROR_ARGUMENT
          && entry.mapping == NULL && entry.length == 0);
    CHECK(lxa_unicode_idna_entry('a', NULL) == LXA_ERROR_ARGUMENT);

    for (i = 0; i < LIBLEXA_NORMALIZATION_COUNT; i++) {
        const liblexa_normalization_entry *row = &liblexa_normalization[i];
        CHECK(lxa_unicode_normalization_properties(row->cp, &properties)
              == LXA_OK && properties.combining_class == row->ccc);
    }
    for (i = 0; i < LIBLEXA_QUICK_CHECK_COUNT; i++) {
        const liblexa_quick_check *row = &liblexa_quick_checks[i];
        CHECK(lxa_unicode_normalization_properties(row->cp, &properties)
              == LXA_OK && properties.quick_checks == row->checks);
    }
    CHECK(lxa_unicode_normalization_properties('a', &properties) == LXA_OK
          && properties.combining_class == 0 && properties.quick_checks == 0);
    CHECK(lxa_unicode_normalization_properties(0x0301, &properties) == LXA_OK
          && properties.combining_class == 230);
    CHECK(lxa_unicode_normalization_properties(0x110000, &properties)
          == LXA_ERROR_ARGUMENT && properties.combining_class == 0
          && properties.quick_checks == 0);
    CHECK(lxa_unicode_normalization_properties(0, NULL)
          == LXA_ERROR_ARGUMENT);
    for (i = 0; i < LIBLEXA_NORMALIZATION_COUNT; i++) {
        const liblexa_normalization_entry *row = &liblexa_normalization[i];
        size_t j;
        CHECK(lxa_unicode_decompose_once(row->cp, true, decomposed,
                                         256, &required) == LXA_OK);
        CHECK(required == (row->length != 0 ? row->length : 1));
        for (j = 0; j < row->length; j++) {
            CHECK(decomposed[j] == liblexa_decompositions[row->offset + j]);
        }
        if (row->compatibility) {
            CHECK(lxa_unicode_decompose_once(row->cp, false, decomposed,
                                             256, &required) == LXA_OK
                  && required == 1 && decomposed[0] == row->cp);
        }
    }
    CHECK(lxa_unicode_decompose_once(0xac00, false, decomposed, 256,
                                     &required) == LXA_OK
          && required == 2 && decomposed[0] == 0x1100
          && decomposed[1] == 0x1161);
    CHECK(lxa_unicode_decompose_once(0xac01, false, decomposed, 256,
                                     &required) == LXA_OK
          && required == 2 && decomposed[0] == 0xac00
          && decomposed[1] == 0x11a8);
    decomposed[0] = 77;
    CHECK(lxa_unicode_decompose_once(0x00e9, false, decomposed, 1,
                                     &required) == LXA_ERROR_OVERFLOW
          && required == 2 && decomposed[0] == 77);
    CHECK(lxa_unicode_decompose_once(0x00e9, false, NULL, 0,
                                     &required) == LXA_ERROR_OVERFLOW
          && required == 2);
    CHECK(lxa_unicode_decompose_once(0xd800, false, decomposed, 256,
                                     &required) == LXA_ERROR_ARGUMENT
          && required == 0);
    for (i = 0; i < LIBLEXA_COMPOSITION_COUNT; i++) {
        const liblexa_composition *row = &liblexa_compositions[i];
        CHECK(lxa_unicode_compose_pair(row->starter, row->second,
                                       &composed) == LXA_OK
              && composed == row->composed);
    }
    CHECK(lxa_unicode_compose_pair(0x1100, 0x1161, &composed) == LXA_OK
          && composed == 0xac00);
    CHECK(lxa_unicode_compose_pair(0x1112, 0x1175, &composed) == LXA_OK
          && composed == 0xd788);
    CHECK(lxa_unicode_compose_pair(0xac00, 0x11a8, &composed) == LXA_OK
          && composed == 0xac01);
    CHECK(lxa_unicode_compose_pair(0xd7a3, 0x11c2, &composed) == LXA_OK
          && composed == UINT32_MAX);
    CHECK(lxa_unicode_compose_pair('a', 'b', &composed) == LXA_OK
          && composed == UINT32_MAX);
    CHECK(lxa_unicode_compose_pair(0xd800, 0x301, &composed)
          == LXA_ERROR_ARGUMENT && composed == UINT32_MAX);
    CHECK(lxa_unicode_compose_pair('a', 'b', NULL) == LXA_ERROR_ARGUMENT);
    return 0;
}
