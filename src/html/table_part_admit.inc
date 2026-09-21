/* Consume one prepared child plan; owners retain completed implied entries. */
static inline lxa_status_t
LXA_TABLE_PART_ADMIT_NAME(LXA_TABLE_PART_ADMIT_VIEW *view,
    const lxa_html_token_t *token, const lxa_html_prepared_table_part_plan_t *plan)
{
    lxa_status_t status = LXA_OK;
    if (plan->extra != 0) status = LXA_TABLE_PART_ADMIT_RESERVE(view, plan->extra);
    if (status == LXA_OK && plan->preflight_nodes != 0)
        status = LXA_TABLE_PART_ADMIT_NODE(view, plan->preflight_nodes);
    if (status != LXA_OK) return status;
    lxa_html_token_t implied = {.kind = LXA_HTML_START_TAG};
    if (plan->implied_group) {
        implied.name = (lxa_span_t){(const uint8_t *) "colgroup", 8};
        status = LXA_TABLE_PART_ADMIT_INSERT(view, &implied);
    }
    if (status == LXA_OK && plan->implied_section) {
        implied.name = (lxa_span_t){(const uint8_t *) "tbody", 5};
        status = LXA_TABLE_PART_ADMIT_INSERT(view, &implied);
    }
    if (status == LXA_OK && plan->implied_row) {
        implied.name = (lxa_span_t){(const uint8_t *) "tr", 2};
        status = LXA_TABLE_PART_ADMIT_INSERT(view, &implied);
    }
    if (status != LXA_OK) return status;
    return plan->void_element ? LXA_TABLE_PART_ADMIT_VOID(view, token)
        : LXA_TABLE_PART_ADMIT_INSERT(view, token);
}
