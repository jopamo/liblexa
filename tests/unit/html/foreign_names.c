/* Copyright (C) 2026 Paul Moses */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "html/foreign_names.h"
#include "html-tags.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
        abort(); \
    } \
} while (0)

typedef lxa_status_t (*adjust_fn)(lxa_span_t, lxa_span_t *);

static lxa_span_t
view(const char *text)
{
    lxa_span_t result = {(const uint8_t *) text, strlen(text)};
    return result;
}

static void
check_table(adjust_fn fn, const liblexa_svg_tag_name *table, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        lxa_span_t input = view(table[i].lowercase), output = {NULL, 0};
        CHECK(fn(input, &output) == LXA_OK);
        CHECK(output.length == strlen(table[i].adjusted));
        CHECK(memcmp(output.data, table[i].adjusted, output.length) == 0);
        CHECK(output.data != input.data);
    }
}

static void
check_borrowed(adjust_fn fn)
{
    const uint8_t unknown[] = {'v', 'i', 'e', 'w', 'b', 'o', 0, 'x'};
    lxa_span_t input = {unknown, 6}, output = {NULL, 0};
    CHECK(fn(input, &output) == LXA_OK);
    CHECK(output.data == unknown && output.length == 6);
    input.length = sizeof(unknown);
    CHECK(fn(input, &output) == LXA_OK);
    CHECK(output.data == unknown && output.length == sizeof(unknown));
    /* A very large declared length must never trigger a read of that span. */
    input.length = SIZE_MAX;
    CHECK(fn(input, &output) == LXA_OK);
    CHECK(output.data == unknown && output.length == SIZE_MAX);
    input.data = NULL;
    input.length = 0;
    CHECK(fn(input, &output) == LXA_OK);
    CHECK(output.data == NULL && output.length == 0);
}

static void
check_invalid(adjust_fn fn)
{
    const uint8_t sentinel[] = "keep";
    lxa_span_t output = {sentinel, sizeof(sentinel)}, invalid = {NULL, 1};
    CHECK(fn(invalid, &output) == LXA_ERROR_ARGUMENT);
    CHECK(output.data == sentinel && output.length == sizeof(sentinel));
    CHECK(fn(view("viewbox"), NULL) == LXA_ERROR_ARGUMENT);
}

int
main(void)
{
    lxa_span_t output = {NULL, 0};
    check_table(lxa_html_svg_tag_name, liblexa_svg_tag_names,
                LIBLEXA_SVG_TAG_NAME_COUNT);
    check_table(lxa_html_svg_attribute_name, liblexa_svg_attribute_names,
                LIBLEXA_SVG_ATTRIBUTE_NAME_COUNT);
    CHECK(lxa_html_mathml_attribute_name(view("definitionurl"), &output) == LXA_OK);
    CHECK(output.length == sizeof("definitionURL") - 1);
    CHECK(memcmp(output.data, "definitionURL", output.length) == 0);
    check_borrowed(lxa_html_svg_tag_name);
    check_borrowed(lxa_html_svg_attribute_name);
    check_borrowed(lxa_html_mathml_attribute_name);
    check_invalid(lxa_html_svg_tag_name);
    check_invalid(lxa_html_svg_attribute_name);
    check_invalid(lxa_html_mathml_attribute_name);
    /* Namespace selection belongs to the tree: these inputs are different. */
    CHECK(lxa_html_svg_tag_name(view("viewbox"), &output) == LXA_OK);
    CHECK(output.length == sizeof("viewbox") - 1);
    CHECK(lxa_html_svg_attribute_name(view("viewbox"), &output) == LXA_OK);
    CHECK(output.length == sizeof("viewBox") - 1);
    return 0;
}
