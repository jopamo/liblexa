#include "check.h"
#include "css/filter.h"

int
main(void)
{
    lxa_css_filter_t filter = {0};
    lxa_css_filter_progress_t p;
    uint32_t output[8];
    const uint32_t first[] = {'A', 0x0d};
    const uint32_t second[] = {0x0a, 0x0c, 0, 0xd800, 0x1f600};
    const uint32_t third[] = {0x0d, 'B'};
    const uint32_t invalid[] = {0x110000};

    CSS_CHECK(lxa_css_filter_feed(&filter, first, 2, output, 8, &p)
           == LXA_CSS_FILTER_OK);
    CSS_CHECK(p.consumed == 2 && p.produced == 1 && output[0] == 'A');
    CSS_CHECK(lxa_css_filter_feed(&filter, second, 5, output, 0, &p)
           == LXA_CSS_FILTER_OUTPUT_FULL);
    CSS_CHECK(p.consumed == 0 && p.produced == 0 && filter.pending_cr);
    CSS_CHECK(lxa_css_filter_feed(&filter, second, 5, output, 8, &p)
           == LXA_CSS_FILTER_OK);
    CSS_CHECK(p.consumed == 5 && p.produced == 5);
    CSS_CHECK(output[0] == 0x0a && output[1] == 0x0a
           && output[2] == 0xfffd && output[3] == 0xfffd
           && output[4] == 0x1f600);

    CSS_CHECK(lxa_css_filter_feed(&filter, third, 2, output, 1, &p)
           == LXA_CSS_FILTER_OUTPUT_FULL);
    CSS_CHECK(p.consumed == 1 && p.produced == 1 && output[0] == 0x0a);
    CSS_CHECK(lxa_css_filter_feed(&filter, third + 1, 1, output, 1, &p)
           == LXA_CSS_FILTER_OK);
    CSS_CHECK(p.consumed == 1 && p.produced == 1 && output[0] == 'B');
    CSS_CHECK(lxa_css_filter_finish(&filter, output, 0, &p)
           == LXA_CSS_FILTER_OK && p.produced == 0);
    CSS_CHECK(lxa_css_filter_finish(&filter, output, 1, &p)
           == LXA_CSS_FILTER_INVALID_ARGUMENT);
    CSS_CHECK(lxa_css_filter_feed(&filter, third + 1, 1, output, 1, &p)
           == LXA_CSS_FILTER_INVALID_ARGUMENT);

    lxa_css_filter_reset(&filter);
    CSS_CHECK(lxa_css_filter_feed(&filter, invalid, 1, output, 8, &p)
           == LXA_CSS_FILTER_INVALID_ARGUMENT);
    CSS_CHECK(p.consumed == 0 && p.produced == 0 && !filter.pending_cr);
    CSS_CHECK(lxa_css_filter_feed(&filter, third + 1, 1, output, 1, &p)
           == LXA_CSS_FILTER_OK);
    CSS_CHECK(p.consumed == 1 && p.produced == 1 && output[0] == 'B');
    CSS_CHECK(lxa_css_filter_feed(&filter, third, 1, NULL, 0, &p)
           == LXA_CSS_FILTER_OK && p.consumed == 1 && filter.pending_cr);
    CSS_CHECK(lxa_css_filter_finish(&filter, output, 0, &p)
           == LXA_CSS_FILTER_OUTPUT_FULL && filter.pending_cr);
    CSS_CHECK(lxa_css_filter_finish(&filter, output, 1, &p)
           == LXA_CSS_FILTER_OK && p.produced == 1 && output[0] == 0x0a);
    return 0;
}
