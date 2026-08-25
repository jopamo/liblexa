/* Instantiate end-target searches with a borrowed stack and static access.
 * LXA_SCOPE_{NAME,VIEW,LENGTH,BUDGET,REASON,READ,PLAN_NAME,IDENTITY},
 * LXA_ORDINARY_{NAME,PLAN_NAME}, LXA_{OPEN,SCOPED}_REFERENCE_NAME and
 * LXA_COLUMN_GROUP_NAME and LXA_{IMPLIED,RUBY}_PLAN_NAME belong to the includer.
 */
static inline lxa_status_t
LXA_SCOPE_NAME(const LXA_SCOPE_VIEW *view, size_t boundary, lxa_span_t target,
               lxa_html_scope_kind_t scope, size_t *index)
{
    bool any_heading = lxa_html_tree_heading(target);
    size_t i = LXA_SCOPE_LENGTH(view);
    *index = i;
    while (i > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge(
            LXA_SCOPE_BUDGET(view), LXA_BUDGET_WORK, 1, LXA_SCOPE_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_SCOPE_READ(view, --i, &name, &ns);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML
            && (any_heading ? lxa_html_tree_heading(name)
                : lxa_html_tree_name_equals(name, (const char *) target.data,
                                             target.length))) {
            *index = i;
            return LXA_OK;
        }
        if (lxa_html_tree_scope_boundary(name, ns, scope)) break;
    }
    return LXA_OK;
}

static inline lxa_status_t
LXA_COLUMN_GROUP_NAME(const LXA_SCOPE_VIEW *view, size_t boundary, bool *found)
{
    size_t length = LXA_SCOPE_LENGTH(view);
    *found = false;
    if (length > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = LXA_SCOPE_READ(view, length - 1, &name, &ns);
        if (status != LXA_OK) return status;
        *found = ns == LXA_DOM_NS_HTML
            && lxa_html_tree_name_equals(name, "colgroup", 8);
    }
    return LXA_OK;
}

static inline lxa_status_t
LXA_SCOPE_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t boundary, lxa_span_t target,
                    lxa_html_scope_kind_t scope, lxa_html_end_target_plan_t *out)
{
    LXA_SCOPE_VIEW search = *view;
    lxa_html_end_target_plan_t plan;
    bool column_group;
    lxa_status_t status = LXA_COLUMN_GROUP_NAME(view, boundary, &column_group);
    if (status != LXA_OK) return status;
    column_group = column_group && !lxa_html_tree_name_equals(target, "colgroup", 8);
    if (column_group) --LXA_SCOPE_LENGTH(&search);
    plan.search_length = LXA_SCOPE_LENGTH(&search);
    status = LXA_SCOPE_NAME(&search, boundary, target, scope, &plan.index);
    if (status == LXA_OK && column_group)
        status = lxa_budget_charge(LXA_SCOPE_BUDGET(view), LXA_BUDGET_WORK,
                                   1, LXA_SCOPE_REASON(view));
    if (status == LXA_OK) *out = plan;
    return status;
}

static inline lxa_status_t
LXA_OPEN_REFERENCE_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
                        uint64_t target, size_t *index)
{
    size_t length = LXA_SCOPE_LENGTH(view);
    *index = length;
    for (size_t i = boundary; i < length; ++i) {
        uint64_t identity;
        lxa_status_t status = lxa_budget_charge(
            LXA_SCOPE_BUDGET(view), LXA_BUDGET_WORK, 1, LXA_SCOPE_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_SCOPE_IDENTITY(view, i, &identity);
        if (status != LXA_OK) return status;
        if (identity == target) {
            *index = i;
            break;
        }
    }
    return LXA_OK;
}

static inline lxa_status_t
LXA_SCOPED_REFERENCE_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
                          uint64_t target, size_t *index)
{
    size_t i = LXA_SCOPE_LENGTH(view);
    *index = i;
    while (i > boundary) {
        uint64_t identity;
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge(
            LXA_SCOPE_BUDGET(view), LXA_BUDGET_WORK, 1, LXA_SCOPE_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_SCOPE_IDENTITY(view, --i, &identity);
        if (status != LXA_OK) return status;
        if (identity == target) {
            *index = i;
            break;
        }
        status = LXA_SCOPE_READ(view, i, &name, &ns);
        if (status != LXA_OK) return status;
        if (lxa_html_tree_scope_boundary(name, ns, LXA_HTML_SCOPE_NORMAL)) break;
    }
    return LXA_OK;
}

static inline lxa_status_t
LXA_ORDINARY_NAME(const LXA_SCOPE_VIEW *view, size_t boundary, lxa_span_t target,
                  size_t *index)
{
    size_t i = LXA_SCOPE_LENGTH(view);
    *index = i;
    while (i > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge(
            LXA_SCOPE_BUDGET(view), LXA_BUDGET_WORK, 1, LXA_SCOPE_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_SCOPE_READ(view, --i, &name, &ns);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML && name.length == target.length) {
            size_t blocks = name.length / 8 + (name.length % 8 != 0);
            status = lxa_budget_charge(LXA_SCOPE_BUDGET(view), LXA_BUDGET_WORK,
                                       blocks, LXA_SCOPE_REASON(view));
            if (status != LXA_OK) return status;
            if (lxa_html_tree_name_equals(name, (const char *) target.data,
                                          target.length)) {
                *index = i;
                return LXA_OK;
            }
        }
        if (lxa_html_tree_special_element(name, ns)) break;
    }
    return LXA_OK;
}

static inline lxa_status_t
LXA_ORDINARY_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
                       lxa_span_t target, lxa_html_end_target_plan_t *out)
{
    LXA_SCOPE_VIEW search = *view;
    lxa_html_end_target_plan_t plan;
    bool column_group;
    lxa_status_t status = LXA_COLUMN_GROUP_NAME(view, boundary, &column_group);
    if (status != LXA_OK) return status;
    if (column_group) --LXA_SCOPE_LENGTH(&search);
    plan.search_length = LXA_SCOPE_LENGTH(&search);
    status = LXA_ORDINARY_NAME(&search, boundary, target, &plan.index);
    if (status == LXA_OK && column_group)
        status = lxa_budget_charge(LXA_SCOPE_BUDGET(view), LXA_BUDGET_WORK,
                                   1, LXA_SCOPE_REASON(view));
    if (status == LXA_OK) *out = plan;
    return status;
}
static inline lxa_status_t
LXA_IMPLIED_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
                      lxa_span_t except, size_t *out)
{
    size_t keep = LXA_SCOPE_LENGTH(view);
    while (keep > boundary) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        lxa_status_t status = lxa_budget_charge(
            LXA_SCOPE_BUDGET(view), LXA_BUDGET_WORK, 1, LXA_SCOPE_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_SCOPE_READ(view, keep - 1, &name, &ns);
        if (status != LXA_OK) return status;
        if (ns != LXA_DOM_NS_HTML || !lxa_html_tree_implied_end_tag(name)
            || (except.length != 0
                && lxa_html_tree_name_equals(name, (const char *) except.data,
                                             except.length)))
            break;
        --keep;
    }
    *out = keep;
    return LXA_OK;
}

static inline lxa_status_t
LXA_RUBY_PLAN_NAME(const LXA_SCOPE_VIEW *view, size_t boundary,
                   bool except_rtc, size_t *out)
{
    static const uint8_t ruby[] = "ruby", rtc[] = "rtc";
    size_t index, keep = LXA_SCOPE_LENGTH(view);
    lxa_status_t status = LXA_SCOPE_NAME(view, boundary,
        (lxa_span_t){ruby, sizeof(ruby) - 1}, LXA_HTML_SCOPE_NORMAL, &index);
    if (status != LXA_OK) return status;
    if (index != keep) {
        lxa_span_t except = except_rtc
            ? (lxa_span_t){rtc, sizeof(rtc) - 1} : (lxa_span_t){NULL, 0};
        status = LXA_IMPLIED_PLAN_NAME(view, index + 1, except, &keep);
        if (status != LXA_OK) return status;
    }
    *out = keep;
    return LXA_OK;
}
