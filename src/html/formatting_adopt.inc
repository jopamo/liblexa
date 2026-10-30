/* Select one adoption outer round; owners apply the selected mutations.
 * LXA_ADOPT_{NAME,VIEW,FORMATTING_LENGTH,OPEN_LENGTH,FIND,REFERENCE,OPEN,SCOPED,SPECIAL,IDENTITY_TYPE}
 * belong to the includer. Queries own their WORK charges.
 */
static inline lxa_status_t
LXA_ADOPT_NAME(const LXA_ADOPT_VIEW *view, lxa_span_t subject,
    lxa_html_formatting_adopt_plan_t *out)
{
    size_t length = LXA_ADOPT_OPEN_LENGTH(view), scoped;
    LXA_ADOPT_IDENTITY_TYPE identity;
    lxa_html_formatting_adopt_plan_t plan = {
        LXA_HTML_FORMATTING_ADOPT_ORDINARY, LXA_ADOPT_FORMATTING_LENGTH(view), length, length};
    lxa_status_t status = LXA_ADOPT_FIND(view, subject, &plan.formatting);
    if (status != LXA_OK) return status;
    if (plan.formatting == LXA_ADOPT_FORMATTING_LENGTH(view)) goto publish;
    status = LXA_ADOPT_REFERENCE(view, plan.formatting, &identity);
    if (status == LXA_OK) status = LXA_ADOPT_OPEN(view, identity, &plan.open);
    if (status != LXA_OK) return status;
    if (plan.open == length) {
        plan.action = LXA_HTML_FORMATTING_ADOPT_REMOVE_CLOSED;
        goto publish;
    }
    status = LXA_ADOPT_SCOPED(view, identity, &scoped);
    if (status != LXA_OK) return status;
    if (scoped == length) {
        plan.action = LXA_HTML_FORMATTING_ADOPT_IGNORE;
        goto publish;
    }
    status = LXA_ADOPT_SPECIAL(view, plan.open + 1, &plan.furthest);
    if (status != LXA_OK) return status;
    plan.action = plan.furthest == length
        ? LXA_HTML_FORMATTING_ADOPT_POP
        : LXA_HTML_FORMATTING_ADOPT_REPAIR;
publish:
    *out = plan;
    return LXA_OK;
}
