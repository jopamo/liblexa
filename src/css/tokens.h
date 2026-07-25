/* Copyright (C) 2026 Paul Moses */
#ifndef LIBLEXA_CSS_TOKENS_H
#define LIBLEXA_CSS_TOKENS_H

#include "liblexa/css.h"

struct lxa_css_tokens {
    lxa_buffer_t text;
    lxa_vector_t entries;
    size_t charged_tokens; /* high-water published count, never refunded */
    lxa_budget_kind_t limit_reason;
};

typedef struct {
    lxa_css_token_t token;
    size_t text_offset, text_length;
} lxa_css_token_entry_t;

/* Scanner construction: text must not overlap this list's storage. */
lxa_status_t lxa_css_tokens_append(lxa_css_tokens_t *tokens,
                                   lxa_css_token_t token, lxa_span_t text);

#endif
