#include <string.h>
#include "dom/node_store_internal.h"

static bool
xml_space(uint8_t c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool
next_scalar(lxa_span_t data, size_t *pos, uint32_t *out)
{
    size_t start = *pos, n, i;
    uint32_t cp, minimum;
    uint8_t c;
    if (start == data.length) return false;
    c = data.data[start];
    if (c < 0x80) {
        *out = c;
        *pos = start + 1;
        return true;
    }
    if (c >= 0xc2 && c <= 0xdf) {
        n = 2; cp = c & 0x1fu; minimum = 0x80;
    } else if (c >= 0xe0 && c <= 0xef) {
        n = 3; cp = c & 0x0fu; minimum = 0x800;
    } else if (c >= 0xf0 && c <= 0xf4) {
        n = 4; cp = c & 0x07u; minimum = 0x10000;
    } else {
        return false;
    }
    if (n > data.length - start) return false;
    for (i = 1; i < n; ++i) {
        c = data.data[start + i];
        if ((c & 0xc0u) != 0x80u) return false;
        cp = (cp << 6) | (c & 0x3fu);
    }
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
        return false;
    *out = cp;
    *pos = start + n;
    return true;
}

static bool
xml_name_start(uint32_t c)
{
    return c == ':' || c == '_' || (c >= 'A' && c <= 'Z')
        || (c >= 'a' && c <= 'z') || (c >= 0xc0 && c <= 0xd6)
        || (c >= 0xd8 && c <= 0xf6) || (c >= 0xf8 && c <= 0x2ff)
        || (c >= 0x370 && c <= 0x37d) || (c >= 0x37f && c <= 0x1fff)
        || (c >= 0x200c && c <= 0x200d) || (c >= 0x2070 && c <= 0x218f)
        || (c >= 0x2c00 && c <= 0x2fef) || (c >= 0x3001 && c <= 0xd7ff)
        || (c >= 0xf900 && c <= 0xfdcf) || (c >= 0xfdf0 && c <= 0xfffd)
        || (c >= 0x10000 && c <= 0xeffff);
}

static bool
xml_name_char(uint32_t c)
{
    return xml_name_start(c) || c == '-' || c == '.'
        || (c >= '0' && c <= '9') || c == 0xb7
        || (c >= 0x300 && c <= 0x36f) || (c >= 0x203f && c <= 0x2040);
}

static bool
xml_reference_char(uint32_t c)
{
    return c == 9 || c == 10 || c == 13
        || (c >= 0x20 && c <= 0xd7ff) || (c >= 0xe000 && c <= 0xfffd)
        || (c >= 0x10000 && c <= 0x10ffff);
}

static bool
reference(lxa_span_t data, size_t *pos, uint32_t *out)
{
    static const struct { const char *text; size_t length; uint32_t cp; } named[] = {
        {"amp;", 4, '&'}, {"lt;", 3, '<'}, {"gt;", 3, '>'},
        {"quot;", 5, '"'}, {"apos;", 5, '\''}
    };
    size_t i, start;
    uint32_t cp = 0, base = 10;
    ++*pos; /* '&' */
    if (*pos == data.length) return false;
    if (data.data[*pos] != '#') {
        for (i = 0; i < sizeof(named) / sizeof(named[0]); ++i) {
            if (named[i].length <= data.length - *pos
                && memcmp(data.data + *pos, named[i].text, named[i].length) == 0) {
                *pos += named[i].length;
                *out = named[i].cp;
                return true;
            }
        }
        return false;
    }
    ++*pos;
    if (*pos < data.length && data.data[*pos] == 'x') {
        base = 16;
        ++*pos;
    }
    start = *pos;
    while (*pos < data.length && data.data[*pos] != ';') {
        uint8_t c = data.data[*pos];
        uint32_t digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') digit = c - 'a' + 10u;
        else if (base == 16 && c >= 'A' && c <= 'F') digit = c - 'A' + 10u;
        else return false;
        if (cp > (0x10ffffu - digit) / base) return false;
        cp = cp * base + digit;
        ++*pos;
    }
    if (*pos == start || *pos == data.length || !xml_reference_char(cp))
        return false;
    ++*pos;
    *out = cp;
    return true;
}

static bool
match_scalar(lxa_span_t value, size_t *pos, uint32_t cp)
{
    uint8_t bytes[4];
    size_t n;
    if (cp < 0x80) {
        bytes[0] = (uint8_t) cp; n = 1;
    } else if (cp < 0x800) {
        bytes[0] = (uint8_t) (0xc0u | (cp >> 6));
        bytes[1] = (uint8_t) (0x80u | (cp & 0x3fu)); n = 2;
    } else if (cp < 0x10000) {
        bytes[0] = (uint8_t) (0xe0u | (cp >> 12));
        bytes[1] = (uint8_t) (0x80u | ((cp >> 6) & 0x3fu));
        bytes[2] = (uint8_t) (0x80u | (cp & 0x3fu)); n = 3;
    } else {
        bytes[0] = (uint8_t) (0xf0u | (cp >> 18));
        bytes[1] = (uint8_t) (0x80u | ((cp >> 12) & 0x3fu));
        bytes[2] = (uint8_t) (0x80u | ((cp >> 6) & 0x3fu));
        bytes[3] = (uint8_t) (0x80u | (cp & 0x3fu)); n = 4;
    }
    if (n > value.length - *pos || memcmp(value.data + *pos, bytes, n) != 0)
        return false;
    *pos += n;
    return true;
}

lxa_status_t
lxa_dom_nodes_pi_attribute_matches(lxa_dom_nodes_t *nodes, lxa_dom_ref_t pi,
    lxa_span_t name, lxa_span_t value, bool *matches)
{
    lxa_span_t target, data;
    lxa_table_t *names = NULL;
    size_t pos = 0, work;
    bool result = false;
    lxa_status_t status;
    if (nodes != NULL) {
        nodes->limit_reason = LXA_BUDGET_COUNT;
    }
    if (matches == NULL) return LXA_ERROR_ARGUMENT;
    *matches = false;
    if (nodes == NULL || (name.data == NULL && name.length != 0)
        || (value.data == NULL && value.length != 0))
        return LXA_ERROR_ARGUMENT;
    status = lxa_dom_nodes_pi_read(nodes, pi, &target, &data);
    if (status != LXA_OK) return status;
    status = lxa_size_mul(data.length, 3, &work);
    if (status == LXA_OK)
        status = lxa_budget_charge(nodes->budget, LXA_BUDGET_WORK, work,
                                  &nodes->limit_reason);
    if (status != LXA_OK) return status;
    while (pos < data.length && xml_space(data.data[pos])) ++pos;
    while (pos < data.length) {
        size_t start = pos, end, matched = 0;
        uint32_t cp;
        uint8_t quote;
        lxa_span_t key;
        bool candidate, equal, replaced;
        if (!next_scalar(data, &pos, &cp) || !xml_name_start(cp)) goto done;
        while (pos < data.length) {
            end = pos;
            if (!next_scalar(data, &end, &cp)) goto done;
            if (!xml_name_char(cp)) break;
            pos = end;
        }
        key = (lxa_span_t){data.data + start, pos - start};
        candidate = key.length == name.length
            && memcmp(key.data, name.data, key.length) == 0;
        equal = candidate;
        while (pos < data.length && xml_space(data.data[pos])) ++pos;
        if (pos == data.length || data.data[pos++] != '=') goto done;
        while (pos < data.length && xml_space(data.data[pos])) ++pos;
        if (pos == data.length
            || (data.data[pos] != '"' && data.data[pos] != '\'')) goto done;
        quote = data.data[pos++];
        while (pos < data.length && data.data[pos] != quote) {
            if (data.data[pos] == '<') goto done;
            if (data.data[pos] == '&') {
                if (!reference(data, &pos, &cp)) goto done;
            } else if (!next_scalar(data, &pos, &cp)) {
                goto done;
            }
            if (equal && !match_scalar(value, &matched, cp)) equal = false;
        }
        if (pos == data.length) goto done;
        ++pos;
        if (names == NULL) {
            status = lxa_table_create_with_budget(&nodes->allocator,
                nodes->budget, NULL, NULL, &names);
            if (status == LXA_ERROR_LIMIT) nodes->limit_reason = LXA_BUDGET_MEMORY;
            if (status != LXA_OK) goto done;
        }
        status = lxa_table_set(names, key, NULL, &replaced, NULL);
        if (status != LXA_OK) {
            nodes->limit_reason = lxa_table_limit_reason(names);
            goto done;
        }
        if (replaced) goto done;
        if (candidate) result = equal && matched == value.length;
        if (pos < data.length && !xml_space(data.data[pos])) goto done;
        while (pos < data.length && xml_space(data.data[pos])) ++pos;
    }
    *matches = result;
done:
    lxa_table_destroy(names);
    return status;
}
