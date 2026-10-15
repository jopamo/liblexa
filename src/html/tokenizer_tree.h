#ifndef LIBLEXA_HTML_TOKENIZER_TREE_H
#define LIBLEXA_HTML_TOKENIZER_TREE_H

#include "html/tokenizer.h"

/* Borrow the shared tokenizer lifetime with tree_on_token and no diagnostics. */
lxa_html_result_t lxa_html_tree_tokenizer_feed(
    lxa_html_tokenizer_t *tokenizer, lxa_span_t input);
/* Prepaid text may borrow decoded_utf8 until tree_on_token returns.
 * The tree copies retained bytes before returning. */
lxa_html_result_t lxa_html_tree_tokenizer_feed_prepaid(
    lxa_html_tokenizer_t *tokenizer, lxa_span_t decoded_utf8);
lxa_html_result_t lxa_html_tree_tokenizer_finish(
    lxa_html_tokenizer_t *tokenizer);

#endif
