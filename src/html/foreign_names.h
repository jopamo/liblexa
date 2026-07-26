#ifndef LIBLEXA_HTML_FOREIGN_NAMES_H
#define LIBLEXA_HTML_FOREIGN_NAMES_H

#include "liblexa/base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal tree-construction adjustments for already ASCII-lowercased HTML
 * tokens. Known results borrow static generated storage; unknown results
 * borrow normalized.data until its owner releases it. No allocation or
 * retained state. The caller copies names needed beyond the token callback
 * into document storage. HTML names require no adjustment.
 * Invalid spans (NULL with nonzero length) or NULL out return ARGUMENT
 * without modifying out. A NULL, zero-length span is valid.
 */
lxa_status_t lxa_html_svg_tag_name(lxa_span_t normalized, lxa_span_t *out);
lxa_status_t lxa_html_svg_attribute_name(lxa_span_t normalized, lxa_span_t *out);
lxa_status_t lxa_html_mathml_attribute_name(lxa_span_t normalized, lxa_span_t *out);

#ifdef __cplusplus
}
#endif

#endif /* LIBLEXA_HTML_FOREIGN_NAMES_H */
