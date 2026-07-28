#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <liblexa/html.h>
#include <liblexa/serialize.h>
#include "lib/fetch/html.h"

/* Link the actual bx consumers with --wrap=lxa_html_document_parse.
 * Allocation accounting runs separately from the timed iterations. */
static struct {
    bool accounting;
    lxa_allocator_t original;
    size_t allocations, requested, records, text_calls, text_bytes;
    double parse_seconds, serialize_seconds;
} measurement;
static bool checking = true;

static double
now(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        perror("clock_gettime");
        exit(1);
    }
    return (double)value.tv_sec + (double)value.tv_nsec / 1e9;
}

static void *
allocate(void *context, size_t size)
{
    lxa_allocator_t *original = context;
    measurement.allocations++;
    measurement.requested += size;
    return original->allocate(original->context, size);
}

static void *
resize(void *context, void *pointer, size_t size)
{
    lxa_allocator_t *original = context;
    measurement.allocations++;
    measurement.requested += size;
    return original->resize(original->context, pointer, size);
}

static void
release(void *context, void *pointer)
{
    lxa_allocator_t *original = context;
    original->release(original->context, pointer);
}

lxa_status_t __real_lxa_html_document_parse(const lxa_allocator_t *,
    const lxa_limits_t *, lxa_span_t, lxa_html_result_t *,
    lxa_limit_reason_t *, lxa_html_document_t **);
lxa_status_t __wrap_lxa_html_document_parse(const lxa_allocator_t *,
    const lxa_limits_t *, lxa_span_t, lxa_html_result_t *,
    lxa_limit_reason_t *, lxa_html_document_t **);

lxa_status_t
__wrap_lxa_html_document_parse(const lxa_allocator_t *allocator,
    const lxa_limits_t *limits, lxa_span_t input, lxa_html_result_t *result,
    lxa_limit_reason_t *reason, lxa_html_document_t **out)
{
    lxa_allocator_t counted = {&measurement.original, allocate, resize, release};
    if (measurement.accounting) {
        measurement.original = *allocator;
        allocator = &counted;
    }
    double start = now();
    lxa_status_t status = __real_lxa_html_document_parse(
        allocator, limits, input, result, reason, out);
    measurement.parse_seconds += now() - start;
    if (status == LXA_OK && measurement.accounting)
        measurement.records = lxa_dom_nodes_length(lxa_html_document_nodes(*out));
    return status;
}

lxa_status_t __real_lxa_html_serialize_document(lxa_dom_nodes_t *,
    lxa_dom_ref_t, lxa_write_f, void *, lxa_budget_kind_t *);
lxa_status_t __wrap_lxa_html_serialize_document(lxa_dom_nodes_t *,
    lxa_dom_ref_t, lxa_write_f, void *, lxa_budget_kind_t *);

lxa_status_t
__wrap_lxa_html_serialize_document(lxa_dom_nodes_t *nodes, lxa_dom_ref_t root,
    lxa_write_f write, void *context, lxa_budget_kind_t *reason)
{
    double start = now();
    lxa_status_t status = __real_lxa_html_serialize_document(
        nodes, root, write, context, reason);
    measurement.serialize_seconds += now() - start;
    return status;
}

lxa_status_t __real_lxa_dom_nodes_text_content(lxa_dom_nodes_t *,
    lxa_dom_ref_t, lxa_buffer_t *, size_t);
lxa_status_t __wrap_lxa_dom_nodes_text_content(lxa_dom_nodes_t *,
    lxa_dom_ref_t, lxa_buffer_t *, size_t);

lxa_status_t
__wrap_lxa_dom_nodes_text_content(lxa_dom_nodes_t *nodes, lxa_dom_ref_t root,
    lxa_buffer_t *output, size_t limit)
{
    size_t previous = output->length;
    lxa_status_t status = __real_lxa_dom_nodes_text_content(
        nodes, root, output, limit);
    if (measurement.accounting && status == LXA_OK) {
        measurement.text_calls++;
        measurement.text_bytes += output->length - previous;
    }
    return status;
}

typedef struct {
    uint64_t digest;
    size_t links, bases, url_bytes, output_bytes;
    bool replace;
} output_t;

static void
hash(output_t *output, const char *bytes, size_t length)
{
    if (!checking) return;
    for (size_t i = 0; i < length; i++)
        output->digest = (output->digest ^ (unsigned char)bytes[i])
                         * UINT64_C(1099511628211);
}

static void
link_callback(void *context, const char *url, BxFetchHtmlLinkKind kind)
{
    output_t *output = context;
    output->links++;
    output->url_bytes += strlen(url);
    output->digest ^= (uint64_t)kind;
    hash(output, url, strlen(url) + 1);
}

static int
base_callback(void *context, const char *url)
{
    output_t *output = context;
    output->bases++;
    output->url_bytes += strlen(url);
    hash(output, url, strlen(url) + 1);
    return 0;
}

static char *
rewrite_callback(void *context, const char *url)
{
    output_t *output = context;
    link_callback(context, url, BX_FETCH_HTML_LINK_REQUISITE);
    if (!output->replace) return NULL;
    char *replacement = strdup("/local");
    if (replacement == NULL) {
        perror("strdup");
        exit(1);
    }
    return replacement;
}

static int
run(const char *mode, const char *input, size_t length, output_t *output)
{
    *output = (output_t){.digest = UINT64_C(14695981039346656037)};
    if (strcmp(mode, "extract") == 0 || strcmp(mode, "extract-no-base") == 0)
        return bx_fetch_html_extract_links(input, length, link_callback, output,
            strcmp(mode, "extract") == 0 ? base_callback : NULL);
    char *bytes;
    if (strcmp(mode, "markdown") == 0) {
        bytes = bx_fetch_html_to_markdown("https://example.org/page", input,
            length, true, &output->output_bytes);
    } else {
        output->replace = strcmp(mode, "rewrite-all") == 0;
        bytes = bx_fetch_html_convert_links(input, length, rewrite_callback,
            output, base_callback);
        if (bytes != NULL) output->output_bytes = strlen(bytes);
    }
    if (bytes == NULL) return -1;
    hash(output, bytes, output->output_bytes);
    free(bytes);
    return 0;
}

int
main(int argc, char **argv)
{
    if (argc != 4 || (strcmp(argv[1], "extract") != 0
        && strcmp(argv[1], "extract-no-base") != 0
        && strcmp(argv[1], "rewrite-none") != 0
        && strcmp(argv[1], "rewrite-all") != 0
        && strcmp(argv[1], "markdown") != 0)) {
        fputs("usage: bx_workloads MODE FILE ITERATIONS\n"
              "MODE: extract, extract-no-base, rewrite-none, rewrite-all, markdown\n",
              stderr);
        return 2;
    }
    char *end;
    errno = 0;
    unsigned long iterations = strtoul(argv[3], &end, 10);
    if (errno != 0 || end == argv[3] || *end != '\0'
        || iterations == 0 || iterations > 100000) return 2;
    FILE *file = fopen(argv[2], "rb");
    if (file == NULL) {
        perror(argv[2]);
        return 1;
    }
    long length;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0
        || (unsigned long)length > BX_FETCH_DOCUMENT_PARSE_MAX_BYTES
        || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 2;
    }
    char *input = malloc((size_t)length + 1);
    if (input == NULL) {
        fclose(file);
        return 1;
    }
    size_t read_length = fread(input, 1, (size_t)length, file);
    int read_error = ferror(file);
    int close_error = fclose(file);
    if (read_length != (size_t)length || read_error || close_error) {
        free(input);
        return 1;
    }
    input[length] = '\0';
    output_t expected, output;
    measurement.accounting = true;
    int failed = run(argv[1], input, (size_t)length, &expected);
    measurement.accounting = false;
    for (size_t i = 0; !failed && i < 10; i++)
        failed = run(argv[1], input, (size_t)length, &output);
    measurement.parse_seconds = 0;
    measurement.serialize_seconds = 0;
    checking = false;
    double start = now();
    for (unsigned long i = 0; !failed && i < iterations; i++) {
        failed = run(argv[1], input, (size_t)length, &output);
        if (!failed && (output.links != expected.links
            || output.bases != expected.bases
            || output.url_bytes != expected.url_bytes
            || output.output_bytes != expected.output_bytes)) {
            fputs("inconsistent workload output\n", stderr);
            failed = 1;
        }
    }
    double seconds = now() - start;
    double parse_seconds = measurement.parse_seconds;
    double serialize_seconds = measurement.serialize_seconds;
    checking = true;
    if (!failed) {
        failed = run(argv[1], input, (size_t)length, &output);
        if (!failed && output.digest != expected.digest) {
            fputs("inconsistent workload output\n", stderr);
            failed = 1;
        }
    }
    free(input);
    if (failed) {
        perror("bx HTML workload");
        return 1;
    }
    printf("mode=%s input=%ld iterations=%lu total_ms=%.3f parse_ms=%.3f "
           "serialize_ms=%.3f "
           "parse_allocations=%zu parse_requested_bytes=%zu records=%zu "
           "links=%zu bases=%zu url_bytes=%zu output_bytes=%zu "
           "text_calls=%zu text_bytes=%zu digest=%" PRIu64 "\n",
           argv[1], length, iterations, seconds * 1e3,
           parse_seconds * 1e3, serialize_seconds * 1e3,
           measurement.allocations,
           measurement.requested, measurement.records, expected.links,
           expected.bases, expected.url_bytes, expected.output_bytes,
           measurement.text_calls, measurement.text_bytes, expected.digest);
    return 0;
}
