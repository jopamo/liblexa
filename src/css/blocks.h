#ifndef LIBLEXA_CSS_BLOCKS_H
#define LIBLEXA_CSS_BLOCKS_H

#include "liblexa/css.h"
#include "css/invariant.h"

#define LXA_CSS_BLOCK_NONE UINT32_MAX

/* Flat, append-only simple-block/function index over immutable tokens.
 * open/close are token indices; an unclosed block ends at the EOF token
 * index, or at token count when the list has no explicit EOF.
 * Children belong to their parent's token interval and carry its block ID.
 * No input or token-text pointer is retained. */
typedef struct {
    size_t open;
    size_t close;
    uint32_t parent;
    lxa_css_token_kind_t opener;
} lxa_css_block_t;

struct lxa_css_blocks {
    lxa_vector_t entries;
    lxa_budget_t local_budget;
    lxa_budget_t *budget;
    lxa_budget_kind_t limit_reason;
    size_t end; /* first EOF index, or token count without explicit EOF */
    bool ready;
};

static inline lxa_status_t
lxa_css_blocks_check(lxa_css_blocks_t *blocks)
{
    lxa_status_t status = lxa_css_invariant(blocks->budget != NULL);
    return status == LXA_OK
        ? lxa_css_vector_check(&blocks->entries, sizeof(lxa_css_block_t))
        : status;
}

lxa_status_t lxa_css_blocks_get(lxa_css_blocks_t *blocks, size_t index,
    lxa_css_block_t *out);

#endif
