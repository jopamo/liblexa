#ifndef LIBLEXA_HTML_TOKENIZER_SCAN_H
#define LIBLEXA_HTML_TOKENIZER_SCAN_H

#include "liblexa/base.h"
#include "html/tokenizer_classes.h"

/* Borrow a valid byte span; stop before the first byte needing a transition. */
#define LXA_HTML_SCAN_CLASS(name, predicate) \
    static inline size_t lxa_html_scan_##name(lxa_span_t input) \
    { \
        size_t at = 0; \
        while (at < input.length) { \
            uint8_t c = input.data[at]; \
            if (predicate) break; \
            at++; \
        } \
        return at; \
    }
#include "html/tokenizer_scan.def"
#undef LXA_HTML_SCAN_CLASS

#endif
