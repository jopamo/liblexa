#include "encoding/stream_internal.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "encoding-indexes.h"

struct lxa_encoding {
    const uint32_t *index; /* NULL for algorithmic encodings */
    const char *name;
};

/* Immutable identities. Single-byte indexes map bytes 0x80..0xff. */
static const lxa_encoding_t encodings[] = {
    {NULL, "utf-8"},
    {liblexa_index_ibm866, "ibm866"},
    {liblexa_index_iso_8859_2, "iso-8859-2"},
    {liblexa_index_iso_8859_3, "iso-8859-3"},
    {liblexa_index_iso_8859_4, "iso-8859-4"},
    {liblexa_index_iso_8859_5, "iso-8859-5"},
    {liblexa_index_iso_8859_6, "iso-8859-6"},
    {liblexa_index_iso_8859_7, "iso-8859-7"},
    {liblexa_index_iso_8859_8, "iso-8859-8"},
    {liblexa_index_iso_8859_10, "iso-8859-10"},
    {liblexa_index_iso_8859_13, "iso-8859-13"},
    {liblexa_index_iso_8859_14, "iso-8859-14"},
    {liblexa_index_iso_8859_15, "iso-8859-15"},
    {liblexa_index_iso_8859_16, "iso-8859-16"},
    {liblexa_index_koi8_r, "koi8-r"},
    {liblexa_index_koi8_u, "koi8-u"},
    {liblexa_index_macintosh, "macintosh"},
    {liblexa_index_windows_874, "windows-874"},
    {liblexa_index_windows_1250, "windows-1250"},
    {liblexa_index_windows_1251, "windows-1251"},
    {liblexa_index_windows_1252, "windows-1252"},
    {liblexa_index_windows_1253, "windows-1253"},
    {liblexa_index_windows_1254, "windows-1254"},
    {liblexa_index_windows_1255, "windows-1255"},
    {liblexa_index_windows_1256, "windows-1256"},
    {liblexa_index_windows_1257, "windows-1257"},
    {liblexa_index_windows_1258, "windows-1258"},
    {liblexa_index_x_mac_cyrillic, "x-mac-cyrillic"},
    {NULL, "x-user-defined"}, /* algorithmic; no index */
    {NULL, "replacement"}    /* decode only */
};
#define ENCODING_COUNT (sizeof(encodings) / sizeof(encodings[0]))
static const lxa_encoding_t *const utf8_encoding = &encodings[0];
static const lxa_encoding_t *const replacement_encoding =
    &encodings[ENCODING_COUNT - 1];
static const lxa_encoding_t utf16le_encoding = {NULL, "utf-16le"};
static const lxa_encoding_t utf16be_encoding = {NULL, "utf-16be"};
static const lxa_encoding_t euc_kr_encoding = {
    liblexa_index_euc_kr, "euc-kr"
};
static const lxa_encoding_t euc_jp_encoding = {
    liblexa_index_jis0208, "euc-jp"
};

struct lxa_decoder {
    lxa_allocator_t allocator;
    lxa_budget_t *budget;
    lxa_encoding_error_mode_t mode;
    size_t errors;
    unsigned remaining;
    uint8_t lower, upper;
    uint32_t scalar;
    const lxa_encoding_t *encoding;
    bool closed, failed, replacement_done;
    bool have_byte, have_lead, have_replay;
    uint8_t byte; /* UTF-8 lead or unmatched UTF-16 byte. */
    uint8_t kr_lead;
    uint8_t jp_lead;
    bool jp_jis0212;
    uint16_t lead, replay;
};

struct lxa_encoder {
    lxa_allocator_t allocator;
    lxa_budget_t *budget;
    lxa_encoding_error_mode_t mode;
    const lxa_encoding_t *encoding;
    bool closed, failed;
};

static bool
ascii_equal(const uint8_t *s, size_t length, const char *name)
{
    size_t i;

    if (strlen(name) != length) {
        return false;
    }
    for (i = 0; i < length; i++) {
        uint8_t c = s[i];
        if (c >= 'A' && c <= 'Z') {
            c += 'a' - 'A';
        }
        if (c != (uint8_t) name[i]) {
            return false;
        }
    }
    return true;
}

static bool
ascii_space(uint8_t c)
{
    return c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ';
}

static const lxa_encoding_t *
canonical_encoding(const char *name)
{
    size_t i;
    for (i = 0; i < ENCODING_COUNT; i++) {
        if (strcmp(encodings[i].name, name) == 0) {
            return &encodings[i];
        }
    }
    return NULL;
}

const lxa_encoding_t *
lxa_encoding_lookup(lxa_span_t label)
{
    size_t start = 0, end = label.length;
    size_t i;

    if (label.data == NULL) {
        return NULL;
    }
    while (start < end && ascii_space(label.data[start])) {
        start++;
    }
    while (end > start && ascii_space(label.data[end - 1])) {
        end--;
    }
    if (ascii_equal(label.data + start, end - start, "utf-8")
        || ascii_equal(label.data + start, end - start, "utf8")
        || ascii_equal(label.data + start, end - start, "unicode-1-1-utf-8"))
    {
        return utf8_encoding;
    }
    if (ascii_equal(label.data + start, end - start, "utf-16le")
        || ascii_equal(label.data + start, end - start, "utf-16"))
    {
        return &utf16le_encoding;
    }
    if (ascii_equal(label.data + start, end - start, "utf-16be")) {
        return &utf16be_encoding;
    }
    if (ascii_equal(label.data + start, end - start, "euc-kr")
        || ascii_equal(label.data + start, end - start, "ks_c_5601-1987")
        || ascii_equal(label.data + start, end - start, "korean"))
    {
        return &euc_kr_encoding;
    }
    if (ascii_equal(label.data + start, end - start, "euc-jp")
        || ascii_equal(label.data + start, end - start, "cseucpkdfmtjapanese")
        || ascii_equal(label.data + start, end - start, "x-euc-jp"))
    {
        return &euc_jp_encoding;
    }
    if (ascii_equal(label.data + start, end - start, "windows-1252")
        || ascii_equal(label.data + start, end - start, "iso-8859-1")
        || ascii_equal(label.data + start, end - start, "latin1")
        || ascii_equal(label.data + start, end - start, "us-ascii")
        || ascii_equal(label.data + start, end - start, "ascii")
        || ascii_equal(label.data + start, end - start, "cp1252")
        || ascii_equal(label.data + start, end - start, "x-cp1252"))
    {
        return canonical_encoding("windows-1252");
    }
    if (ascii_equal(label.data + start, end - start, "ibm866")
        || ascii_equal(label.data + start, end - start, "866")
        || ascii_equal(label.data + start, end - start, "cp866")
        || ascii_equal(label.data + start, end - start, "csibm866"))
    {
        return canonical_encoding("ibm866");
    }
    for (i = 0; i < ENCODING_COUNT; i++) {
        if (ascii_equal(label.data + start, end - start, encodings[i].name)) {
            return &encodings[i];
        }
    }
    return NULL;
}

static bool
valid_encoding(const lxa_encoding_t *encoding)
{
    size_t i;
    if (encoding == &utf16le_encoding || encoding == &utf16be_encoding
        || encoding == &euc_kr_encoding || encoding == &euc_jp_encoding) {
        return true;
    }
    for (i = 0; i < ENCODING_COUNT; i++) {
        if (encoding == &encodings[i]) {
            return true;
        }
    }
    return false;
}

static bool
valid_mode(lxa_encoding_error_mode_t mode)
{
    return mode == LXA_ENCODING_FATAL || mode == LXA_ENCODING_REPLACE;
}

lxa_status_t
lxa_decoder_create(const lxa_encoding_t *encoding, lxa_encoding_error_mode_t mode,
                   const lxa_allocator_t *allocator, lxa_decoder_t **out)
{
    return lxa_decoder_create_with_budget(encoding, mode, allocator, NULL, out);
}

lxa_status_t
lxa_decoder_create_with_budget(const lxa_encoding_t *encoding,
                               lxa_encoding_error_mode_t mode,
                               const lxa_allocator_t *allocator,
                               lxa_budget_t *budget, lxa_decoder_t **out)
{
    lxa_decoder_t *decoder;
    lxa_status_t status;

    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = NULL;
    if (!valid_encoding(encoding) || !valid_mode(mode) || allocator == NULL
        || allocator->allocate == NULL || allocator->resize == NULL
        || allocator->release == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    if (budget != NULL) {
        status = lxa_budget_charge(budget, LXA_BUDGET_MEMORY,
                                   sizeof(*decoder), NULL);
        if (status != LXA_OK) {
            return status;
        }
    }
    status = lxa_alloc(allocator, sizeof(*decoder), (void **) &decoder);
    if (status != LXA_OK) {
        if (budget != NULL) {
            lxa_status_t released = lxa_budget_release(
                budget, LXA_BUDGET_MEMORY, sizeof(*decoder));
            assert(released == LXA_OK);
            (void) released;
        }
        return status;
    }
    memset(decoder, 0, sizeof(*decoder));
    decoder->allocator = *allocator;
    decoder->budget = budget;
    decoder->mode = mode;
    decoder->encoding = encoding;
    *out = decoder;
    return LXA_OK;
}

lxa_status_t
lxa_encoder_create(const lxa_encoding_t *encoding, lxa_encoding_error_mode_t mode,
                   const lxa_allocator_t *allocator, lxa_encoder_t **out)
{
    return lxa_encoder_create_with_budget(encoding, mode, allocator, NULL, out);
}

lxa_status_t
lxa_encoder_create_with_budget(const lxa_encoding_t *encoding,
                               lxa_encoding_error_mode_t mode,
                               const lxa_allocator_t *allocator,
                               lxa_budget_t *budget, lxa_encoder_t **out)
{
    lxa_encoder_t *encoder;
    lxa_status_t status;

    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    *out = NULL;
    if (!valid_encoding(encoding) || encoding == replacement_encoding
        || !valid_mode(mode) || allocator == NULL
        || allocator->allocate == NULL || allocator->resize == NULL
        || allocator->release == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    if (budget != NULL) {
        status = lxa_budget_charge(budget, LXA_BUDGET_MEMORY,
                                   sizeof(*encoder), NULL);
        if (status != LXA_OK) {
            return status;
        }
    }
    status = lxa_alloc(allocator, sizeof(*encoder), (void **) &encoder);
    if (status != LXA_OK) {
        if (budget != NULL) {
            lxa_status_t released = lxa_budget_release(
                budget, LXA_BUDGET_MEMORY, sizeof(*encoder));
            assert(released == LXA_OK);
            (void) released;
        }
        return status;
    }
    memset(encoder, 0, sizeof(*encoder));
    encoder->allocator = *allocator;
    encoder->budget = budget;
    encoder->mode = mode;
    encoder->encoding = encoding;
    *out = encoder;
    return LXA_OK;
}

void
lxa_decoder_reset(lxa_decoder_t *decoder)
{
    if (decoder != NULL) {
        decoder->remaining = 0;
        decoder->scalar = 0;
        decoder->lower = decoder->upper = 0;
        decoder->closed = decoder->failed = false;
        decoder->replacement_done = false;
        decoder->errors = 0;
        decoder->have_byte = decoder->have_lead = decoder->have_replay = false;
        decoder->kr_lead = 0;
        decoder->jp_lead = 0;
        decoder->jp_jis0212 = false;
    }
}

void
lxa_encoder_reset(lxa_encoder_t *encoder)
{
    if (encoder != NULL) {
        encoder->closed = encoder->failed = false;
    }
}

void
lxa_decoder_destroy(lxa_decoder_t *decoder)
{
    if (decoder != NULL) {
        lxa_allocator_t allocator = decoder->allocator;
        lxa_budget_t *budget = decoder->budget;
        lxa_free(&allocator, decoder);
        if (budget != NULL) {
            lxa_status_t released = lxa_budget_release(
                budget, LXA_BUDGET_MEMORY, sizeof(*decoder));
            assert(released == LXA_OK);
            (void) released;
        }
    }
}

size_t
lxa_decoder_error_count(const lxa_decoder_t *decoder)
{
    return decoder == NULL ? 0 : decoder->errors;
}

size_t
lxa_decoder_pending_bytes(const lxa_decoder_t *decoder)
{
    if (decoder == NULL) return 0;
    if (decoder->encoding == utf8_encoding && decoder->remaining != 0) {
        size_t width = decoder->byte < 0xe0 ? 2
                     : decoder->byte < 0xf0 ? 3 : 4;
        return width - decoder->remaining;
    }
    if (decoder->encoding == &utf16le_encoding
        || decoder->encoding == &utf16be_encoding)
        return (decoder->have_byte ? 1u : 0u)
             + (decoder->have_lead ? 2u : 0u)
             + (decoder->have_replay ? 2u : 0u);
    if (decoder->encoding == &euc_kr_encoding)
        return decoder->kr_lead != 0 ? 1 : 0;
    if (decoder->encoding == &euc_jp_encoding)
        return decoder->jp_lead == 0 ? 0 : decoder->jp_jis0212 ? 2 : 1;
    return 0;
}

static void
record_error(lxa_decoder_t *decoder)
{
    if (decoder->errors != SIZE_MAX) {
        decoder->errors++;
    }
}

void
lxa_encoder_destroy(lxa_encoder_t *encoder)
{
    if (encoder != NULL) {
        lxa_allocator_t allocator = encoder->allocator;
        lxa_budget_t *budget = encoder->budget;
        lxa_free(&allocator, encoder);
        if (budget != NULL) {
            lxa_status_t released = lxa_budget_release(
                budget, LXA_BUDGET_MEMORY, sizeof(*encoder));
            assert(released == LXA_OK);
            (void) released;
        }
    }
}

static lxa_encoding_result_t
bad_input(lxa_decoder_t *decoder, uint32_t *output,
          lxa_encoding_progress_t *progress, size_t capacity)
{
    if (decoder->mode == LXA_ENCODING_FATAL) {
        record_error(decoder);
        decoder->failed = true;
        return LXA_ENCODING_MALFORMED;
    }
    if (progress->produced == capacity) {
        return LXA_ENCODING_OUTPUT_FULL;
    }
    output[progress->produced++] = 0xfffd;
    record_error(decoder);
    decoder->remaining = 0;
    return LXA_ENCODING_OK;
}

static lxa_encoding_result_t
utf16_unit(lxa_decoder_t *decoder, uint16_t unit, uint32_t *output,
           size_t capacity, lxa_encoding_progress_t *progress)
{
    lxa_encoding_result_t result;
    if (decoder->have_lead) {
        if (unit >= 0xdc00 && unit <= 0xdfff) {
            if (progress->produced == capacity) {
                return LXA_ENCODING_OUTPUT_FULL;
            }
            output[progress->produced++] =
                0x10000u + ((uint32_t) (decoder->lead - 0xd800) << 10)
                + (unit - 0xdc00);
            decoder->have_lead = false;
            decoder->have_replay = false;
            return LXA_ENCODING_OK;
        }
        result = bad_input(decoder, output, progress, capacity);
        if (result != LXA_ENCODING_OK) {
            return result;
        }
        decoder->have_lead = false;
        /* Process the already accepted unit again after the error. */
        return LXA_ENCODING_OK;
    }
    if (unit >= 0xd800 && unit <= 0xdbff) {
        decoder->lead = unit;
        decoder->have_lead = true;
        decoder->have_replay = false;
        return LXA_ENCODING_OK;
    }
    if (unit >= 0xdc00 && unit <= 0xdfff) {
        result = bad_input(decoder, output, progress, capacity);
        if (result != LXA_ENCODING_OK) {
            return result;
        }
    }
    else {
        if (progress->produced == capacity) {
            return LXA_ENCODING_OUTPUT_FULL;
        }
        output[progress->produced++] = unit;
    }
    decoder->have_replay = false;
    return LXA_ENCODING_OK;
}

static lxa_encoding_result_t
utf16_feed(lxa_decoder_t *decoder, lxa_span_t input, uint32_t *output,
           size_t capacity, lxa_encoding_progress_t *progress)
{
    while (decoder->have_replay || progress->consumed < input.length) {
        lxa_encoding_result_t result;
        if (!decoder->have_replay) {
            uint8_t second;
            if (!decoder->have_byte) {
                decoder->byte = input.data[progress->consumed++];
                decoder->have_byte = true;
                if (progress->consumed == input.length) {
                    break;
                }
            }
            second = input.data[progress->consumed++];
            decoder->replay = decoder->encoding == &utf16be_encoding
                ? ((uint16_t) decoder->byte << 8) | second
                : ((uint16_t) second << 8) | decoder->byte;
            decoder->have_byte = false;
            decoder->have_replay = true;
        }
        result = utf16_unit(decoder, decoder->replay, output, capacity, progress);
        if (result != LXA_ENCODING_OK) {
            return result;
        }
    }
    return LXA_ENCODING_OK;
}

static lxa_encoding_result_t
euc_kr_feed(lxa_decoder_t *decoder, lxa_span_t input, uint32_t *output,
            size_t capacity, lxa_encoding_progress_t *progress)
{
    while (progress->consumed < input.length) {
        uint8_t byte = input.data[progress->consumed];
        if (decoder->kr_lead != 0) {
            uint32_t cp = UINT32_MAX;
            if (byte >= 0x41 && byte <= 0xfe) {
                size_t pointer = ((size_t) decoder->kr_lead - 0x81) * 190
                                 + (byte - 0x41);
                cp = euc_kr_encoding.index[pointer];
            }
            if (cp != UINT32_MAX) {
                if (progress->produced == capacity) {
                    return LXA_ENCODING_OUTPUT_FULL;
                }
                decoder->kr_lead = 0;
                output[progress->produced++] = cp;
                progress->consumed++;
                continue;
            }
            if (decoder->mode == LXA_ENCODING_FATAL) {
                if (byte >= 0x80) {
                    progress->consumed++;
                }
                record_error(decoder);
                decoder->failed = true;
                return LXA_ENCODING_MALFORMED;
            }
            if (progress->produced == capacity) {
                return LXA_ENCODING_OUTPUT_FULL;
            }
            decoder->kr_lead = 0;
            output[progress->produced++] = 0xfffd;
            record_error(decoder);
            if (byte >= 0x80) {
                progress->consumed++;
            }
            continue;
        }
        if (byte < 0x80) {
            if (progress->produced == capacity) {
                return LXA_ENCODING_OUTPUT_FULL;
            }
            output[progress->produced++] = byte;
            progress->consumed++;
        }
        else if (byte >= 0x81 && byte <= 0xfe) {
            decoder->kr_lead = byte;
            progress->consumed++;
        }
        else {
            if (decoder->mode == LXA_ENCODING_FATAL) {
                progress->consumed++;
                record_error(decoder);
                decoder->failed = true;
                return LXA_ENCODING_MALFORMED;
            }
            if (progress->produced == capacity) {
                return LXA_ENCODING_OUTPUT_FULL;
            }
            output[progress->produced++] = 0xfffd;
            record_error(decoder);
            progress->consumed++;
        }
    }
    return LXA_ENCODING_OK;
}

static lxa_encoding_result_t
euc_jp_feed(lxa_decoder_t *decoder, lxa_span_t input, uint32_t *output,
            size_t capacity, lxa_encoding_progress_t *progress)
{
    while (progress->consumed < input.length) {
        uint8_t byte = input.data[progress->consumed];
        uint8_t lead = decoder->jp_lead;
        if (lead == 0x8f && byte >= 0xa1 && byte <= 0xfe) {
            decoder->jp_jis0212 = true;
            decoder->jp_lead = byte;
            progress->consumed++;
            continue;
        }
        if (lead != 0) {
            uint32_t cp = UINT32_MAX;
            if (lead == 0x8e && byte >= 0xa1 && byte <= 0xdf) {
                cp = byte - 0xa1 + 0xff61;
            }
            else if (lead >= 0xa1 && lead <= 0xfe
                     && byte >= 0xa1 && byte <= 0xfe)
            {
                size_t pointer = ((size_t) lead - 0xa1) * 94 + byte - 0xa1;
                cp = decoder->jp_jis0212
                    ? liblexa_index_jis0212[pointer]
                    : euc_jp_encoding.index[pointer];
            }
            if (cp != UINT32_MAX) {
                if (progress->produced == capacity) {
                    return LXA_ENCODING_OUTPUT_FULL;
                }
                decoder->jp_lead = 0;
                decoder->jp_jis0212 = false;
                output[progress->produced++] = cp;
                progress->consumed++;
                continue;
            }
            if (decoder->mode == LXA_ENCODING_FATAL) {
                if (byte >= 0x80) {
                    progress->consumed++;
                }
                record_error(decoder);
                decoder->failed = true;
                return LXA_ENCODING_MALFORMED;
            }
            if (progress->produced == capacity) {
                return LXA_ENCODING_OUTPUT_FULL;
            }
            decoder->jp_lead = 0;
            decoder->jp_jis0212 = false;
            output[progress->produced++] = 0xfffd;
            record_error(decoder);
            if (byte >= 0x80) {
                progress->consumed++;
            }
            continue;
        }
        if (byte < 0x80) {
            if (progress->produced == capacity) {
                return LXA_ENCODING_OUTPUT_FULL;
            }
            output[progress->produced++] = byte;
            progress->consumed++;
        }
        else if (byte == 0x8e || byte == 0x8f
                 || (byte >= 0xa1 && byte <= 0xfe))
        {
            decoder->jp_lead = byte;
            progress->consumed++;
        }
        else {
            if (decoder->mode == LXA_ENCODING_FATAL) {
                progress->consumed++;
                record_error(decoder);
                decoder->failed = true;
                return LXA_ENCODING_MALFORMED;
            }
            if (progress->produced == capacity) {
                return LXA_ENCODING_OUTPUT_FULL;
            }
            output[progress->produced++] = 0xfffd;
            record_error(decoder);
            progress->consumed++;
        }
    }
    return LXA_ENCODING_OK;
}

lxa_encoding_result_t
lxa_decoder_feed(lxa_decoder_t *decoder, lxa_span_t input, uint32_t *output,
                 size_t capacity, lxa_encoding_progress_t *progress)
{
    if (progress == NULL) {
        return LXA_ENCODING_INVALID_ARGUMENT;
    }
    progress->consumed = progress->produced = 0;
    if (decoder == NULL || decoder->failed || decoder->closed
        || (input.data == NULL && input.length != 0)
        || (output == NULL && capacity != 0))
    {
        return LXA_ENCODING_INVALID_ARGUMENT;
    }

    if (decoder->encoding == &utf16le_encoding
        || decoder->encoding == &utf16be_encoding)
    {
        return utf16_feed(decoder, input, output, capacity, progress);
    }
    if (decoder->encoding == &euc_kr_encoding) {
        return euc_kr_feed(decoder, input, output, capacity, progress);
    }
    if (decoder->encoding == &euc_jp_encoding) {
        return euc_jp_feed(decoder, input, output, capacity, progress);
    }
    if (decoder->encoding == replacement_encoding && input.length != 0) {
        if (!decoder->replacement_done) {
            if (decoder->mode == LXA_ENCODING_FATAL) {
                progress->consumed = 1;
                record_error(decoder);
                decoder->failed = true;
                return LXA_ENCODING_MALFORMED;
            }
            if (capacity == 0) {
                return LXA_ENCODING_OUTPUT_FULL;
            }
            output[0] = 0xfffd;
            record_error(decoder);
            progress->produced = 1;
            decoder->replacement_done = true;
        }
        progress->consumed = input.length;
        return LXA_ENCODING_OK;
    }

    while (progress->consumed < input.length) {
        uint8_t c = input.data[progress->consumed];
        if (decoder->encoding != utf8_encoding) {
            uint32_t cp = c < 0x80 ? c :
                decoder->encoding->index != NULL
                ? decoder->encoding->index[c - 0x80] : 0xf700u + c;
            if (cp != UINT32_MAX) {
                if (progress->produced == capacity) {
                    return LXA_ENCODING_OUTPUT_FULL;
                }
                output[progress->produced++] = cp;
                progress->consumed++;
                continue;
            }
            if (decoder->mode == LXA_ENCODING_FATAL) {
                progress->consumed++;
                record_error(decoder);
                decoder->failed = true;
                return LXA_ENCODING_MALFORMED;
            }
            if (progress->produced == capacity) {
                return LXA_ENCODING_OUTPUT_FULL;
            }
            output[progress->produced++] = 0xfffd;
            record_error(decoder);
            progress->consumed++;
            continue;
        }
        if (decoder->remaining == 0) {
            if (c < 0x80) {
                if (progress->produced == capacity) {
                    return LXA_ENCODING_OUTPUT_FULL;
                }
                output[progress->produced++] = c;
            }
            else if (c >= 0xc2 && c <= 0xdf) {
                decoder->byte = c;
                decoder->scalar = c & 0x1f;
                decoder->remaining = 1;
                decoder->lower = 0x80;
                decoder->upper = 0xbf;
            }
            else if (c >= 0xe0 && c <= 0xef) {
                decoder->byte = c;
                decoder->scalar = c & 0x0f;
                decoder->remaining = 2;
                decoder->lower = c == 0xe0 ? 0xa0 : 0x80;
                decoder->upper = c == 0xed ? 0x9f : 0xbf;
            }
            else if (c >= 0xf0 && c <= 0xf4) {
                decoder->byte = c;
                decoder->scalar = c & 0x07;
                decoder->remaining = 3;
                decoder->lower = c == 0xf0 ? 0x90 : 0x80;
                decoder->upper = c == 0xf4 ? 0x8f : 0xbf;
            }
            else {
                /* This byte is the entire malformed sequence. */
                if (decoder->mode == LXA_ENCODING_FATAL) {
                    progress->consumed++;
                    record_error(decoder);
                    decoder->failed = true;
                    return LXA_ENCODING_MALFORMED;
                }
                if (progress->produced == capacity) {
                    return LXA_ENCODING_OUTPUT_FULL;
                }
                output[progress->produced++] = 0xfffd;
                record_error(decoder);
            }
            progress->consumed++;
            continue;
        }
        if (c < decoder->lower || c > decoder->upper) {
            /* The current byte starts a new sequence; leave it for the
             * next iteration (or next call when output is full). */
            lxa_encoding_result_t result =
                bad_input(decoder, output, progress, capacity);
            if (result != LXA_ENCODING_OK) {
                return result;
            }
            continue;
        }
        if (decoder->remaining == 1 && progress->produced == capacity) {
            return LXA_ENCODING_OUTPUT_FULL;
        }
        decoder->scalar = (decoder->scalar << 6) | (c & 0x3f);
        decoder->remaining--;
        decoder->lower = 0x80;
        decoder->upper = 0xbf;
        progress->consumed++;
        if (decoder->remaining == 0) {
            output[progress->produced++] = decoder->scalar;
        }
    }
    return LXA_ENCODING_OK;
}

lxa_encoding_result_t
lxa_decoder_finish(lxa_decoder_t *decoder, uint32_t *output, size_t capacity,
                   lxa_encoding_progress_t *progress)
{
    lxa_encoding_result_t result;
    if (progress == NULL) {
        return LXA_ENCODING_INVALID_ARGUMENT;
    }
    progress->consumed = progress->produced = 0;
    if (decoder == NULL || decoder->failed || decoder->closed
        || (output == NULL && capacity != 0))
    {
        return LXA_ENCODING_INVALID_ARGUMENT;
    }
    if (decoder->encoding == &utf16le_encoding
        || decoder->encoding == &utf16be_encoding)
    {
        lxa_span_t empty = {NULL, 0};
        result = utf16_feed(decoder, empty, output, capacity, progress);
        if (result != LXA_ENCODING_OK) {
            return result;
        }
        if (decoder->have_byte || decoder->have_lead) {
            result = bad_input(decoder, output, progress, capacity);
            if (result != LXA_ENCODING_OK) {
                return result;
            }
            decoder->have_byte = decoder->have_lead = false;
        }
    }
    if (decoder->remaining != 0) {
        result = bad_input(decoder, output, progress, capacity);
        if (result != LXA_ENCODING_OK) {
            return result;
        }
    }
    if (decoder->kr_lead != 0) {
        result = bad_input(decoder, output, progress, capacity);
        if (result != LXA_ENCODING_OK) {
            return result;
        }
        decoder->kr_lead = 0;
    }
    if (decoder->jp_lead != 0) {
        result = bad_input(decoder, output, progress, capacity);
        if (result != LXA_ENCODING_OK) {
            return result;
        }
        decoder->jp_lead = 0;
        decoder->jp_jis0212 = false;
    }
    decoder->closed = true;
    return LXA_ENCODING_OK;
}

static size_t
utf8_length(uint32_t scalar)
{
    if (scalar < 0x80) return 1;
    if (scalar < 0x800) return 2;
    if (scalar < 0x10000) return 3;
    return 4;
}

static void
utf16_write(uint8_t *out, uint16_t unit, bool big_endian)
{
    out[big_endian ? 0 : 1] = (uint8_t) (unit >> 8);
    out[big_endian ? 1 : 0] = (uint8_t) unit;
}

static uint32_t
reverse_pointer(const liblexa_encoding_reverse *index, size_t count,
                uint32_t cp)
{
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (index[mid].codepoint < cp) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    return lo < count && index[lo].codepoint == cp
           ? index[lo].pointer : UINT32_MAX;
}

lxa_encoding_result_t
lxa_encoder_feed(lxa_encoder_t *encoder, const uint32_t *input, size_t length,
                 lxa_mut_span_t output, lxa_encoding_progress_t *progress)
{
    if (progress == NULL) {
        return LXA_ENCODING_INVALID_ARGUMENT;
    }
    progress->consumed = progress->produced = 0;
    if (encoder == NULL || encoder->failed || encoder->closed
        || (input == NULL && length != 0)
        || (output.data == NULL && output.length != 0))
    {
        return LXA_ENCODING_INVALID_ARGUMENT;
    }
    while (progress->consumed < length) {
        uint32_t cp = input[progress->consumed];
        size_t n, at;
        if (encoder->encoding == &euc_jp_encoding) {
            uint32_t pointer = UINT32_MAX;
            uint8_t ascii = 0;
            if (cp < 0x80 || cp == 0xa5 || cp == 0x203e) {
                n = 1;
                ascii = (uint8_t) (cp == 0xa5 ? 0x5c
                                   : cp == 0x203e ? 0x7e : cp);
            }
            else if (cp >= 0xff61 && cp <= 0xff9f) {
                n = 2;
            }
            else {
                if (cp == 0x2212) {
                    cp = 0xff0d;
                }
                pointer = reverse_pointer(liblexa_index_jis0208_reverse,
                                          LIBLEXA_INDEX_JIS0208_REVERSE_COUNT,
                                          cp);
                if (pointer == UINT32_MAX
                    && encoder->mode == LXA_ENCODING_FATAL)
                {
                    progress->consumed++;
                    encoder->failed = true;
                    return LXA_ENCODING_MALFORMED;
                }
                n = pointer == UINT32_MAX ? 1 : 2;
                ascii = '?';
            }
            if (n > output.length - progress->produced) {
                return LXA_ENCODING_OUTPUT_FULL;
            }
            at = progress->produced;
            if (n == 1) {
                output.data[at] = ascii;
            }
            else if (cp >= 0xff61 && cp <= 0xff9f) {
                output.data[at] = 0x8e;
                output.data[at + 1] = (uint8_t) (cp - 0xff61 + 0xa1);
            }
            else {
                output.data[at] = (uint8_t) (pointer / 94 + 0xa1);
                output.data[at + 1] = (uint8_t) (pointer % 94 + 0xa1);
            }
            progress->produced += n;
            progress->consumed++;
            continue;
        }
        if (encoder->encoding == &euc_kr_encoding) {
            uint32_t pointer = UINT32_MAX;
            if (cp < 0x80) {
                n = 1;
            }
            else {
                pointer = reverse_pointer(liblexa_index_euc_kr_reverse,
                                          LIBLEXA_INDEX_EUC_KR_REVERSE_COUNT,
                                          cp);
                if (pointer == UINT32_MAX
                    && encoder->mode == LXA_ENCODING_FATAL)
                {
                    progress->consumed++;
                    encoder->failed = true;
                    return LXA_ENCODING_MALFORMED;
                }
                n = pointer == UINT32_MAX ? 1 : 2;
            }
            if (n > output.length - progress->produced) {
                return LXA_ENCODING_OUTPUT_FULL;
            }
            at = progress->produced;
            if (n == 1) {
                output.data[at] = (uint8_t) (pointer == UINT32_MAX
                                              && cp >= 0x80 ? '?' : cp);
            }
            else {
                output.data[at] = (uint8_t) (pointer / 190 + 0x81);
                output.data[at + 1] = (uint8_t) (pointer % 190 + 0x41);
            }
            progress->produced += n;
            progress->consumed++;
            continue;
        }
        if (encoder->encoding == &utf16le_encoding
            || encoder->encoding == &utf16be_encoding)
        {
            bool big_endian = encoder->encoding == &utf16be_encoding;
            if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
                if (encoder->mode == LXA_ENCODING_FATAL) {
                    progress->consumed++;
                    encoder->failed = true;
                    return LXA_ENCODING_MALFORMED;
                }
                cp = '?';
            }
            n = cp < 0x10000 ? 2 : 4;
            if (n > output.length - progress->produced) {
                return LXA_ENCODING_OUTPUT_FULL;
            }
            at = progress->produced;
            if (n == 2) {
                utf16_write(output.data + at, (uint16_t) cp, big_endian);
            }
            else {
                cp -= 0x10000;
                utf16_write(output.data + at,
                            (uint16_t) (0xd800 | (cp >> 10)), big_endian);
                utf16_write(output.data + at + 2,
                            (uint16_t) (0xdc00 | (cp & 0x3ff)), big_endian);
            }
            progress->produced += n;
            progress->consumed++;
            continue;
        }
        if (encoder->encoding != utf8_encoding) {
            uint32_t byte = UINT32_MAX;
            if (cp < 0x80) {
                byte = cp;
            }
            else if (encoder->encoding->index == NULL) {
                if (cp >= 0xf780 && cp <= 0xf7ff) {
                    byte = cp - 0xf700;
                }
            }
            else {
                size_t i;
                for (i = 0; i < 128; i++) {
                    if (encoder->encoding->index[i] == cp) {
                        byte = (uint32_t) i + 0x80;
                        break;
                    }
                }
            }
            if (byte == UINT32_MAX && encoder->mode == LXA_ENCODING_FATAL) {
                progress->consumed++;
                encoder->failed = true;
                return LXA_ENCODING_MALFORMED;
            }
            if (progress->produced == output.length) {
                return LXA_ENCODING_OUTPUT_FULL;
            }
            output.data[progress->produced++] =
                (uint8_t) (byte == UINT32_MAX ? '?' : byte);
            progress->consumed++;
            continue;
        }
        if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
            if (encoder->mode == LXA_ENCODING_FATAL) {
                progress->consumed++;
                encoder->failed = true;
                return LXA_ENCODING_MALFORMED;
            }
            cp = '?';
        }
        n = utf8_length(cp);
        if (n > output.length - progress->produced) {
            return LXA_ENCODING_OUTPUT_FULL;
        }
        at = progress->produced;
        if (n == 1) {
            output.data[at] = (uint8_t) cp;
        }
        else {
            size_t i;
            for (i = n - 1; i > 0; i--) {
                output.data[at + i] = 0x80 | (cp & 0x3f);
                cp >>= 6;
            }
            output.data[at] = (uint8_t) ((0xf0 << (4 - n)) | cp);
        }
        progress->produced += n;
        progress->consumed++;
    }
    return LXA_ENCODING_OK;
}

lxa_encoding_result_t
lxa_encoder_finish(lxa_encoder_t *encoder, lxa_mut_span_t output,
                   lxa_encoding_progress_t *progress)
{
    if (progress == NULL) {
        return LXA_ENCODING_INVALID_ARGUMENT;
    }
    progress->consumed = progress->produced = 0;
    if (encoder == NULL || encoder->failed || encoder->closed
        || (output.data == NULL && output.length != 0))
    {
        return LXA_ENCODING_INVALID_ARGUMENT;
    }
    encoder->closed = true;
    return LXA_ENCODING_OK;
}
