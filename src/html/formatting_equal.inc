/* Instantiate formatting equality with static stored-attribute access.
 * LXA_FORMATTING_{EQUAL_NAME,VIEW,CURSOR,DONE,NAME,NEXT,READ,BUDGET,REASON}
 * belong to the includer.
 */
static inline lxa_status_t
LXA_FORMATTING_EQUAL_NAME(const LXA_FORMATTING_VIEW *view,
                          const lxa_html_token_t *token, bool *out)
{
    if (!lxa_html_formatting_token_shape_valid(token))
        return LXA_ERROR_ARGUMENT;
    lxa_span_t name;
    LXA_FORMATTING_CURSOR cursor = (LXA_FORMATTING_CURSOR){0};
    size_t count = 0;
    bool equal = false;
    lxa_status_t status = LXA_FORMATTING_NAME(view, &name);
    if (status != LXA_OK) return status;
    if (name.length != token->name.length
        || memcmp(name.data, token->name.data, name.length) != 0)
        goto publish;
    for (;;) {
        lxa_span_t key, value;
        bool matched = false;
        status = LXA_FORMATTING_NEXT(view, cursor, &cursor);
        if (status != LXA_OK) return status;
        if (LXA_FORMATTING_DONE(cursor)) break;
        if (count == token->attribute_count) goto publish;
        ++count;
        status = lxa_budget_charge(LXA_FORMATTING_BUDGET(view),
            LXA_BUDGET_WORK, 1, LXA_FORMATTING_REASON(view));
        if (status != LXA_OK) return status;
        status = LXA_FORMATTING_READ(view, cursor, &key, &value);
        if (status != LXA_OK) return status;
        for (size_t i = 0; i < token->attribute_count; ++i) {
            const lxa_html_attribute_t *candidate = &token->attributes[i];
            status = lxa_budget_charge(LXA_FORMATTING_BUDGET(view),
                LXA_BUDGET_WORK, 1, LXA_FORMATTING_REASON(view));
            if (status != LXA_OK) return status;
            if (candidate->name.data == NULL || candidate->name.length == 0
                || (candidate->value.data == NULL && candidate->value.length != 0))
                return LXA_ERROR_ARGUMENT;
            if (key.length != candidate->name.length) continue;
            status = lxa_budget_charge(LXA_FORMATTING_BUDGET(view),
                LXA_BUDGET_WORK, key.length / 8 + (key.length % 8 != 0),
                LXA_FORMATTING_REASON(view));
            if (status != LXA_OK) return status;
            if (memcmp(key.data, candidate->name.data, key.length) != 0)
                continue;
            if (value.length != candidate->value.length) goto publish;
            status = lxa_budget_charge(LXA_FORMATTING_BUDGET(view),
                LXA_BUDGET_WORK, value.length / 8 + (value.length % 8 != 0),
                LXA_FORMATTING_REASON(view));
            if (status != LXA_OK) return status;
            if (value.length != 0
                && memcmp(value.data, candidate->value.data, value.length) != 0)
                goto publish;
            matched = true;
            break;
        }
        if (!matched) goto publish;
    }
    equal = count == token->attribute_count;
publish:
    *out = equal;
    return LXA_OK;
}
