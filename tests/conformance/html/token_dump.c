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
    char number[32];
    int length;
    (void) context;
    if (status == LXA_OK)
        status = quoted((lxa_span_t) {(const uint8_t *) code, strlen(code)});
    length = snprintf(number, sizeof(number), ",%zu", offset);
    if (length < 0 || (size_t) length >= sizeof(number))
        return LXA_ERROR_OVERFLOW;
    if (status == LXA_OK) status = append(number, (size_t) length);
    return status == LXA_OK ? literal("]\n") : status;
}

typedef enum {
    CHUNK_WHOLE, CHUNK_BYTE, CHUNK_SEVEN, CHUNK_STRUCTURAL, CHUNK_RANDOM,
    CHUNK_COUNT
} chunk_mode_t;

static bool
structural(uint8_t c)
{
    return c == '<' || c == '>' || c == '&' || c == '=' || c == '/'
        || c == '?' || c == '!' || c == '"' || c == '\'' || c <= ' '
        || c >= 0x7f;
}

static lxa_html_result_t
run(size_t state, const char *tag, size_t length, chunk_mode_t mode)
{
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_html_tokenizer_t *tokenizer = NULL;
    lxa_html_result_t result;
    lxa_status_t status;
    uint32_t random = UINT32_C(0x1782ec49);
    status = lxa_html_tokenizer_create(&allocator, token, error, NULL, &tokenizer);
    if (status != LXA_OK) {
        fprintf(stderr, "tokenizer create status %d\n", (int) status);
        return LXA_HTML_INTERNAL;
    }
    if (state == 5) {
        result = lxa_html_tokenizer_allow_cdata(tokenizer, true);
        if (result == LXA_HTML_OK)
            result = lxa_html_tokenizer_feed(tokenizer,
                (lxa_span_t) {(const uint8_t *) "<![CDATA[", 9});
    }
    else {
        const char *name = state == 0 || state == 4 ? "" : tag;
        result = lxa_html_tokenizer_set_text_mode(tokenizer,
            (lxa_html_text_mode_t) state,
            (lxa_span_t) {(const uint8_t *) name, strlen(name)});
    }
    for (size_t at = 0; result == LXA_HTML_OK && at < length;) {
        size_t count = length - at;
        if (mode == CHUNK_BYTE) count = 1;
        else if (mode == CHUNK_SEVEN && count > 7) count = 7;
        else if (mode == CHUNK_RANDOM) {
            size_t chunk;
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            chunk = 1 + random % 23;
            if (count > chunk) count = chunk;
        }
        else if (mode == CHUNK_STRUCTURAL) {
            count = 1;
            if (!structural(input[at]))
                while (count < length - at && !structural(input[at + count]))
                    count++;
        }
        result = lxa_html_tokenizer_feed(tokenizer, (lxa_span_t) {input + at, count});
        at += count;
    }
    if (result == LXA_HTML_OK) result = lxa_html_tokenizer_finish(tokenizer);
    lxa_html_tokenizer_destroy(tokenizer);
    return result;
}

static uint32_t
little_endian(const uint8_t bytes[4])
{
    return (uint32_t) bytes[0] | (uint32_t) bytes[1] << 8
        | (uint32_t) bytes[2] << 16 | (uint32_t) bytes[3] << 24;
}

static int
batch(chunk_mode_t mode)
{
    uint8_t header[12];
    char tag[128];
    size_t count;
    /* Each record is three LE u32s (state, input size, tag size), then bytes. */
    while ((count = fread(header, 1, sizeof(header), stdin)) != 0) {
        size_t state, length, tag_length;
        lxa_html_result_t result;
        if (count != sizeof(header)) return 2;
        state = little_endian(header);
        length = little_endian(header + 4);
        tag_length = little_endian(header + 8);
        if (state > 5 || length > sizeof(input) || tag_length >= sizeof(tag))
            return 2;
        if (fread(input, 1, length, stdin) != length
            || fread(tag, 1, tag_length, stdin) != tag_length
            || memchr(tag, 0, tag_length) != NULL) return 2;
        tag[tag_length] = 0;
        used = 0;
        result = run(state, tag, length, mode);
        if (fwrite(output, 1, used, stdout) != used
            || fprintf(stdout, "[\"Result\",%d]\n", (int) result) < 0) return 1;
    }
    return ferror(stdin) || fflush(stdout) != 0 ? 1 : 0;
}

int
main(int argc, char **argv)
{
    static const char *states[] = {
        "Data state", "RCDATA state", "RAWTEXT state",
        "Script data state", "PLAINTEXT state", "CDATA section state"
    };
    static const char *chunks[] = {"whole", "byte", "seven", "structural", "random"};
    size_t length, state, mode;
    lxa_html_result_t result;
    if (argc != 3 && argc != 4) return 2;
    for (mode = 0; mode < CHUNK_COUNT; mode++)
        if (strcmp(argv[argc - 1], chunks[mode]) == 0) break;
    if (mode == CHUNK_COUNT) return 2;
    if (argc == 3)
        return strcmp(argv[1], "--batch") == 0 ? batch((chunk_mode_t) mode) : 2;
    for (state = 0; state < sizeof(states) / sizeof(states[0]); state++)
        if (strcmp(argv[1], states[state]) == 0) break;
    if (state == sizeof(states) / sizeof(states[0])) return 2;
    length = fread(input, 1, sizeof(input), stdin);
    if (fgetc(stdin) != EOF || ferror(stdin)) return 2;
    result = run(state, argv[2], length, (chunk_mode_t) mode);
    if (result != LXA_HTML_OK) {
        fprintf(stderr, "tokenizer result %d\n", (int) result);
        return 1;
    }
    return fwrite(output, 1, used, stdout) != used || fflush(stdout) != 0 ? 1 : 0;
}
