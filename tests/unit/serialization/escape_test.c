#include "liblexa/serialize.h"
#include "tests/support/test_check.h"
#include <string.h>

typedef struct {
    uint8_t bytes[256];
    size_t length;
    size_t limit;
    size_t fail_after;
    lxa_status_t fail_status;
} sink_t;

static lxa_write_result_t
write_bytes(void *context, lxa_span_t bytes)
{
    sink_t *sink = context;
    lxa_write_result_t result;
    size_t count = bytes.length;
    if (count > sink->limit) {
        count = sink->limit;
    }
    if (count > sizeof(sink->bytes) - sink->length) {
        count = sizeof(sink->bytes) - sink->length;
    }
    if (sink->fail_status != LXA_OK && sink->length >= sink->fail_after) {
        result.consumed = 0;
        result.status = sink->fail_status;
        return result;
    }
    if (sink->fail_status != LXA_OK && count > sink->fail_after - sink->length) {
        count = sink->fail_after - sink->length;
    }
    if (count != 0) {
        memcpy(sink->bytes + sink->length, bytes.data, count);
    }
    sink->length += count;
    result.consumed = count;
    result.status = sink->fail_status != LXA_OK && sink->length == sink->fail_after
                    ? sink->fail_status : LXA_OK;
    return result;
}

static void
check(const char *input, size_t length, lxa_escape_context_t mode,
      const char *expected)
{
    sink_t sink = {{0}, 0, 2, 0, LXA_OK};
    lxa_span_t span = {(const uint8_t *) input, length};
    TEST_CHECK(lxa_html_escape(span, mode, write_bytes, &sink) == LXA_OK);
    TEST_CHECK(sink.length == strlen(expected));
    TEST_CHECK(memcmp(sink.bytes, expected, sink.length) == 0);
}

int
main(void)
{
    const char input[] = "a&<>\"'\xc2\xa0\xc2z";
    lxa_span_t span = {(const uint8_t *) "a&b", 3};
    sink_t sink = {{0}, 0, 1, 2, LXA_ERROR_NO_MEMORY};
    lxa_span_t invalid = {NULL, 1};
    check("<>&", 3, LXA_ESCAPE_TEXT, "&lt;&gt;&amp;");
    check(input, sizeof(input) - 1, LXA_ESCAPE_TEXT,
          "a&amp;&lt;&gt;\"'&nbsp;\xc2z");
    check(input, sizeof(input) - 1, LXA_ESCAPE_ATTRIBUTE,
          "a&amp;&lt;&gt;&quot;'&nbsp;\xc2z");
    {
        const char binary[] = {'a', 0, 'b'};
        sink_t binary_sink = {{0}, 0, 3, 0, LXA_OK};
        lxa_span_t binary_span = {(const uint8_t *) binary, sizeof(binary)};
        TEST_CHECK(lxa_html_escape(binary_span, LXA_ESCAPE_TEXT, write_bytes,
                               &binary_sink) == LXA_OK);
        TEST_CHECK(binary_sink.length == sizeof(binary));
        TEST_CHECK(memcmp(binary_sink.bytes, binary, sizeof(binary)) == 0);
    }
    TEST_CHECK(lxa_html_escape(span, LXA_ESCAPE_TEXT, write_bytes, &sink)
           == LXA_ERROR_NO_MEMORY);
    TEST_CHECK(sink.length == 2 && memcmp(sink.bytes, "a&", 2) == 0);
    sink.fail_status = LXA_OK;
    sink.limit = 0;
    TEST_CHECK(lxa_html_escape(span, LXA_ESCAPE_TEXT, write_bytes, &sink)
           == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_html_escape(invalid, LXA_ESCAPE_TEXT, write_bytes, &sink)
           == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_html_escape(span, (lxa_escape_context_t) 7, write_bytes, &sink)
           == LXA_ERROR_ARGUMENT);
    TEST_CHECK(lxa_html_escape(span, LXA_ESCAPE_TEXT, NULL, &sink)
           == LXA_ERROR_ARGUMENT);
    invalid.length = 0;
    TEST_CHECK(lxa_html_escape(invalid, LXA_ESCAPE_TEXT, write_bytes, &sink)
           == LXA_OK);
    return 0;
}
