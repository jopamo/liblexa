static lxa_status_t
LXA_RELATION_NAME(lxa_dom_nodes_t *nodes, lxa_dom_ref_t source,
    lxa_dom_ref_t *out LXA_RELATION_PARAMETERS)
{
    LXA_RELATION_SETUP
    lxa_dom_record_t record;
    lxa_id_t id, source_id;
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
        case LXA_REL_FIRST: id = record.first_child; break;
        case LXA_REL_NEXT: id = record.next; break;
        case LXA_REL_LAST: id = record.last_child; break;
        case LXA_REL_PREV: id = record.prev; break;
        case LXA_REL_PARENT: id = record.parent; break;
        default: return LXA_ERROR_ARGUMENT;
    }
    if ((relation == LXA_REL_FIRST || relation == LXA_REL_LAST)
        && record.kind != LXA_DOM_KIND_DOCUMENT
        && record.kind != LXA_DOM_KIND_ELEMENT
        && record.kind != LXA_DOM_KIND_DOCUMENT_FRAGMENT) {
        return LXA_ERROR_ARGUMENT;
    }
    if ((relation == LXA_REL_FIRST || relation == LXA_REL_LAST)
        && (record.first_child == LXA_ID_NONE)
            != (record.last_child == LXA_ID_NONE)) {
        return LXA_ERROR_ARGUMENT;
    }
    if (id == LXA_ID_NONE) {
        return LXA_OK;
    }
    status = lxa_budget_charge_work(nodes->budget, 1,
        &nodes->limit_reason);
    if (status != LXA_OK) {
        return status;
    }
    if (id < nodes->retired || id >= nodes->records.length) {
        return LXA_ERROR_ARGUMENT;
    }
    source_id = (lxa_id_t) source.handle;
    if (lxa_dom_record_at(nodes, id)->generation != nodes->generation) {
        return LXA_ERROR_ARGUMENT;
    }
    if ((relation == LXA_REL_FIRST
            && (lxa_dom_record_at(nodes, id)->parent != source_id
                || lxa_dom_record_at(nodes, id)->prev != LXA_ID_NONE))
        || (relation == LXA_REL_LAST
            && (lxa_dom_record_at(nodes, id)->parent != source_id
                || lxa_dom_record_at(nodes, id)->next != LXA_ID_NONE))
        || (relation == LXA_REL_NEXT
            && (record.parent == LXA_ID_NONE
                || lxa_dom_record_at(nodes, id)->parent != record.parent
                || lxa_dom_record_at(nodes, id)->prev != source_id))
        || (relation == LXA_REL_PREV
            && (record.parent == LXA_ID_NONE
                || lxa_dom_record_at(nodes, id)->parent != record.parent
                || lxa_dom_record_at(nodes, id)->next != source_id))
        || (relation == LXA_REL_PARENT
            && (record.prev == LXA_ID_NONE
                    ? lxa_dom_record_at(nodes, id)->first_child != source_id
                    : (record.prev < nodes->retired
                          || record.prev >= nodes->records.length
                          || lxa_dom_record_at(nodes, record.prev)->next != source_id
                          || lxa_dom_record_at(nodes, record.prev)->parent != id)))) {
        return LXA_ERROR_ARGUMENT;
    }
    out->document = nodes;
    out->epoch = nodes->epoch;
    LXA_RELATION_FINISH;
}
