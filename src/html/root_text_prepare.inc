/* Publish preparation before owner-specific insertion and tokenizer feedback. */
static inline lxa_status_t
LXA_ROOT_TEXT_PREPARE_NAME(LXA_ROOT_TEXT_PREPARE_VIEW *view,
    const lxa_html_root_text_plan_t *plan, lxa_html_root_mode_t *mode)
{
    lxa_status_t status = plan->body
        ? LXA_ROOT_TEXT_PREPARE_BODY(view)
        : LXA_ROOT_TEXT_PREPARE_HEAD(view);
    if (status != LXA_OK) return status;
    *mode = plan->return_mode;
    if (plan->disable_frameset) LXA_ROOT_TEXT_PREPARE_FRAMESET(view) = false;
    return LXA_OK;
}
