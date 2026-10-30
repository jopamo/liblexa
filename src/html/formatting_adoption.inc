/* Instantiate adoption execution with owner-specific effects.
 * LXA_ADOPTION_{NAME,VIEW,REFERENCE,SAME,LENGTH,BOUNDARY,FORMATTING_LENGTH,
 * READ,IDENTITY,OPEN_IDENTITY,FIND,
 * PLAN,ORDINARY,POP,REMOVE_FORMATTING,REMOVE_OPEN,BEGIN,INNER,FINAL,COMMIT,INTERNAL}
 * belong to the includer. Owner operations retain safely destructible state
 * on partial failure; callers abort the operation rather than replay effects.
 */
static lxa_status_t
LXA_ADOPTION_NAME(LXA_ADOPTION_VIEW *view, lxa_span_t subject)
{
    lxa_status_t status;
    if (LXA_ADOPTION_LENGTH(view) > LXA_ADOPTION_BOUNDARY(view)) {
        lxa_span_t name;
        lxa_dom_namespace_t ns;
        LXA_ADOPTION_REFERENCE current;
        size_t index;
        status = LXA_ADOPTION_READ(view, LXA_ADOPTION_LENGTH(view) - 1,
            &name, &ns, &current);
        if (status != LXA_OK) return status;
        if (ns == LXA_DOM_NS_HTML && name.length == subject.length
            && memcmp(name.data, subject.data, name.length) == 0) {
            status = LXA_ADOPTION_FIND(view, current, &index);
            if (status != LXA_OK) return status;
            if (index == LXA_ADOPTION_FORMATTING_LENGTH(view))
                return LXA_ADOPTION_POP(view, LXA_ADOPTION_LENGTH(view) - 1);
        }
    }
    for (unsigned outer = 0; outer < 8; ++outer) {
        lxa_html_formatting_adopt_plan_t plan;
        status = LXA_ADOPTION_PLAN(view, subject, &plan);
        if (status != LXA_OK) return status;
        switch (plan.action) {
            case LXA_HTML_FORMATTING_ADOPT_ORDINARY:
                return LXA_ADOPTION_ORDINARY(view, subject);
            case LXA_HTML_FORMATTING_ADOPT_REMOVE_CLOSED:
                return LXA_ADOPTION_REMOVE_FORMATTING(view, plan.formatting);
            case LXA_HTML_FORMATTING_ADOPT_IGNORE:
                return LXA_OK;
            case LXA_HTML_FORMATTING_ADOPT_POP:
                status = LXA_ADOPTION_POP(view, plan.open);
                return status == LXA_OK
                    ? LXA_ADOPTION_REMOVE_FORMATTING(view, plan.formatting)
                    : status;
            case LXA_HTML_FORMATTING_ADOPT_REPAIR:
                break;
            default:
                return LXA_ADOPTION_INTERNAL(view);
        }
        LXA_ADOPTION_REFERENCE formatting, furthest, last, clone;
        size_t formatting_index = plan.formatting, furthest_index = plan.furthest;
        size_t bookmark = formatting_index, node = furthest_index;
        status = LXA_ADOPTION_IDENTITY(view, formatting_index, &formatting);
        if (status == LXA_OK) status = LXA_ADOPTION_BEGIN(view, plan.open);
        if (status != LXA_OK) return status;
        status = LXA_ADOPTION_OPEN_IDENTITY(view, furthest_index, &furthest);
        if (status != LXA_OK) return status;
        last = furthest;
        for (size_t inner = 1;; ++inner) {
            LXA_ADOPTION_REFERENCE ref;
            size_t entry;
            if (node <= plan.open) return LXA_ADOPTION_INTERNAL(view);
            status = LXA_ADOPTION_OPEN_IDENTITY(view, --node, &ref);
            if (status != LXA_OK) return status;
            if (LXA_ADOPTION_SAME(ref, formatting)) break;
            status = LXA_ADOPTION_FIND(view, ref, &entry);
            if (status != LXA_OK) return status;
            if (inner > 3 && entry != LXA_ADOPTION_FORMATTING_LENGTH(view)) {
                status = LXA_ADOPTION_REMOVE_FORMATTING(view, entry);
                if (status != LXA_OK) return status;
                if (entry < bookmark) --bookmark;
                entry = LXA_ADOPTION_FORMATTING_LENGTH(view);
            }
            if (entry == LXA_ADOPTION_FORMATTING_LENGTH(view)) {
                status = LXA_ADOPTION_REMOVE_OPEN(view, node);
                if (status != LXA_OK) return status;
                --furthest_index;
                continue;
            }
            status = LXA_ADOPTION_INNER(view, entry, node, last, &clone);
            if (status != LXA_OK) return status;
            if (LXA_ADOPTION_SAME(last, furthest)) bookmark = entry + 1;
            last = clone;
        }
        status = LXA_ADOPTION_FINAL(view, last, formatting, furthest, &clone);
        if (status != LXA_OK) return status;
        status = LXA_ADOPTION_FIND(view, formatting, &formatting_index);
        if (status != LXA_OK) return status;
        if (formatting_index == LXA_ADOPTION_FORMATTING_LENGTH(view))
            return LXA_ADOPTION_INTERNAL(view);
        if (formatting_index < bookmark) --bookmark;
        status = LXA_ADOPTION_COMMIT(view, clone, formatting_index, bookmark,
            plan.open, furthest_index);
        if (status != LXA_OK) return status;
    }
    return LXA_OK;
}
