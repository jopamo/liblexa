/* Copyright (C) 2026 Paul Moses */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "liblexa/encoding.h"

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
        abort(); \
    } \
} while (0)

static lxa_span_t
span(const char *s, size_t n)
{
    lxa_span_t result = {(const uint8_t *) s, n};
    return result;
}

int
main(void)
{
    static const char *const single_byte[] = {
        "ibm866", "iso-8859-2", "iso-8859-3", "iso-8859-4",
        "iso-8859-5", "iso-8859-6", "iso-8859-7", "iso-8859-8",
        "iso-8859-10", "iso-8859-13", "iso-8859-14", "iso-8859-15",
        "iso-8859-16", "koi8-r", "koi8-u", "macintosh", "windows-874",
        "windows-1250", "windows-1251", "windows-1252", "windows-1253",
        "windows-1254", "windows-1255", "windows-1256", "windows-1257",
        "windows-1258", "x-mac-cyrillic"
    };
    size_t i;
    lxa_allocator_t allocator = lxa_allocator_default();
    lxa_span_t label = span(" UTF8\t", 6);
    const lxa_encoding_t *utf8 = lxa_encoding_lookup(label);
    lxa_decoder_t *decoder = NULL;
    lxa_encoder_t *encoder = NULL;
    lxa_encoding_progress_t p;
    uint32_t cps[4] = {0};
    uint8_t bytes[9] = {0};
    lxa_mut_span_t out = {bytes, sizeof(bytes)};

    {
        lxa_limits_t limits = lxa_limits_default();
        lxa_budget_t budget;
        limits.max_memory_bytes = 1;
        CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CHECK(lxa_decoder_create_with_budget(utf8, LXA_ENCODING_REPLACE,
              &allocator, &budget, &decoder) == LXA_ERROR_LIMIT);
        CHECK(decoder == NULL && budget.used[LXA_BUDGET_MEMORY] == 0);
        limits = lxa_limits_default();
        CHECK(lxa_budget_init(&budget, &limits) == LXA_OK);
        CHECK(lxa_decoder_create_with_budget(utf8, LXA_ENCODING_REPLACE,
              &allocator, &budget, &decoder) == LXA_OK);
        CHECK(lxa_encoder_create_with_budget(utf8, LXA_ENCODING_FATAL,
              &allocator, &budget, &encoder) == LXA_OK);
        CHECK(budget.used[LXA_BUDGET_MEMORY] != 0);
        lxa_encoder_destroy(encoder);
        lxa_decoder_destroy(decoder);
        CHECK(budget.used[LXA_BUDGET_MEMORY] == 0);
        decoder = NULL;
        encoder = NULL;
    }

    for (i = 0; i < sizeof(single_byte) / sizeof(single_byte[0]); i++) {
        const lxa_encoding_t *enc =
            lxa_encoding_lookup(span(single_byte[i], strlen(single_byte[i])));
        CHECK(enc != NULL);
        CHECK(lxa_decoder_create(enc, LXA_ENCODING_REPLACE, &allocator,
                                  &decoder) == LXA_OK);
        CHECK(lxa_decoder_feed(decoder, span("A", 1), cps, 1, &p)
               == LXA_ENCODING_OK && cps[0] == 'A');
        lxa_decoder_destroy(decoder);
    }
    CHECK(utf8 != NULL);
    CHECK(lxa_encoding_lookup(span("unknown", 7)) == NULL);
    CHECK(lxa_encoding_lookup(span("latin1", 6))
           == lxa_encoding_lookup(span(" WINDOWS-1252 ", 14)));
    CHECK(lxa_decoder_create(utf8, LXA_ENCODING_REPLACE, &allocator,
                              &decoder) == LXA_OK);
    CHECK(lxa_decoder_error_count(decoder) == 0);
    CHECK(lxa_decoder_feed(decoder, span("\xf0\x9f", 2), cps, 4, &p)
           == LXA_ENCODING_OK);
    CHECK(p.consumed == 2 && p.produced == 0);
    CHECK(lxa_decoder_feed(decoder, span("\x98\x80!", 3), cps, 0, &p)
           == LXA_ENCODING_OUTPUT_FULL);
    CHECK(p.consumed == 1 && p.produced == 0);
    CHECK(lxa_decoder_feed(decoder, span("\x80!", 2), cps, 4, &p)
           == LXA_ENCODING_OK);
    CHECK(p.consumed == 2 && p.produced == 2);
    CHECK(cps[0] == 0x1f600 && cps[1] == '!');
    CHECK(lxa_decoder_finish(decoder, cps, 4, &p) == LXA_ENCODING_OK);
    CHECK(p.produced == 0);
    CHECK(lxa_decoder_error_count(decoder) == 0);
    CHECK(lxa_decoder_feed(decoder, span("a", 1), cps, 4, &p)
           == LXA_ENCODING_INVALID_ARGUMENT);

    lxa_decoder_reset(decoder);
    CHECK(lxa_decoder_feed(decoder, span("\xe0", 1), cps, 4, &p)
           == LXA_ENCODING_OK);
    CHECK(lxa_decoder_feed(decoder, span("A", 1), cps, 0, &p)
           == LXA_ENCODING_OUTPUT_FULL);
    CHECK(p.consumed == 0 && p.produced == 0);
    CHECK(lxa_decoder_feed(decoder, span("A", 1), cps, 4, &p)
           == LXA_ENCODING_OK);
    CHECK(p.consumed == 1 && p.produced == 2);
    CHECK(cps[0] == 0xfffd && cps[1] == 'A');
    CHECK(lxa_decoder_error_count(decoder) == 1);
    lxa_decoder_reset(decoder);
    CHECK(lxa_decoder_error_count(decoder) == 0);
    CHECK(lxa_decoder_feed(decoder, span("\xed\xa0\x80", 3), cps, 4, &p)
           == LXA_ENCODING_OK);
    CHECK(p.produced == 3 && cps[0] == 0xfffd && cps[1] == 0xfffd
           && cps[2] == 0xfffd);
    CHECK(lxa_decoder_error_count(decoder) == 3);
    lxa_decoder_reset(decoder);
    CHECK(lxa_decoder_feed(decoder, span("\xe2\x82", 2), cps, 4, &p)
           == LXA_ENCODING_OK);
    CHECK(lxa_decoder_finish(decoder, cps, 0, &p)
           == LXA_ENCODING_OUTPUT_FULL);
    CHECK(lxa_decoder_finish(decoder, cps, 4, &p) == LXA_ENCODING_OK);
    CHECK(p.produced == 1 && cps[0] == 0xfffd);
    lxa_decoder_destroy(decoder);

    CHECK(lxa_decoder_create(utf8, LXA_ENCODING_FATAL, &allocator,
                              &decoder) == LXA_OK);
    CHECK(lxa_decoder_feed(decoder, span("\xc2", 1), cps, 4, &p)
           == LXA_ENCODING_OK);
    CHECK(lxa_decoder_feed(decoder, span("A", 1), cps, 4, &p)
           == LXA_ENCODING_MALFORMED);
    CHECK(p.consumed == 0 && p.produced == 0);
    CHECK(lxa_decoder_finish(decoder, cps, 4, &p)
           == LXA_ENCODING_INVALID_ARGUMENT);
    lxa_decoder_reset(decoder);
    CHECK(lxa_decoder_feed(decoder, span("\xc2", 1), cps, 4, &p)
           == LXA_ENCODING_OK);
    CHECK(lxa_decoder_finish(decoder, cps, 4, &p)
           == LXA_ENCODING_MALFORMED);
    lxa_decoder_destroy(decoder);

    CHECK(lxa_encoder_create(utf8, LXA_ENCODING_REPLACE, &allocator,
                              &encoder) == LXA_OK);
    cps[0] = 'a'; cps[1] = 0x1f600; cps[2] = 0xd800;
    out.length = 4;
    CHECK(lxa_encoder_feed(encoder, cps, 3, out, &p)
           == LXA_ENCODING_OUTPUT_FULL);
    CHECK(p.consumed == 1 && p.produced == 1 && bytes[0] == 'a');
    out.length = sizeof(bytes);
    CHECK(lxa_encoder_feed(encoder, cps + p.consumed, 2, out, &p)
           == LXA_ENCODING_OK);
    CHECK(p.consumed == 2 && p.produced == 5);
    CHECK(bytes[0] == 0xf0 && bytes[1] == 0x9f
           && bytes[2] == 0x98 && bytes[3] == 0x80 && bytes[4] == '?');
    CHECK(lxa_encoder_finish(encoder, out, &p) == LXA_ENCODING_OK);
    lxa_encoder_destroy(encoder);
    CHECK(lxa_encoder_create(utf8, LXA_ENCODING_FATAL, &allocator,
                              &encoder) == LXA_OK);
    CHECK(lxa_encoder_feed(encoder, cps + 2, 1, out, &p)
           == LXA_ENCODING_MALFORMED);
    CHECK(p.consumed == 1 && p.produced == 0);
    lxa_encoder_reset(encoder);
    CHECK(lxa_encoder_feed(encoder, cps, 1, out, &p) == LXA_ENCODING_OK);
    lxa_encoder_destroy(encoder);

    /* WHATWG's ASCII label resolves to windows-1252, including C1. */
    utf8 = lxa_encoding_lookup(span("us-ascii", 8));
    CHECK(lxa_decoder_create(utf8, LXA_ENCODING_REPLACE, &allocator,
                              &decoder) == LXA_OK);
    CHECK(lxa_decoder_feed(decoder, span("\x80\x81\xff", 3), cps, 2, &p)
           == LXA_ENCODING_OUTPUT_FULL);
    CHECK(p.consumed == 2 && p.produced == 2);
    CHECK(cps[0] == 0x20ac && cps[1] == 0x81);
    CHECK(lxa_decoder_feed(decoder, span("\xff", 1), cps, 4, &p)
           == LXA_ENCODING_OK);
    CHECK(cps[0] == 0xff);
    lxa_decoder_destroy(decoder);
    CHECK(lxa_encoder_create(utf8, LXA_ENCODING_REPLACE, &allocator,
                              &encoder) == LXA_OK);
    cps[0] = 0x20ac; cps[1] = 0x81; cps[2] = 0x1f600;
    out.length = 2;
    CHECK(lxa_encoder_feed(encoder, cps, 3, out, &p)
           == LXA_ENCODING_OUTPUT_FULL);
    CHECK(p.consumed == 2 && bytes[0] == 0x80 && bytes[1] == 0x81);
    out.length = sizeof(bytes);
    CHECK(lxa_encoder_feed(encoder, cps + 2, 1, out, &p)
           == LXA_ENCODING_OK);
    CHECK(p.consumed == 1 && bytes[0] == '?');
    lxa_encoder_destroy(encoder);

    utf8 = lxa_encoding_lookup(span("CP866", 5));
    CHECK(utf8 == lxa_encoding_lookup(span("ibm866", 6)));
    CHECK(lxa_decoder_create(utf8, LXA_ENCODING_FATAL, &allocator,
                              &decoder) == LXA_OK);
    CHECK(lxa_decoder_feed(decoder, span("A\x80\xff", 3), cps, 4, &p)
           == LXA_ENCODING_OK);
    CHECK(p.produced == 3 && cps[0] == 'A' && cps[1] == 0x410
           && cps[2] == 0xa0);
    lxa_decoder_destroy(decoder);
    CHECK(lxa_encoder_create(utf8, LXA_ENCODING_FATAL, &allocator,
                              &encoder) == LXA_OK);
    CHECK(lxa_encoder_feed(encoder, cps, 3, out, &p) == LXA_ENCODING_OK);
    CHECK(p.consumed == 3 && p.produced == 3 && bytes[0] == 'A'
           && bytes[1] == 0x80 && bytes[2] == 0xff);
    lxa_encoder_destroy(encoder);

    utf8 = lxa_encoding_lookup(span("x-user-defined", 14));
    CHECK(utf8 != NULL);
    CHECK(lxa_decoder_create(utf8, LXA_ENCODING_REPLACE, &allocator,
                              &decoder) == LXA_OK);
    CHECK(lxa_decoder_feed(decoder, span("\x7f\x80\xff", 3), cps, 4, &p)
           == LXA_ENCODING_OK);
    CHECK(p.produced == 3 && cps[0] == 0x7f && cps[1] == 0xf780
           && cps[2] == 0xf7ff);
    lxa_decoder_destroy(decoder);
    CHECK(lxa_encoder_create(utf8, LXA_ENCODING_FATAL, &allocator,
                              &encoder) == LXA_OK);
    out.length = sizeof(bytes);
    CHECK(lxa_encoder_feed(encoder, cps, 3, out, &p) == LXA_ENCODING_OK);
    CHECK(p.produced == 3 && bytes[0] == 0x7f && bytes[1] == 0x80
           && bytes[2] == 0xff);
    lxa_encoder_destroy(encoder);

    utf8 = lxa_encoding_lookup(span("replacement", 11));
    CHECK(utf8 != NULL);
    CHECK(lxa_encoder_create(utf8, LXA_ENCODING_REPLACE, &allocator,
                              &encoder) == LXA_ERROR_ARGUMENT);
    CHECK(encoder == NULL);
    CHECK(lxa_decoder_create(utf8, LXA_ENCODING_REPLACE, &allocator,
                              &decoder) == LXA_OK);
    CHECK(lxa_decoder_feed(decoder, span("abc", 3), cps, 0, &p)
           == LXA_ENCODING_OUTPUT_FULL && p.consumed == 0);
    CHECK(lxa_decoder_feed(decoder, span("abc", 3), cps, 4, &p)
           == LXA_ENCODING_OK);
    CHECK(p.consumed == 3 && p.produced == 1 && cps[0] == 0xfffd);
    CHECK(lxa_decoder_feed(decoder, span("xyz", 3), cps, 4, &p)
           == LXA_ENCODING_OK && p.consumed == 3 && p.produced == 0);
    lxa_decoder_reset(decoder);
    CHECK(lxa_decoder_feed(decoder, span("z", 1), cps, 4, &p)
           == LXA_ENCODING_OK && p.produced == 1 && cps[0] == 0xfffd);
    lxa_decoder_destroy(decoder);
    CHECK(lxa_decoder_create(utf8, LXA_ENCODING_FATAL, &allocator,
                              &decoder) == LXA_OK);
    CHECK(lxa_decoder_feed(decoder, span("abc", 3), cps, 4, &p)
           == LXA_ENCODING_MALFORMED && p.consumed == 1);
    lxa_decoder_destroy(decoder);

    utf8 = lxa_encoding_lookup(span("utf-16", 6));
    CHECK(utf8 == lxa_encoding_lookup(span("utf-16le", 8)));
    CHECK(lxa_encoder_create(utf8, LXA_ENCODING_REPLACE, &allocator,
                              &encoder) == LXA_OK);
    cps[0] = 0xfeff; cps[1] = 0x1f600; cps[2] = 0xd800;
    out.length = 5;
    CHECK(lxa_encoder_feed(encoder, cps, 3, out, &p)
           == LXA_ENCODING_OUTPUT_FULL);
    CHECK(p.consumed == 1 && p.produced == 2
           && bytes[0] == 0xff && bytes[1] == 0xfe);
    out.length = sizeof(bytes);
    CHECK(lxa_encoder_feed(encoder, cps + 1, 2, out, &p)
           == LXA_ENCODING_OK);
    CHECK(p.consumed == 2 && p.produced == 6
           && bytes[0] == 0x3d && bytes[1] == 0xd8
           && bytes[2] == 0x00 && bytes[3] == 0xde
           && bytes[4] == '?' && bytes[5] == 0);
    lxa_encoder_destroy(encoder);
    CHECK(lxa_decoder_create(utf8, LXA_ENCODING_REPLACE, &allocator,
                              &decoder) == LXA_OK);
    CHECK(lxa_decoder_feed(decoder, span("\x3d\xd8", 2), cps, 4, &p)
           == LXA_ENCODING_OK && p.consumed == 2 && p.produced == 0);
    CHECK(lxa_decoder_feed(decoder, span("\x00", 1), cps, 4, &p)
           == LXA_ENCODING_OK && p.produced == 0);
    CHECK(lxa_decoder_feed(decoder, span("\xde\x41\x00", 3), cps, 0, &p)
           == LXA_ENCODING_OUTPUT_FULL && p.consumed == 1);
    CHECK(lxa_decoder_feed(decoder, span("\x41\x00", 2), cps, 4, &p)
           == LXA_ENCODING_OK && p.consumed == 2 && p.produced == 2);
    CHECK(cps[0] == 0x1f600 && cps[1] == 'A');
    lxa_decoder_reset(decoder);
    CHECK(lxa_decoder_feed(decoder, span("\x00\xd8\x42\x00", 4), cps, 4, &p)
           == LXA_ENCODING_OK && p.produced == 2);
    CHECK(cps[0] == 0xfffd && cps[1] == 'B');
    lxa_decoder_reset(decoder);
    CHECK(lxa_decoder_feed(decoder, span("\x00\xd8\x41", 3), cps, 4, &p)
           == LXA_ENCODING_OK);
    CHECK(lxa_decoder_finish(decoder, cps, 4, &p)
           == LXA_ENCODING_OK && p.produced == 1 && cps[0] == 0xfffd);
    lxa_decoder_destroy(decoder);

    utf8 = lxa_encoding_lookup(span("utf-16be", 8));
    CHECK(lxa_encoder_create(utf8, LXA_ENCODING_FATAL, &allocator,
                              &encoder) == LXA_OK);
    cps[0] = 0x1f600;
    out.length = 3;
    CHECK(lxa_encoder_feed(encoder, cps, 1, out, &p)
           == LXA_ENCODING_OUTPUT_FULL && p.consumed == 0);
    out.length = sizeof(bytes);
    CHECK(lxa_encoder_feed(encoder, cps, 1, out, &p)
           == LXA_ENCODING_OK && p.produced == 4
           && bytes[0] == 0xd8 && bytes[1] == 0x3d
           && bytes[2] == 0xde && bytes[3] == 0);
    lxa_encoder_destroy(encoder);
    CHECK(lxa_decoder_create(utf8, LXA_ENCODING_FATAL, &allocator,
                              &decoder) == LXA_OK);
    CHECK(lxa_decoder_feed(decoder, span("\xd8\x3d\xde\x00", 4), cps, 0, &p)
           == LXA_ENCODING_OUTPUT_FULL && p.consumed == 4);
    CHECK(lxa_decoder_feed(decoder, span(NULL, 0), cps, 4, &p)
           == LXA_ENCODING_OK && p.produced == 1 && cps[0] == 0x1f600);
    CHECK(lxa_decoder_finish(decoder, cps, 4, &p) == LXA_ENCODING_OK);
    lxa_decoder_reset(decoder);
    CHECK(lxa_decoder_feed(decoder, span("\xdc\x00", 2), cps, 4, &p)
           == LXA_ENCODING_MALFORMED && p.consumed == 2);
    lxa_decoder_destroy(decoder);

    utf8 = lxa_encoding_lookup(span("euc-kr", 6));
    CHECK(utf8 == lxa_encoding_lookup(span("korean", 6)));
    CHECK(lxa_encoder_create(utf8, LXA_ENCODING_REPLACE, &allocator,
                              &encoder) == LXA_OK);
    cps[0] = 'A'; cps[1] = 0xac00; cps[2] = 0x1f600;
    out.length = 2;
    CHECK(lxa_encoder_feed(encoder, cps, 3, out, &p)
           == LXA_ENCODING_OUTPUT_FULL);
    CHECK(p.consumed == 1 && p.produced == 1 && bytes[0] == 'A');
    out.length = sizeof(bytes);
    CHECK(lxa_encoder_feed(encoder, cps + 1, 2, out, &p)
           == LXA_ENCODING_OK);
    CHECK(p.consumed == 2 && p.produced == 3 && bytes[0] == 0xb0
           && bytes[1] == 0xa1 && bytes[2] == '?');
    lxa_encoder_destroy(encoder);
    CHECK(lxa_decoder_create(utf8, LXA_ENCODING_REPLACE, &allocator,
                              &decoder) == LXA_OK);
    CHECK(lxa_decoder_feed(decoder, span("\xb0", 1), cps, 4, &p)
           == LXA_ENCODING_OK && p.consumed == 1 && p.produced == 0);
    CHECK(lxa_decoder_feed(decoder, span("\xa1", 1), cps, 0, &p)
           == LXA_ENCODING_OUTPUT_FULL && p.consumed == 0);
    CHECK(lxa_decoder_feed(decoder, span("\xa1", 1), cps, 4, &p)
           == LXA_ENCODING_OK && p.produced == 1 && cps[0] == 0xac00);
    lxa_decoder_reset(decoder);
    CHECK(lxa_decoder_feed(decoder, span("\x81 A", 3), cps, 4, &p)
           == LXA_ENCODING_OK && p.produced == 3);
    CHECK(cps[0] == 0xfffd && cps[1] == ' ' && cps[2] == 'A');
    lxa_decoder_reset(decoder);
    CHECK(lxa_decoder_feed(decoder, span("\x81", 1), cps, 4, &p)
           == LXA_ENCODING_OK);
    CHECK(lxa_decoder_finish(decoder, cps, 0, &p)
           == LXA_ENCODING_OUTPUT_FULL);
    CHECK(lxa_decoder_finish(decoder, cps, 4, &p)
           == LXA_ENCODING_OK && p.produced == 1 && cps[0] == 0xfffd);
    lxa_decoder_destroy(decoder);

    utf8 = lxa_encoding_lookup(span("euc-jp", 6));
    CHECK(lxa_encoder_create(utf8, LXA_ENCODING_REPLACE, &allocator,
                              &encoder) == LXA_OK);
    cps[0] = 'A'; cps[1] = 0x3000; cps[2] = 0xff61; cps[3] = 0x2212;
    out.length = 2;
    CHECK(lxa_encoder_feed(encoder, cps, 4, out, &p)
           == LXA_ENCODING_OUTPUT_FULL && p.consumed == 1 && p.produced == 1);
    CHECK(bytes[0] == 'A');
    out.length = sizeof(bytes);
    CHECK(lxa_encoder_feed(encoder, cps + 1, 3, out, &p)
           == LXA_ENCODING_OK && p.consumed == 3 && p.produced == 6);
    CHECK(bytes[0] == 0xa1 && bytes[1] == 0xa1
           && bytes[2] == 0x8e && bytes[3] == 0xa1
           && bytes[4] == 0xa1 && bytes[5] == 0xdd);
    cps[0] = 0xa5; cps[1] = 0x203e; cps[2] = 0x1f600;
    CHECK(lxa_encoder_feed(encoder, cps, 3, out, &p)
           == LXA_ENCODING_OK && p.produced == 3
           && bytes[0] == '\\' && bytes[1] == '~' && bytes[2] == '?');
    lxa_encoder_destroy(encoder);
    CHECK(lxa_encoder_create(utf8, LXA_ENCODING_FATAL, &allocator,
                              &encoder) == LXA_OK);
    CHECK(lxa_encoder_feed(encoder, cps + 2, 1, out, &p)
           == LXA_ENCODING_MALFORMED && p.consumed == 1 && p.produced == 0);
    CHECK(lxa_encoder_feed(encoder, cps, 1, out, &p)
           == LXA_ENCODING_INVALID_ARGUMENT);
    lxa_encoder_destroy(encoder);
    CHECK(lxa_decoder_create(utf8, LXA_ENCODING_REPLACE, &allocator,
                              &decoder) == LXA_OK);
    CHECK(lxa_decoder_feed(decoder, span("\x8f\xa2", 2), cps, 4, &p)
           == LXA_ENCODING_OK && p.produced == 0);
    CHECK(lxa_decoder_feed(decoder, span("\xaf\xa1\xa1\x8e\xa1", 5),
                            cps, 4, &p)
           == LXA_ENCODING_OK && p.produced == 3);
    CHECK(cps[0] == 0x2d8 && cps[1] == 0x3000 && cps[2] == 0xff61);
    lxa_decoder_reset(decoder);
    CHECK(lxa_decoder_feed(decoder, span("\x8f\xa2", 2), cps, 4, &p)
           == LXA_ENCODING_OK);
    CHECK(lxa_decoder_feed(decoder, span("Z", 1), cps, 4, &p)
           == LXA_ENCODING_OK && p.produced == 2);
    CHECK(cps[0] == 0xfffd && cps[1] == 'Z');
    lxa_decoder_reset(decoder);
    CHECK(lxa_decoder_feed(decoder, span("\x8f\xa2", 2), cps, 4, &p)
           == LXA_ENCODING_OK);
    CHECK(lxa_decoder_finish(decoder, cps, 4, &p)
           == LXA_ENCODING_OK && p.produced == 1 && cps[0] == 0xfffd);
    lxa_decoder_destroy(decoder);
    return 0;
}
