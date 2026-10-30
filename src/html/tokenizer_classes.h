#ifndef LIBLEXA_HTML_TOKENIZER_CLASSES_H
#define LIBLEXA_HTML_TOKENIZER_CLASSES_H

#include <stdbool.h>
#include <stdint.h>

#define LXA_HTML_BYTE_CLASS(name, predicate) \
    static inline bool lxa_html_byte_##name(uint8_t c) \
    { \
        return (predicate); \
    }
#include "html/tokenizer_classes.def"
#undef LXA_HTML_BYTE_CLASS

#endif
