/* Recover an in-scope caption before root table-part reprocessing. */
static inline lxa_status_t
LXA_ROOT_CAPTION_NAME(LXA_ROOT_CAPTION_VIEW *view, lxa_span_t incoming)
{
    size_t index;
    size_t needed = lxa_html_tree_name_equals(incoming, "td", 2)
            || lxa_html_tree_name_equals(incoming, "th", 2)
        ? 3
        : lxa_html_tree_name_equals(incoming, "tr", 2)
            || lxa_html_tree_name_equals(incoming, "col", 3)
        ? 2
        : 1;
    lxa_status_t status = LXA_ROOT_CAPTION_FIND(view, &index);
    if (status != LXA_OK || index == LXA_ROOT_CAPTION_LENGTH(view)) return status;
    size_t removed = LXA_ROOT_CAPTION_LENGTH(view) - index;
    size_t slots = lxa_html_tree_name_equals(incoming, "col", 3) ? 1 : needed;
    status = LXA_ROOT_CAPTION_RESERVE(view, slots > removed ? slots - removed : 0);
    if (status == LXA_OK) status = LXA_ROOT_CAPTION_NODE(view, needed);
    if (status == LXA_OK) status = LXA_ROOT_CAPTION_POP(view, index);
    return status;
}
