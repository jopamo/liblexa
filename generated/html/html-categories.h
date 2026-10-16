/* DO NOT EDIT: generated from WHATWG HTML source.html.
 * Revision: 2f441941fc523877bd9d5cd7de3b91a81a00ca2e
 * SHA-256: bec5f8ad394043ac0ee18ba090ec74ae8760b38af5bdf584db58220c299bb9de
 * See LICENSE for the WHATWG standards-data license. */
#ifndef LIBLEXA_GENERATED_HTML_CATEGORIES_H
#define LIBLEXA_GENERATED_HTML_CATEGORIES_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
/* IDs 0..3: invalid and unknown HTML/MathML/SVG names. Known IDs
 * follow namespace/name order. Scope: normal=1, list=2, button=4, table=8. */
typedef struct { const char *tag;
    uint8_t namespace_id, length, special, scope_mask;
} liblexa_html_tag_metadata;
static const liblexa_html_tag_metadata liblexa_html_tag_metadata_table[] = {
    {NULL, 0, 0, 0, 0},
    {NULL, 1, 0, 0, 0},
    {NULL, 2, 0, 0, 0},
    {NULL, 3, 0, 0, 0},
    {"a", 1, 1, 0, 0},
    {"address", 1, 7, 1, 0},
    {"applet", 1, 6, 1, 7},
    {"area", 1, 4, 1, 0},
    {"article", 1, 7, 1, 0},
    {"aside", 1, 5, 1, 0},
    {"b", 1, 1, 0, 0},
    {"base", 1, 4, 1, 0},
    {"basefont", 1, 8, 1, 0},
    {"bgsound", 1, 7, 1, 0},
    {"big", 1, 3, 0, 0},
    {"blockquote", 1, 10, 1, 0},
    {"body", 1, 4, 1, 0},
    {"br", 1, 2, 1, 0},
    {"button", 1, 6, 1, 4},
    {"caption", 1, 7, 1, 7},
    {"center", 1, 6, 1, 0},
    {"code", 1, 4, 0, 0},
    {"col", 1, 3, 1, 0},
    {"colgroup", 1, 8, 1, 0},
    {"dd", 1, 2, 1, 0},
    {"details", 1, 7, 1, 0},
    {"dir", 1, 3, 1, 0},
    {"div", 1, 3, 1, 0},
    {"dl", 1, 2, 1, 0},
    {"dt", 1, 2, 1, 0},
    {"em", 1, 2, 0, 0},
    {"embed", 1, 5, 1, 0},
    {"fieldset", 1, 8, 1, 0},
    {"figcaption", 1, 10, 1, 0},
    {"figure", 1, 6, 1, 0},
    {"font", 1, 4, 0, 0},
    {"footer", 1, 6, 1, 0},
    {"form", 1, 4, 1, 0},
    {"frame", 1, 5, 1, 0},
    {"frameset", 1, 8, 1, 0},
    {"h1", 1, 2, 1, 0},
    {"h2", 1, 2, 1, 0},
    {"h3", 1, 2, 1, 0},
    {"h4", 1, 2, 1, 0},
    {"h5", 1, 2, 1, 0},
    {"h6", 1, 2, 1, 0},
    {"head", 1, 4, 1, 0},
    {"header", 1, 6, 1, 0},
    {"hgroup", 1, 6, 1, 0},
    {"hr", 1, 2, 1, 0},
    {"html", 1, 4, 1, 15},
    {"i", 1, 1, 0, 0},
    {"iframe", 1, 6, 1, 0},
    {"img", 1, 3, 1, 0},
    {"input", 1, 5, 1, 0},
    {"keygen", 1, 6, 1, 0},
    {"li", 1, 2, 1, 0},
    {"link", 1, 4, 1, 0},
    {"listing", 1, 7, 1, 0},
    {"main", 1, 4, 1, 0},
    {"marquee", 1, 7, 1, 7},
    {"menu", 1, 4, 1, 0},
    {"meta", 1, 4, 1, 0},
    {"nav", 1, 3, 1, 0},
    {"nobr", 1, 4, 0, 0},
    {"noembed", 1, 7, 1, 0},
    {"noframes", 1, 8, 1, 0},
    {"noscript", 1, 8, 1, 0},
    {"object", 1, 6, 1, 7},
    {"ol", 1, 2, 1, 2},
    {"optgroup", 1, 8, 0, 0},
    {"option", 1, 6, 0, 0},
    {"p", 1, 1, 1, 0},
    {"param", 1, 5, 1, 0},
    {"plaintext", 1, 9, 1, 0},
    {"pre", 1, 3, 1, 0},
    {"rb", 1, 2, 0, 0},
    {"rp", 1, 2, 0, 0},
    {"rt", 1, 2, 0, 0},
    {"rtc", 1, 3, 0, 0},
    {"s", 1, 1, 0, 0},
    {"script", 1, 6, 1, 0},
    {"search", 1, 6, 1, 0},
    {"section", 1, 7, 1, 0},
    {"select", 1, 6, 1, 7},
    {"small", 1, 5, 0, 0},
    {"source", 1, 6, 1, 0},
    {"strike", 1, 6, 0, 0},
    {"strong", 1, 6, 0, 0},
    {"style", 1, 5, 1, 0},
    {"summary", 1, 7, 1, 0},
    {"table", 1, 5, 1, 15},
    {"tbody", 1, 5, 1, 0},
    {"td", 1, 2, 1, 7},
    {"template", 1, 8, 1, 15},
    {"textarea", 1, 8, 1, 0},
    {"tfoot", 1, 5, 1, 0},
    {"th", 1, 2, 1, 7},
    {"thead", 1, 5, 1, 0},
    {"title", 1, 5, 1, 0},
    {"tr", 1, 2, 1, 0},
    {"track", 1, 5, 1, 0},
    {"tt", 1, 2, 0, 0},
    {"u", 1, 1, 0, 0},
    {"ul", 1, 2, 1, 2},
    {"wbr", 1, 3, 1, 0},
    {"xmp", 1, 3, 1, 0},
    {"annotation-xml", 2, 14, 1, 7},
    {"mi", 2, 2, 1, 7},
    {"mn", 2, 2, 1, 7},
    {"mo", 2, 2, 1, 7},
    {"ms", 2, 2, 1, 7},
    {"mtext", 2, 5, 1, 7},
    {"desc", 3, 4, 1, 7},
    {"foreignObject", 3, 13, 1, 7},
    {"title", 3, 5, 1, 7},
};
#define LIBLEXA_HTML_TAG_METADATA_COUNT 116
#define LIBLEXA_HTML_TAG_NAME_MAX 14
static inline uint16_t
liblexa_html_tag_id(const uint8_t *name, size_t length, unsigned namespace_id)
{
    if (namespace_id < 1 || namespace_id > 3) return 0;
    if (name == NULL || length == 0 || length > LIBLEXA_HTML_TAG_NAME_MAX)
        return (uint16_t) namespace_id;
    switch (namespace_id) {
    case 1:
        switch (length) {
        case 1:
            switch (name[0]) {
            case 97:
                return 4;
            case 98:
                return 10;
            case 105:
                return 51;
            case 112:
                return 72;
            case 115:
                return 80;
            case 117:
                return 103;
            default: return 1;
            }
        case 2:
            switch (name[0]) {
            case 98:
                return (name[1] == 114) ? 17 : 1;
            case 100:
                switch (name[1]) {
                case 100:
                    return 24;
                case 108:
                    return 28;
                case 116:
                    return 29;
                default: return 1;
                }
            case 101:
                return (name[1] == 109) ? 30 : 1;
            case 104:
                switch (name[1]) {
                case 49:
                    return 40;
                case 50:
                    return 41;
                case 51:
                    return 42;
                case 52:
                    return 43;
                case 53:
                    return 44;
                case 54:
                    return 45;
                case 114:
                    return 49;
                default: return 1;
                }
            case 108:
                return (name[1] == 105) ? 56 : 1;
            case 111:
                return (name[1] == 108) ? 69 : 1;
            case 114:
                switch (name[1]) {
                case 98:
                    return 76;
                case 112:
                    return 77;
                case 116:
                    return 78;
                default: return 1;
                }
            case 116:
                switch (name[1]) {
                case 100:
                    return 93;
                case 104:
                    return 97;
                case 114:
                    return 100;
                case 116:
                    return 102;
                default: return 1;
                }
            case 117:
                return (name[1] == 108) ? 104 : 1;
            default: return 1;
            }
        case 3:
            switch (name[0]) {
            case 98:
                return (name[1] == 105 && name[2] == 103) ? 14 : 1;
            case 99:
                return (name[1] == 111 && name[2] == 108) ? 22 : 1;
            case 100:
                switch (name[1]) {
                case 105:
                    switch (name[2]) {
                    case 114:
                        return 26;
                    case 118:
                        return 27;
                    default: return 1;
                    }
                default: return 1;
                }
            case 105:
                return (name[1] == 109 && name[2] == 103) ? 53 : 1;
            case 110:
                return (name[1] == 97 && name[2] == 118) ? 63 : 1;
            case 112:
                return (name[1] == 114 && name[2] == 101) ? 75 : 1;
            case 114:
                return (name[1] == 116 && name[2] == 99) ? 79 : 1;
            case 119:
                return (name[1] == 98 && name[2] == 114) ? 105 : 1;
            case 120:
                return (name[1] == 109 && name[2] == 112) ? 106 : 1;
            default: return 1;
            }
        case 4:
            switch (name[0]) {
            case 97:
                return (name[1] == 114 && name[2] == 101 && name[3] == 97) ? 7 : 1;
            case 98:
                switch (name[1]) {
                case 97:
                    return (name[2] == 115 && name[3] == 101) ? 11 : 1;
                case 111:
                    return (name[2] == 100 && name[3] == 121) ? 16 : 1;
                default: return 1;
                }
            case 99:
                return (name[1] == 111 && name[2] == 100 && name[3] == 101) ? 21 : 1;
            case 102:
                switch (name[1]) {
                case 111:
                    switch (name[2]) {
                    case 110:
                        return (name[3] == 116) ? 35 : 1;
                    case 114:
                        return (name[3] == 109) ? 37 : 1;
                    default: return 1;
                    }
                default: return 1;
                }
            case 104:
                switch (name[1]) {
                case 101:
                    return (name[2] == 97 && name[3] == 100) ? 46 : 1;
                case 116:
                    return (name[2] == 109 && name[3] == 108) ? 50 : 1;
                default: return 1;
                }
            case 108:
                return (name[1] == 105 && name[2] == 110 && name[3] == 107) ? 57 : 1;
            case 109:
                switch (name[1]) {
                case 97:
                    return (name[2] == 105 && name[3] == 110) ? 59 : 1;
                case 101:
                    switch (name[2]) {
                    case 110:
                        return (name[3] == 117) ? 61 : 1;
                    case 116:
                        return (name[3] == 97) ? 62 : 1;
                    default: return 1;
                    }
                default: return 1;
                }
            case 110:
                return (name[1] == 111 && name[2] == 98 && name[3] == 114) ? 64 : 1;
            default: return 1;
            }
        case 5:
            switch (name[0]) {
            case 97:
                return (name[1] == 115 && name[2] == 105 && name[3] == 100 && name[4] == 101) ? 9 : 1;
            case 101:
                return (name[1] == 109 && name[2] == 98 && name[3] == 101 && name[4] == 100) ? 31 : 1;
            case 102:
                return (name[1] == 114 && name[2] == 97 && name[3] == 109 && name[4] == 101) ? 38 : 1;
            case 105:
                return (name[1] == 110 && name[2] == 112 && name[3] == 117 && name[4] == 116) ? 54 : 1;
            case 112:
                return (name[1] == 97 && name[2] == 114 && name[3] == 97 && name[4] == 109) ? 73 : 1;
            case 115:
                switch (name[1]) {
                case 109:
                    return (name[2] == 97 && name[3] == 108 && name[4] == 108) ? 85 : 1;
                case 116:
                    return (name[2] == 121 && name[3] == 108 && name[4] == 101) ? 89 : 1;
                default: return 1;
                }
            case 116:
                switch (name[1]) {
                case 97:
                    return (name[2] == 98 && name[3] == 108 && name[4] == 101) ? 91 : 1;
                case 98:
                    return (name[2] == 111 && name[3] == 100 && name[4] == 121) ? 92 : 1;
                case 102:
                    return (name[2] == 111 && name[3] == 111 && name[4] == 116) ? 96 : 1;
                case 104:
                    return (name[2] == 101 && name[3] == 97 && name[4] == 100) ? 98 : 1;
                case 105:
                    return (name[2] == 116 && name[3] == 108 && name[4] == 101) ? 99 : 1;
                case 114:
                    return (name[2] == 97 && name[3] == 99 && name[4] == 107) ? 101 : 1;
                default: return 1;
                }
            default: return 1;
            }
        case 6:
            switch (name[0]) {
            case 97:
                return (name[1] == 112 && name[2] == 112 && name[3] == 108 && name[4] == 101 && name[5] == 116) ? 6 : 1;
            case 98:
                return (name[1] == 117 && name[2] == 116 && name[3] == 116 && name[4] == 111 && name[5] == 110) ? 18 : 1;
            case 99:
                return (name[1] == 101 && name[2] == 110 && name[3] == 116 && name[4] == 101 && name[5] == 114) ? 20 : 1;
            case 102:
                switch (name[1]) {
                case 105:
                    return (name[2] == 103 && name[3] == 117 && name[4] == 114 && name[5] == 101) ? 34 : 1;
                case 111:
                    return (name[2] == 111 && name[3] == 116 && name[4] == 101 && name[5] == 114) ? 36 : 1;
                default: return 1;
                }
            case 104:
                switch (name[1]) {
                case 101:
                    return (name[2] == 97 && name[3] == 100 && name[4] == 101 && name[5] == 114) ? 47 : 1;
                case 103:
                    return (name[2] == 114 && name[3] == 111 && name[4] == 117 && name[5] == 112) ? 48 : 1;
                default: return 1;
                }
            case 105:
                return (name[1] == 102 && name[2] == 114 && name[3] == 97 && name[4] == 109 && name[5] == 101) ? 52 : 1;
            case 107:
                return (name[1] == 101 && name[2] == 121 && name[3] == 103 && name[4] == 101 && name[5] == 110) ? 55 : 1;
            case 111:
                switch (name[1]) {
                case 98:
                    return (name[2] == 106 && name[3] == 101 && name[4] == 99 && name[5] == 116) ? 68 : 1;
                case 112:
                    return (name[2] == 116 && name[3] == 105 && name[4] == 111 && name[5] == 110) ? 71 : 1;
                default: return 1;
                }
            case 115:
                switch (name[1]) {
                case 99:
                    return (name[2] == 114 && name[3] == 105 && name[4] == 112 && name[5] == 116) ? 81 : 1;
                case 101:
                    switch (name[2]) {
                    case 97:
                        return (name[3] == 114 && name[4] == 99 && name[5] == 104) ? 82 : 1;
                    case 108:
                        return (name[3] == 101 && name[4] == 99 && name[5] == 116) ? 84 : 1;
                    default: return 1;
                    }
                case 111:
                    return (name[2] == 117 && name[3] == 114 && name[4] == 99 && name[5] == 101) ? 86 : 1;
                case 116:
                    switch (name[2]) {
                    case 114:
                        switch (name[3]) {
                        case 105:
                            return (name[4] == 107 && name[5] == 101) ? 87 : 1;
                        case 111:
                            return (name[4] == 110 && name[5] == 103) ? 88 : 1;
                        default: return 1;
                        }
                    default: return 1;
                    }
                default: return 1;
                }
            default: return 1;
            }
        case 7:
            switch (name[0]) {
            case 97:
                switch (name[1]) {
                case 100:
                    return (name[2] == 100 && name[3] == 114 && name[4] == 101 && name[5] == 115 && name[6] == 115) ? 5 : 1;
                case 114:
                    return (name[2] == 116 && name[3] == 105 && name[4] == 99 && name[5] == 108 && name[6] == 101) ? 8 : 1;
                default: return 1;
                }
            case 98:
                return (name[1] == 103 && name[2] == 115 && name[3] == 111 && name[4] == 117 && name[5] == 110 && name[6] == 100) ? 13 : 1;
            case 99:
                return (name[1] == 97 && name[2] == 112 && name[3] == 116 && name[4] == 105 && name[5] == 111 && name[6] == 110) ? 19 : 1;
            case 100:
                return (name[1] == 101 && name[2] == 116 && name[3] == 97 && name[4] == 105 && name[5] == 108 && name[6] == 115) ? 25 : 1;
            case 108:
                return (name[1] == 105 && name[2] == 115 && name[3] == 116 && name[4] == 105 && name[5] == 110 && name[6] == 103) ? 58 : 1;
            case 109:
                return (name[1] == 97 && name[2] == 114 && name[3] == 113 && name[4] == 117 && name[5] == 101 && name[6] == 101) ? 60 : 1;
            case 110:
                return (name[1] == 111 && name[2] == 101 && name[3] == 109 && name[4] == 98 && name[5] == 101 && name[6] == 100) ? 65 : 1;
            case 115:
                switch (name[1]) {
                case 101:
                    return (name[2] == 99 && name[3] == 116 && name[4] == 105 && name[5] == 111 && name[6] == 110) ? 83 : 1;
                case 117:
                    return (name[2] == 109 && name[3] == 109 && name[4] == 97 && name[5] == 114 && name[6] == 121) ? 90 : 1;
                default: return 1;
                }
            default: return 1;
            }
        case 8:
            switch (name[0]) {
            case 98:
                return (name[1] == 97 && name[2] == 115 && name[3] == 101 && name[4] == 102 && name[5] == 111 && name[6] == 110 && name[7] == 116) ? 12 : 1;
            case 99:
                return (name[1] == 111 && name[2] == 108 && name[3] == 103 && name[4] == 114 && name[5] == 111 && name[6] == 117 && name[7] == 112) ? 23 : 1;
            case 102:
                switch (name[1]) {
                case 105:
                    return (name[2] == 101 && name[3] == 108 && name[4] == 100 && name[5] == 115 && name[6] == 101 && name[7] == 116) ? 32 : 1;
                case 114:
                    return (name[2] == 97 && name[3] == 109 && name[4] == 101 && name[5] == 115 && name[6] == 101 && name[7] == 116) ? 39 : 1;
                default: return 1;
                }
            case 110:
                switch (name[1]) {
                case 111:
                    switch (name[2]) {
                    case 102:
                        return (name[3] == 114 && name[4] == 97 && name[5] == 109 && name[6] == 101 && name[7] == 115) ? 66 : 1;
                    case 115:
                        return (name[3] == 99 && name[4] == 114 && name[5] == 105 && name[6] == 112 && name[7] == 116) ? 67 : 1;
                    default: return 1;
                    }
                default: return 1;
                }
            case 111:
                return (name[1] == 112 && name[2] == 116 && name[3] == 103 && name[4] == 114 && name[5] == 111 && name[6] == 117 && name[7] == 112) ? 70 : 1;
            case 116:
                switch (name[1]) {
                case 101:
                    switch (name[2]) {
                    case 109:
                        return (name[3] == 112 && name[4] == 108 && name[5] == 97 && name[6] == 116 && name[7] == 101) ? 94 : 1;
                    case 120:
                        return (name[3] == 116 && name[4] == 97 && name[5] == 114 && name[6] == 101 && name[7] == 97) ? 95 : 1;
                    default: return 1;
                    }
                default: return 1;
                }
            default: return 1;
            }
        case 9:
            return (name[0] == 112 && name[1] == 108 && name[2] == 97 && name[3] == 105 && name[4] == 110 && name[5] == 116 && name[6] == 101 && name[7] == 120 && name[8] == 116) ? 74 : 1;
        case 10:
            switch (name[0]) {
            case 98:
                return (name[1] == 108 && name[2] == 111 && name[3] == 99 && name[4] == 107 && name[5] == 113 && name[6] == 117 && name[7] == 111 && name[8] == 116 && name[9] == 101) ? 15 : 1;
            case 102:
                return (name[1] == 105 && name[2] == 103 && name[3] == 99 && name[4] == 97 && name[5] == 112 && name[6] == 116 && name[7] == 105 && name[8] == 111 && name[9] == 110) ? 33 : 1;
            default: return 1;
            }
        default: return 1;
        }
    case 2:
        switch (length) {
        case 2:
            switch (name[0]) {
            case 109:
                switch (name[1]) {
                case 105:
                    return 108;
                case 110:
                    return 109;
                case 111:
                    return 110;
                case 115:
                    return 111;
                default: return 2;
                }
            default: return 2;
            }
        case 5:
            return (name[0] == 109 && name[1] == 116 && name[2] == 101 && name[3] == 120 && name[4] == 116) ? 112 : 2;
        case 14:
            return (name[0] == 97 && name[1] == 110 && name[2] == 110 && name[3] == 111 && name[4] == 116 && name[5] == 97 && name[6] == 116 && name[7] == 105 && name[8] == 111 && name[9] == 110 && name[10] == 45 && name[11] == 120 && name[12] == 109 && name[13] == 108) ? 107 : 2;
        default: return 2;
        }
    case 3:
        switch (length) {
        case 4:
            return (name[0] == 100 && name[1] == 101 && name[2] == 115 && name[3] == 99) ? 113 : 3;
        case 5:
            return (name[0] == 116 && name[1] == 105 && name[2] == 116 && name[3] == 108 && name[4] == 101) ? 115 : 3;
        case 13:
            return (name[0] == 102 && name[1] == 111 && name[2] == 114 && name[3] == 101 && name[4] == 105 && name[5] == 103 && name[6] == 110 && name[7] == 79 && name[8] == 98 && name[9] == 106 && name[10] == 101 && name[11] == 99 && name[12] == 116) ? 114 : 3;
        default: return 3;
        }
    default: return (uint16_t) namespace_id;
    }
}
#define LIBLEXA_HTML_SPECIAL_COUNT 92
#define LIBLEXA_HTML_SCOPE_BOUNDARY_COUNT 22
static inline bool
liblexa_html_formatting_tag(const uint8_t *name, size_t length)
{
    if (name == NULL) return false;
    switch (length) {
    case 1:
        switch (name[0]) {
        case 97:
            return true;
        case 98:
            return true;
        case 105:
            return true;
        case 115:
            return true;
        case 117:
            return true;
        default: return false;
        }
    case 2:
        switch (name[0]) {
        case 101:
            return name[1] == 109;
        case 116:
            return name[1] == 116;
        default: return false;
        }
    case 3:
        return name[0] == 98 && name[1] == 105 && name[2] == 103;
    case 4:
        switch (name[0]) {
        case 99:
            return name[1] == 111 && name[2] == 100 && name[3] == 101;
        case 102:
            return name[1] == 111 && name[2] == 110 && name[3] == 116;
        case 110:
            return name[1] == 111 && name[2] == 98 && name[3] == 114;
        default: return false;
        }
    case 5:
        return name[0] == 115 && name[1] == 109 && name[2] == 97 && name[3] == 108 && name[4] == 108;
    case 6:
        switch (name[0]) {
        case 115:
            switch (name[1]) {
            case 116:
                switch (name[2]) {
                case 114:
                    switch (name[3]) {
                    case 105:
                        return name[4] == 107 && name[5] == 101;
                    case 111:
                        return name[4] == 110 && name[5] == 103;
                    default: return false;
                    }
                default: return false;
                }
            default: return false;
            }
        default: return false;
        }
    default: return false;
    }
}
/* kind: MathML text=1, HTML=2. HTML annotation-xml requires encoding\n * equal, ASCII case-insensitively, to text/html or application/xhtml+xml. */
typedef struct { const char *namespace_name, *tag; unsigned kind, check_encoding; } liblexa_html_integration_point;
static const liblexa_html_integration_point liblexa_html_integration_points[] = {
    {"mathml", "mi", 1, 0},
    {"mathml", "mn", 1, 0},
    {"mathml", "mo", 1, 0},
    {"mathml", "ms", 1, 0},
    {"mathml", "mtext", 1, 0},
    {"mathml", "annotation-xml", 2, 1},
    {"svg", "desc", 2, 0},
    {"svg", "foreignObject", 2, 0},
    {"svg", "title", 2, 0},
};
#define LIBLEXA_HTML_INTEGRATION_COUNT 9
static const char *const liblexa_html_implied_end_tags[] = {
    "dd",
    "dt",
    "li",
    "optgroup",
    "option",
    "p",
    "rb",
    "rp",
    "rt",
    "rtc",
};
#define LIBLEXA_HTML_IMPLIED_END_COUNT 10
typedef enum {
    LIBLEXA_DOCTYPE_PUBLIC_EXACT, LIBLEXA_DOCTYPE_SYSTEM_EXACT,
    LIBLEXA_DOCTYPE_PUBLIC_PREFIX,
    LIBLEXA_DOCTYPE_PUBLIC_PREFIX_EMPTY_SYSTEM,
    LIBLEXA_DOCTYPE_PUBLIC_PREFIX_NONEMPTY_SYSTEM
} liblexa_html_doctype_condition;
typedef struct { const char *identifier; unsigned length;
    liblexa_html_doctype_condition condition; unsigned limited;
} liblexa_html_doctype_rule;
static const liblexa_html_doctype_rule liblexa_html_doctype_rules[] = {
    {"-//w3o//dtd w3 html strict 3.0//en//", 36, LIBLEXA_DOCTYPE_PUBLIC_EXACT, 0},
    {"-/w3c/dtd html 4.0 transitional/en", 34, LIBLEXA_DOCTYPE_PUBLIC_EXACT, 0},
    {"html", 4, LIBLEXA_DOCTYPE_PUBLIC_EXACT, 0},
    {"http://www.ibm.com/data/dtd/v11/ibmxhtml1-transitional.dtd", 58, LIBLEXA_DOCTYPE_SYSTEM_EXACT, 0},
    {"+//silmaril//dtd html pro v0r11 19970101//", 42, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//as//dtd html 3.0 aswedit + extensions//", 42, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//advasoft ltd//dtd html 3.0 aswedit + extensions//", 52, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html 2.0 level 1//", 31, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html 2.0 level 2//", 31, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html 2.0 strict level 1//", 38, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html 2.0 strict level 2//", 38, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html 2.0 strict//", 30, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html 2.0//", 23, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html 2.1e//", 24, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html 3.0//", 23, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html 3.2 final//", 29, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html 3.2//", 23, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html 3//", 21, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html level 0//", 27, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html level 1//", 27, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html level 2//", 27, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html level 3//", 27, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html strict level 0//", 34, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html strict level 1//", 34, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html strict level 2//", 34, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html strict level 3//", 34, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html strict//", 26, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//ietf//dtd html//", 19, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//metrius//dtd metrius presentational//", 40, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//microsoft//dtd internet explorer 2.0 html strict//", 53, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//microsoft//dtd internet explorer 2.0 html//", 46, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//microsoft//dtd internet explorer 2.0 tables//", 48, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//microsoft//dtd internet explorer 3.0 html strict//", 53, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//microsoft//dtd internet explorer 3.0 html//", 46, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//microsoft//dtd internet explorer 3.0 tables//", 48, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//netscape comm. corp.//dtd html//", 35, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//netscape comm. corp.//dtd strict html//", 42, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//o'reilly and associates//dtd html 2.0//", 42, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//o'reilly and associates//dtd html extended 1.0//", 51, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//o'reilly and associates//dtd html extended relaxed 1.0//", 59, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//sq//dtd html 2.0 hotmetal + extensions//", 43, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//softquad software//dtd hotmetal pro 6.0::19990601::extensions to html 4.0//", 78, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//softquad//dtd hotmetal pro 4.0::19971010::extensions to html 4.0//", 69, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//spyglass//dtd html 2.0 extended//", 36, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//sun microsystems corp.//dtd hotjava html//", 45, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//sun microsystems corp.//dtd hotjava strict html//", 52, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//w3c//dtd html 3 1995-03-24//", 31, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//w3c//dtd html 3.2 draft//", 28, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//w3c//dtd html 3.2 final//", 28, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//w3c//dtd html 3.2//", 22, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//w3c//dtd html 3.2s draft//", 29, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//w3c//dtd html 4.0 frameset//", 31, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//w3c//dtd html 4.0 transitional//", 35, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//w3c//dtd html experimental 19960712//", 40, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//w3c//dtd html experimental 970421//", 38, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//w3c//dtd w3 html//", 21, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//w3o//dtd w3 html 3.0//", 25, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//webtechs//dtd mozilla html 2.0//", 35, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//webtechs//dtd mozilla html//", 31, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 0},
    {"-//w3c//dtd html 4.01 frameset//", 32, LIBLEXA_DOCTYPE_PUBLIC_PREFIX_EMPTY_SYSTEM, 0},
    {"-//w3c//dtd html 4.01 transitional//", 36, LIBLEXA_DOCTYPE_PUBLIC_PREFIX_EMPTY_SYSTEM, 0},
    {"-//w3c//dtd xhtml 1.0 frameset//", 32, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 1},
    {"-//w3c//dtd xhtml 1.0 transitional//", 36, LIBLEXA_DOCTYPE_PUBLIC_PREFIX, 1},
    {"-//w3c//dtd html 4.01 frameset//", 32, LIBLEXA_DOCTYPE_PUBLIC_PREFIX_NONEMPTY_SYSTEM, 1},
    {"-//w3c//dtd html 4.01 transitional//", 36, LIBLEXA_DOCTYPE_PUBLIC_PREFIX_NONEMPTY_SYSTEM, 1},
};
#define LIBLEXA_HTML_DOCTYPE_RULE_COUNT 65
#endif
