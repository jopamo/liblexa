#ifndef LIBLEXA_HTML_SOURCE_CONSUMER_H
#define LIBLEXA_HTML_SOURCE_CONSUMER_H

#include "liblexa/html_source.h"

lxa_html_result_t lxa_html_source_result(lxa_status_t status);

/* Borrow an initialized budget with no accepted INPUT or DOM nodes. Owned
 * storage is released on return; cumulative charges remain with the caller. */
lxa_status_t lxa_html_extract_attributes_with_budget(
    const lxa_allocator_t *allocator, lxa_budget_t *budget, lxa_span_t input,
    const lxa_html_extract_options_t *options, const lxa_html_source_target_t *targets,
    size_t count, lxa_html_source_attribute_callback_t callback, void *context,
    lxa_html_result_t *parser_result, lxa_limit_reason_t *reason);

#endif
