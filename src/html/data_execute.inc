/* Engines own insertion/queueing and first-NUL recovery. Input stays borrowed. */
static inline lxa_status_t
LXA_DATA_EXECUTE_NAME(LXA_DATA_EXECUTE_VIEW *view, lxa_span_t bytes,
    bool foreign, bool table)
{
    static const uint8_t replacement[] = {0xef, 0xbf, 0xbd};
    lxa_status_t status = lxa_budget_charge(LXA_DATA_EXECUTE_BUDGET(view),
        LXA_BUDGET_WORK, bytes.length, LXA_DATA_EXECUTE_REASON(view));
    if (status != LXA_OK) return status;
    size_t offset = 0;
    bool check_column_group = table && !foreign;
    while (offset < bytes.length) {
        lxa_html_data_run_t run;
        status = lxa_html_tree_data_run(bytes, offset, &run);
        if (status != LXA_OK) return status;
        if (run.bytes.length != 0) {
            status = LXA_DATA_EXECUTE_APPEND(view, run.bytes, table);
            if (status != LXA_OK) return status;
        }
        if (!run.nul) break;
        if (check_column_group) {
            check_column_group = false;
            status = LXA_DATA_EXECUTE_NULL(view);
            if (status != LXA_OK) return status;
        }
        if (foreign) {
            status = LXA_DATA_EXECUTE_APPEND(view,
                ((lxa_span_t) {replacement, sizeof(replacement)}), false);
            if (status != LXA_OK) return status;
        }
        offset = run.next;
    }
    return LXA_OK;
}
