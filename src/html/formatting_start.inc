/* Accepted formatting-start preprocessing. Queries and effects own WORK.
 * LXA_FORMATTING_START_{NAME,VIEW,REFERENCE,FIND,IDENTITY,ADOPT,FIND_REFERENCE,
 * FORMATTING_LENGTH,REMOVE_FORMATTING,OPEN,OPEN_LENGTH,REMOVE_OPEN,RECONSTRUCT,
 * SCOPED,PLAN} belong to the includer.
 */
static inline lxa_status_t
LXA_FORMATTING_START_NAME(LXA_FORMATTING_START_VIEW *view,
    const lxa_html_token_t *token, size_t *drop)
{
    lxa_status_t status;
    size_t index;
    if (lxa_html_tree_name_equals(token->name, "a", 1)) {
        status = LXA_FORMATTING_START_FIND(view, token->name, &index);
        if (status != LXA_OK) return status;
        if (index != LXA_FORMATTING_START_FORMATTING_LENGTH(view)) {
            LXA_FORMATTING_START_REFERENCE old;
            status = LXA_FORMATTING_START_IDENTITY(view, index, &old);
            if (status == LXA_OK) status = LXA_FORMATTING_START_ADOPT(view, token->name);
            if (status == LXA_OK) status = LXA_FORMATTING_START_FIND_REFERENCE(view, old, &index);
            if (status != LXA_OK) return status;
            if (index != LXA_FORMATTING_START_FORMATTING_LENGTH(view)) {
                status = LXA_FORMATTING_START_REMOVE_FORMATTING(view, index);
                if (status != LXA_OK) return status;
            }
            status = LXA_FORMATTING_START_OPEN(view, old, &index);
            if (status != LXA_OK) return status;
            if (index != LXA_FORMATTING_START_OPEN_LENGTH(view)) {
                status = LXA_FORMATTING_START_REMOVE_OPEN(view, index);
                if (status != LXA_OK) return status;
            }
        }
    }
    if (lxa_html_tree_name_equals(token->name, "nobr", 4)) {
        status = LXA_FORMATTING_START_RECONSTRUCT(view);
        if (status == LXA_OK) status = LXA_FORMATTING_START_SCOPED(view, token->name, &index);
        if (status == LXA_OK && index != LXA_FORMATTING_START_OPEN_LENGTH(view)) {
            status = LXA_FORMATTING_START_ADOPT(view, token->name);
            if (status == LXA_OK) status = LXA_FORMATTING_START_RECONSTRUCT(view);
        }
        return status == LXA_OK ? LXA_FORMATTING_START_PLAN(view, token, drop) : status;
    }
    status = LXA_FORMATTING_START_PLAN(view, token, drop);
    if (status == LXA_OK) status = LXA_FORMATTING_START_RECONSTRUCT(view);
    return status;
}
