static lxa_status_t
LXA_RELATION_NAME(lxa_dom_nodes_t *nodes, lxa_dom_ref_t source,
    lxa_dom_ref_t *out LXA_RELATION_PARAMETERS)
{
    LXA_RELATION_SETUP
    lxa_dom_record_t record;
    lxa_id_t id;
    const lxa_dom_record_t *related_record;
    lxa_status_t status;
    if (nodes != NULL) {
        nodes->limit_reason = LXA_BUDGET_COUNT;
    }
    if (out == NULL) {
        return LXA_ERROR_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    status = lxa_dom_nodes_read(nodes, source, &record);
    if (status != LXA_OK) {
        return status;
    }
    switch (relation) {
        case LXA_REL_FIRST: related_record = record.first_child; break;
        case LXA_REL_NEXT: related_record = record.next; break;
        case LXA_REL_LAST: related_record = record.last_child; break;
        case LXA_REL_PREV: related_record = record.prev; break;
        case LXA_REL_PARENT: related_record = record.parent; break;
        default: return LXA_ERROR_ARGUMENT;
    }
    if ((relation == LXA_REL_FIRST || relation == LXA_REL_LAST)
        && record.kind != LXA_DOM_KIND_DOCUMENT
        && record.kind != LXA_DOM_KIND_ELEMENT
        && record.kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT) {
        return LXA_ERROR_ARGUMENT;
    }
    if ((relation == LXA_REL_FIRST || relation == LXA_REL_LAST)
        && (record.first_child == NULL)
            != (record.last_child == NULL)) {
        return LXA_ERROR_ARGUMENT;
    }
    if (related_record == NULL) {
        return LXA_OK;
    }
    status = lxa_budget_charge_work(nodes->budget, 1,
        &nodes->limit_reason);
    if (status != LXA_OK) {
        return status;
    }
    status = lxa_dom_record_identity(nodes, related_record, &id);
    if (status != LXA_OK) return status;
    const lxa_dom_record_t *source_record = lxa_dom_record_at(nodes, (lxa_id_t) source.handle);
    if (related_record->generation != nodes->generation) {
        return LXA_ERROR_ARGUMENT;
    }
    if ((relation == LXA_REL_FIRST
            && (related_record->parent != source_record
                || related_record->prev != NULL))
        || (relation == LXA_REL_LAST
            && (related_record->parent != source_record
                || related_record->next != NULL))
        || (relation == LXA_REL_NEXT
            && (record.parent == NULL
                || related_record->parent != record.parent
                || related_record->prev != source_record))
        || (relation == LXA_REL_PREV
            && (record.parent == NULL
                || related_record->parent != record.parent
                || related_record->next != source_record))
        || (relation == LXA_REL_PARENT
            && (record.prev == NULL
                    ? related_record->first_child != source_record
                    : (!record_is_active(nodes, record.prev)
                          || record.prev->next != source_record
                          || record.prev->parent != related_record)))) {
        return LXA_ERROR_ARGUMENT;
    }
    out->document = nodes;
    out->epoch = nodes->epoch;
    LXA_RELATION_FINISH;
}
