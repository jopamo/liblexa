#include <stdio.h>
#include <string.h>

#include "html/tokenizer.h"

static unsigned char input[4096], output[65536];
static size_t used;

static lxa_status_t
append(const void *bytes, size_t length)
{
    if (length > sizeof(output) - used) return LXA_ERROR_LIMIT;
    if (length != 0) memcpy(output + used, bytes, length);
    used += length;
    return LXA_OK;
}

static lxa_status_t
literal(const char *text)
{
    return append(text, strlen(text));
}

static lxa_status_t
quoted(lxa_span_t text)
{
    static const char hex[] = "0123456789abcdef";
    lxa_status_t status = literal("\"");
    for (size_t i = 0; status == LXA_OK && i < text.length; i++) {
        unsigned char c = text.data[i];
        if (c < 0x20) {
            char escaped[] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
            status = append(escaped, sizeof(escaped));
        }
        else {
            if (c == '"' || c == '\\') status = literal("\\");
            if (status == LXA_OK) status = append(&c, 1);
        }
    }
    return status == LXA_OK ? literal("\"") : status;
}

static lxa_status_t
token(void *context, lxa_html_tokenizer_t *tokenizer,
      const lxa_html_token_t *value)
{
    lxa_status_t status;
    (void) context;
    (void) tokenizer;
    if (value->kind == LXA_HTML_EOF) return LXA_OK;
    switch (value->kind) {
    case LXA_HTML_START_TAG: status = literal("[\"StartTag\","); break;
    case LXA_HTML_END_TAG: status = literal("[\"EndTag\","); break;
    case LXA_HTML_TEXT: status = literal("[\"Character\","); break;
    case LXA_HTML_COMMENT: status = literal("[\"Comment\","); break;
    case LXA_HTML_DOCTYPE: status = literal("[\"DOCTYPE\","); break;
    default: return LXA_ERROR_UNSUPPORTED;
    }
    if (status != LXA_OK) return status;
    if (value->kind == LXA_HTML_TEXT || value->kind == LXA_HTML_COMMENT)
        status = quoted(value->data);
    else if (value->kind == LXA_HTML_DOCTYPE && value->name.length == 0)
        status = literal("null");
    else
        status = quoted(value->name);
    if (status != LXA_OK) return status;
    if (value->kind == LXA_HTML_START_TAG) {
        status = literal(",[");
        for (size_t i = 0; status == LXA_OK && i < value->attribute_count; i++) {
            if (i != 0) status = literal(",");
            if (status == LXA_OK) status = literal("[");
            if (status == LXA_OK) status = quoted(value->attributes[i].name);
            if (status == LXA_OK) status = literal(",");
            if (status == LXA_OK) status = quoted(value->attributes[i].value);
            if (status == LXA_OK) status = literal("]");
        }
        if (status == LXA_OK) status = literal("]");
        if (status == LXA_OK && value->self_closing) status = literal(",true");
    }
    else if (value->kind == LXA_HTML_DOCTYPE) {
        status = literal(",");
        if (status == LXA_OK)
            status = value->has_public_id ? quoted(value->public_id) : literal("null");
        if (status == LXA_OK) status = literal(",");
        if (status == LXA_OK)
            status = value->has_system_id ? quoted(value->system_id) : literal("null");
        if (status == LXA_OK) status = literal(value->force_quirks ? ",false" : ",true");
    }
    return status == LXA_OK ? literal("]\n") : status;
}

static lxa_status_t
error(void *context, const char *code, size_t offset)
{
    lxa_status_t status = literal("[\"Error\",");
    (void) context;
    (void) offset;
    if (status == LXA_OK)
        status = quoted((lxa_span_t) {(const uint8_t *) code, strlen(code)});
    return status == LXA_OK ? literal("]\n") : status;
}

int
main(int argc, char **argv)
{
    static const char *states[] = {
        "Data state", "RCDATA state", "RAWTEXT state",
        "Script data state", "PLAINTEXT state", "CDATA section state"
    };
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_html_tokenizer_t *tokenizer = NULL;
    lxa_html_result_t result;
    size_t length, state, chunk;
    int extra;
    if (argc != 4 || (strcmp(argv[3], "whole") != 0 && strcmp(argv[3], "byte") != 0))
        return 2;
    for (state = 0; state < sizeof(states) / sizeof(states[0]); state++)
        if (strcmp(argv[1], states[state]) == 0) break;
    if (state == sizeof(states) / sizeof(states[0])) return 2;
    length = fread(input, 1, sizeof(input), stdin);
    extra = fgetc(stdin);
    if (ferror(stdin) || extra != EOF) return 2;
    if (lxa_html_tokenizer_create(&allocator, token, error, NULL, &tokenizer)
        != LXA_OK) return 1;
    if (state == 5) {
        result = lxa_html_tokenizer_allow_cdata(tokenizer, true);
        if (result == LXA_HTML_OK)
            result = lxa_html_tokenizer_feed(tokenizer,
                (lxa_span_t) {(const uint8_t *) "<![CDATA[", 9});
    }
    else {
        const char *name = state == 0 || state == 4 ? "" : argv[2];
        result = lxa_html_tokenizer_set_text_mode(tokenizer,
            (lxa_html_text_mode_t) state,
            (lxa_span_t) {(const uint8_t *) name, strlen(name)});
    }
    chunk = strcmp(argv[3], "byte") == 0 ? 1 : sizeof(input);
    for (size_t at = 0; result == LXA_HTML_OK && at < length; at += chunk) {
        size_t count = length - at < chunk ? length - at : chunk;
        result = lxa_html_tokenizer_feed(tokenizer, (lxa_span_t) {input + at, count});
    }
    if (result == LXA_HTML_OK) result = lxa_html_tokenizer_finish(tokenizer);
    lxa_html_tokenizer_destroy(tokenizer);
    if (result != LXA_HTML_OK) {
        fprintf(stderr, "tokenizer result %d\n", (int) result);
        return 1;
    }
    return fwrite(output, 1, used, stdout) != used || fflush(stdout) != 0 ? 1 : 0;
}
