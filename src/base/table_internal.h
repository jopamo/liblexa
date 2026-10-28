#ifndef LIBLEXA_BASE_TABLE_INTERNAL_H
#define LIBLEXA_BASE_TABLE_INTERNAL_H

#include "liblexa/base.h"

typedef struct {
    lxa_table_t *owner;
    size_t slot;
    const uint8_t *key;
    void *value;
} lxa_table_insert_t;

/* Optional rollback ownership for a newly inserted key, not a replacement.
 * Outputs are cleared on failure. No table mutation may intervene before
 * rollback; find and failed delete leave the token valid. Discard the token
 * on commit. Do not copy it or retain it across table destruction.
 * Values remain caller-owned. */
lxa_status_t lxa_table_set_with_rollback(lxa_table_t *table, lxa_span_t key,
    void *value, bool *replaced, void **previous, lxa_table_insert_t *inserted);

/* Remove this owned insertion without allocating or charging WORK. Preserve
 * spent WORK and retained slot capacity, release the key's MEMORY charge,
 * then consume the token. Invalid ownership leaves the table unchanged. */
lxa_status_t lxa_table_rollback_insert(lxa_table_t *table,
    lxa_table_insert_t *inserted);

#endif
