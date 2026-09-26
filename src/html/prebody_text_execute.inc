/* Owners retain whitespace insertion and root admission effects. */
static inline lxa_status_t
LXA_PREBODY_TEXT_NAME(LXA_PREBODY_TEXT_VIEW *view, lxa_span_t *bytes)
{
    lxa_html_prebody_text_plan_t plan;
    lxa_status_t status = LXA_PREBODY_TEXT_PLAN(view, *bytes, &plan);
    if (status != LXA_OK) return status;
    if (plan.emit_prefix) {
        lxa_span_t prefix = {bytes->data, plan.space_prefix};
        status = LXA_PREBODY_TEXT_EMIT(view, prefix);
        if (status != LXA_OK) return status;
    }
    if (plan.space_prefix != 0) bytes->data += plan.space_prefix;
    bytes->length -= plan.space_prefix;
    if (bytes->length == 0) return LXA_OK;
    if (plan.ensure_head) {
        status = LXA_PREBODY_TEXT_HEAD(view);
        if (status != LXA_OK) return status;
    }
    LXA_PREBODY_TEXT_MODE(view) = LXA_TREE_AFTER_HEAD;
    return LXA_OK;
}
