#include <string.h>

#include "html/foreign_names.h"
#include "html-tags.h"

static lxa_status_t
adjust(lxa_span_t name, lxa_span_t *out, const liblexa_svg_tag_name *table,
       size_t count)
{
    size_t i;
    if (out == NULL || (name.data == NULL && name.length != 0))
        return LXA_ERROR_ARGUMENT;
    for (i = 0; i < count; ++i) {
        size_t length = strlen(table[i].lowercase);
        if (name.length == length
            && memcmp(name.data, table[i].lowercase, length) == 0) {
            out->data = (const uint8_t *) table[i].adjusted;
            out->length = strlen(table[i].adjusted);
            return LXA_OK;
        }
    }
    *out = name;
    return LXA_OK;
}

lxa_status_t
lxa_html_svg_tag_name(lxa_span_t normalized, lxa_span_t *out)
{
    return adjust(normalized, out, liblexa_svg_tag_names,
                  LIBLEXA_SVG_TAG_NAME_COUNT);
}

lxa_status_t
lxa_html_svg_attribute_name(lxa_span_t normalized, lxa_span_t *out)
{
    return adjust(normalized, out, liblexa_svg_attribute_names,
                  LIBLEXA_SVG_ATTRIBUTE_NAME_COUNT);
}

lxa_status_t
lxa_html_mathml_attribute_name(lxa_span_t normalized, lxa_span_t *out)
{
    static const uint8_t adjusted[] = "definitionURL";
    static const uint8_t lower[] = "definitionurl";
    if (out == NULL || (normalized.data == NULL && normalized.length != 0))
        return LXA_ERROR_ARGUMENT;
    if (normalized.length == sizeof(lower) - 1
        && memcmp(normalized.data, lower, sizeof(lower) - 1) == 0) {
        out->data = adjusted;
        out->length = sizeof(adjusted) - 1;
    }
    else *out = normalized;
    return LXA_OK;
}
