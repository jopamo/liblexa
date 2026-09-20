/* Consume one prepared child plan; owners retain completed implied entries. */
static inline lxa_status_t
LXA_TABLE_PART_ADMIT_NAME(LXA_TABLE_PART_ADMIT_VIEW *view,
    const lxa_html_token_t *token, const lxa_html_virtual_table_start_plan_t *plan)
{
    lxa_status_t status = LXA_TABLE_PART_ADMIT_RESERVE(view, plan->extra);
    if (status == LXA_OK) status = LXA_TABLE_PART_ADMIT_NODE(view, plan->extra);
    if (status != LXA_OK) return status;
    lxa_html_token_t implied = {.kind = LXA_HTML_START_TAG};
    if (plan->implied_section) {
        implied.name = (lxa_span_t){(const uint8_t *) "tbody", 5};
        status = LXA_TABLE_PART_ADMIT_INSERT(view, &implied);
    }
    if (status == LXA_OK && plan->implied_row) {
        implied.name = (lxa_span_t){(const uint8_t *) "tr", 2};
        status = LXA_TABLE_PART_ADMIT_INSERT(view, &implied);
    }
    return status == LXA_OK ? LXA_TABLE_PART_ADMIT_INSERT(view, token) : status;
}
