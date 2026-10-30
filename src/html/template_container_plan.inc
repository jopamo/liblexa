/* Accepted template-container recovery before admission or pops.
 * LXA_TEMPLATE_CONTAINER_{NAME,VIEW,LENGTH,RUBY,LIST,PARAGRAPH,READ,WORK}
 * belong to the includer.
 */
static inline lxa_status_t
LXA_TEMPLATE_CONTAINER_NAME(const LXA_TEMPLATE_CONTAINER_VIEW *view,
    size_t boundary, lxa_span_t name, bool quirks, lxa_html_template_container_plan_t *out)
{
    size_t length = LXA_TEMPLATE_CONTAINER_LENGTH(view);
    if (out == NULL || boundary > length || name.data == NULL || name.length == 0)
        return LXA_ERROR_ARGUMENT;
    lxa_html_template_container_plan_t plan = {length, false};
    lxa_status_t status = LXA_OK;
    if (lxa_html_tree_ruby_annotation(name))
        status = LXA_TEMPLATE_CONTAINER_RUBY(view, boundary, name, &plan.keep);
    if (status != LXA_OK) return status;
    if (lxa_html_tree_template_list_item(name)) {
        status = LXA_TEMPLATE_CONTAINER_LIST(view, boundary,
            !lxa_html_tree_name_equals(name, "li", 2), &plan.keep);
        if (status != LXA_OK) return status;
        if (plan.keep > boundary && plan.keep < length) {
            lxa_span_t previous;
            status = LXA_TEMPLATE_CONTAINER_WORK(view, 1);
            if (status == LXA_OK)
                status = LXA_TEMPLATE_CONTAINER_READ(view, plan.keep - 1, &previous);
            if (status != LXA_OK) return status;
            if (lxa_html_tree_name_equals(previous, "table", 5)) {
                plan.foster = true;
                *out = plan;
                return LXA_OK;
            }
        }
        if (plan.keep == length)
            status = LXA_TEMPLATE_CONTAINER_PARAGRAPH(view, boundary, &plan.keep);
    } else if (lxa_html_tree_template_paragraph_container(name)
        || lxa_html_tree_name_equals(name, "form", 4)
        || lxa_html_tree_name_equals(name, "plaintext", 9)
        || (!quirks && lxa_html_tree_name_equals(name, "table", 5))) {
        status = LXA_TEMPLATE_CONTAINER_PARAGRAPH(view, boundary, &plan.keep);
        if (status == LXA_OK && lxa_html_tree_heading(name) && plan.keep > boundary) {
            lxa_span_t current;
            status = LXA_TEMPLATE_CONTAINER_WORK(view, 1);
            if (status == LXA_OK)
                status = LXA_TEMPLATE_CONTAINER_READ(view, plan.keep - 1, &current);
            if (status == LXA_OK && lxa_html_tree_heading(current)) --plan.keep;
        }
    }
    if (status == LXA_OK) *out = plan;
    return status;
}
