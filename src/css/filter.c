#include "css/filter.h"

void
lxa_css_filter_reset(lxa_css_filter_t *filter)
{
    if (filter != NULL) {
        filter->pending_cr = false;
        filter->finished = false;
    }
}

lxa_css_filter_result_t
lxa_css_filter_feed(lxa_css_filter_t *filter, const uint32_t *input,
                    size_t length, uint32_t *output, size_t capacity,
                    lxa_css_filter_progress_t *progress)
{
    if (progress == NULL) {
        return LXA_CSS_FILTER_INVALID_ARGUMENT;
    }
    progress->consumed = 0;
    progress->produced = 0;
    if (filter == NULL || filter->finished || (input == NULL && length != 0)
        || (output == NULL && capacity != 0)) {
        return LXA_CSS_FILTER_INVALID_ARGUMENT;
    }
    while (progress->consumed < length) {
        uint32_t cp = input[progress->consumed];
        if (cp > 0x10ffff) {
            return LXA_CSS_FILTER_INVALID_ARGUMENT;
        }
        if (filter->pending_cr) {
            if (progress->produced == capacity) {
                return LXA_CSS_FILTER_OUTPUT_FULL;
            }
            output[progress->produced++] = 0x0a;
            filter->pending_cr = false;
            if (cp == 0x0a) {
                progress->consumed++;
                continue;
            }
        }
        if (cp == 0x0d) {
            filter->pending_cr = true;
            progress->consumed++;
            continue;
        }
        if (progress->produced == capacity) {
            return LXA_CSS_FILTER_OUTPUT_FULL;
        }
        if (cp == 0x0c) {
            cp = 0x0a;
        }
        else if (cp == 0 || (cp >= 0xd800 && cp <= 0xdfff)) {
            cp = 0xfffd;
        }
        output[progress->produced++] = cp;
        progress->consumed++;
    }
    return LXA_CSS_FILTER_OK;
}

lxa_css_filter_result_t
lxa_css_filter_finish(lxa_css_filter_t *filter, uint32_t *output,
                      size_t capacity, lxa_css_filter_progress_t *progress)
{
    if (progress == NULL) {
        return LXA_CSS_FILTER_INVALID_ARGUMENT;
    }
    progress->consumed = 0;
    progress->produced = 0;
    if (filter == NULL || filter->finished || (output == NULL && capacity != 0)) {
        return LXA_CSS_FILTER_INVALID_ARGUMENT;
    }
    if (filter->pending_cr) {
        if (capacity == 0) {
            return LXA_CSS_FILTER_OUTPUT_FULL;
        }
        output[0] = 0x0a;
        progress->produced = 1;
        filter->pending_cr = false;
    }
    filter->finished = true;
    return LXA_CSS_FILTER_OK;
}
