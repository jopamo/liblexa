#include "html/source_consumer.h"
#include "html/control_context.h"
#include <string.h>

typedef struct {
    lxa_html_control_context_t control;
    lxa_budget_t *budget;
    lxa_vector_t mapping;
    const lxa_html_source_target_t *targets;
    size_t count, input_length;
    lxa_html_source_attribute_callback_t callback;
    void *user;
    const lxa_encoding_t *encoding;
    bool include_templates, callback_failed;
    lxa_limit_reason_t reason;
} extraction_t;

lxa_html_result_t lxa_html_source_result(lxa_status_t status)
{
    switch (status) {
    case LXA_OK:
        return LXA_HTML_OK;
    case LXA_ERROR_ARGUMENT:
        return LXA_HTML_BAD_ARGUMENT;
    case LXA_ERROR_OVERFLOW:
        return LXA_HTML_OVERFLOW;
    case LXA_ERROR_NO_MEMORY:
        return LXA_HTML_NO_MEMORY;
    case LXA_ERROR_LIMIT:
        return LXA_HTML_LIMIT;
    case LXA_ERROR_ENTROPY:
        return LXA_HTML_ENTROPY;
    case LXA_ERROR_UNSUPPORTED:
        return LXA_HTML_UNSUPPORTED;
    default:
        return LXA_HTML_INTERNAL;
    }
}

static lxa_status_t token_status(lxa_html_result_t result,
                                 lxa_html_tokenizer_t *tokenizer)
{
    switch (result) {
    case LXA_HTML_OK:
        return LXA_OK;
    case LXA_HTML_BAD_ARGUMENT:
        return LXA_ERROR_ARGUMENT;
    case LXA_HTML_OVERFLOW:
        return LXA_ERROR_OVERFLOW;
    case LXA_HTML_NO_MEMORY:
        return LXA_ERROR_NO_MEMORY;
    case LXA_HTML_LIMIT:
        return LXA_ERROR_LIMIT;
    case LXA_HTML_ENTROPY:
        return LXA_ERROR_ENTROPY;
    case LXA_HTML_UNSUPPORTED:
        return LXA_ERROR_UNSUPPORTED;
    case LXA_HTML_CALLBACK_FAILURE:
        return lxa_html_tokenizer_callback_status(tokenizer);
    default:
        return LXA_ERROR_INTERNAL;
    }
}

static lxa_status_t name_check(lxa_span_t name, bool empty,
                               const lxa_allocator_t *allocator, extraction_t *owner,
                               lxa_decoder_t **decoder)
{
    bool unicode = false;
    lxa_status_t status;
    if ((name.data == NULL && name.length != 0) || (!empty && name.length == 0))
        return LXA_ERROR_ARGUMENT;
    status = lxa_budget_check_item(owner->budget, LXA_BUDGET_NAME_BYTES, name.length,
                                   &owner->reason);
    if (status == LXA_OK)
        status = lxa_budget_charge(owner->budget, LXA_BUDGET_WORK, name.length,
                                   &owner->reason);
    if (status != LXA_OK)
        return status;
    for (size_t i = 0; i < name.length; i++) {
        uint8_t c = name.data[i];
        if (c <= 0x20 || c == 0x7f || (c >= 'A' && c <= 'Z'))
            return LXA_ERROR_ARGUMENT;
        unicode |= c >= 0x80;
    }
    if (!unicode)
        return LXA_OK;
    status =
        lxa_budget_charge(owner->budget, LXA_BUDGET_WORK, name.length, &owner->reason);
    if (status != LXA_OK)
        return status;
    if (*decoder == NULL) {
        const lxa_encoding_t *utf8 =
            lxa_encoding_lookup((lxa_span_t){(const uint8_t *)"utf-8", 5});
        status = lxa_decoder_create_with_budget(utf8, LXA_ENCODING_FATAL, allocator,
                                                owner->budget, decoder);
        if (status == LXA_ERROR_LIMIT)
            owner->reason = LXA_BUDGET_MEMORY;
        if (status != LXA_OK)
            return status;
    }
    lxa_decoder_reset(*decoder);
    while (name.length != 0) {
        uint32_t scalars[32];
        lxa_encoding_progress_t progress;
        lxa_encoding_result_t result =
            lxa_decoder_feed(*decoder, name, scalars, 32, &progress);
        if (result == LXA_ENCODING_MALFORMED)
            return LXA_ERROR_ARGUMENT;
        if (result != LXA_ENCODING_OK && result != LXA_ENCODING_OUTPUT_FULL)
            return LXA_ERROR_INTERNAL;
        if (progress.consumed == 0 && progress.produced == 0)
            return LXA_ERROR_INTERNAL;
        name.data += progress.consumed;
        name.length -= progress.consumed;
    }
    uint32_t scalar;
    lxa_encoding_progress_t progress;
    lxa_encoding_result_t result = lxa_decoder_finish(*decoder, &scalar, 1, &progress);
    return result == LXA_ENCODING_OK          ? LXA_OK
           : result == LXA_ENCODING_MALFORMED ? LXA_ERROR_ARGUMENT
                                              : LXA_ERROR_INTERNAL;
}

static lxa_status_t target_index(extraction_t *owner, lxa_span_t tag,
                                 lxa_span_t name, size_t *index)
{
    *index = SIZE_MAX;
    for (size_t t = 0; t < owner->count; t++) {
        const lxa_html_source_target_t *target = &owner->targets[t];
        lxa_status_t status =
            lxa_budget_charge(owner->budget, LXA_BUDGET_WORK, 1, &owner->reason);
        if (status != LXA_OK)
            return status;
        if (target->attribute.length != name.length ||
            (target->tag.length != 0 && target->tag.length != tag.length))
            continue;
        size_t work;
        status = lxa_size_add(target->attribute.length, target->tag.length, &work);
        if (status == LXA_OK)
            status = lxa_budget_charge(owner->budget, LXA_BUDGET_WORK, work, &owner->reason);
        if (status != LXA_OK)
            return status;
        if (memcmp(target->attribute.data, name.data, name.length) == 0 &&
            (target->tag.length == 0 || memcmp(target->tag.data, tag.data, tag.length) == 0)) {
            *index = t;
            break;
        }
    }
    return LXA_OK;
}

static lxa_status_t retain_value(void *context, lxa_span_t tag,
                                 lxa_span_t name, bool *retain)
{
    extraction_t *owner = context;
    *retain = lxa_html_tree_attribute_value_required(tag, name, false);
    if (*retain)
        return LXA_OK;
    size_t index;
    lxa_status_t status = target_index(owner, tag, name, &index);
    *retain = index != SIZE_MAX;
    return status;
}

static lxa_status_t observe(void *context, lxa_html_tokenizer_t *tokenizer,
                            const lxa_html_token_t *token)
{
    extraction_t *owner = context;
    lxa_status_t status;
    if (token->kind == LXA_HTML_START_TAG && owner->count != 0 &&
        (owner->include_templates || owner->control.templates.frames.bytes.length == 0)) {
        for (size_t a = 0; a < token->attribute_count; a++) {
            const lxa_html_attribute_t *attribute = &token->attributes[a];
            size_t t;
            status = target_index(owner, token->name, attribute->name, &t);
            if (status != LXA_OK)
                return status;
            if (t != SIZE_MAX) {
                if (token->attribute_sources == NULL)
                    return LXA_ERROR_INTERNAL;
                lxa_html_source_attribute_t event = {.tag = token->name,
                                                     .name = attribute->name,
                                                     .value = attribute->value,
                                                     .target_index = t,
                                                     .encoding = owner->encoding};
                size_t mapping_work = 3;
                for (size_t n =
                         owner->mapping.bytes.length / sizeof(lxa_html_input_mapping_t);
                     n != 0; n >>= 1)
                    mapping_work += 3;
                status = lxa_budget_charge(owner->budget, LXA_BUDGET_WORK, mapping_work,
                                           &owner->reason);
                if (status != LXA_OK)
                    return status;
                status = lxa_html_input_raw_offset(&owner->mapping,
                                                   token->attribute_sources[a].start,
                                                   &event.source_start);
                if (status == LXA_OK)
                    status = lxa_html_input_raw_offset(&owner->mapping,
                                                       token->attribute_sources[a].name_end,
                                                       &event.source_name_end);
                if (status == LXA_OK)
                    status = lxa_html_input_raw_offset(&owner->mapping,
                                                       token->attribute_sources[a].end,
                                                       &event.source_end);
                if (status != LXA_OK || event.source_start >= event.source_name_end ||
                    event.source_name_end > event.source_end ||
                    event.source_end > owner->input_length)
                    return LXA_ERROR_INTERNAL;
                status = owner->callback(owner->user, &event);
                if (status != LXA_OK) {
                    owner->callback_failed = true;
                    owner->reason = LXA_BUDGET_COUNT;
                    return status;
                }
            }
        }
    }
    status = lxa_html_control_context_on_token(&owner->control, tokenizer, token, false,
                                               NULL, NULL);
    if (status == LXA_ERROR_LIMIT)
        owner->reason = owner->control.limit_reason;
    return status;
}

lxa_status_t
lxa_html_extract_attributes_with_budget(const lxa_allocator_t *allocator, lxa_budget_t *budget,
                            lxa_span_t input, const lxa_html_extract_options_t *options,
                            const lxa_html_source_target_t *targets, size_t count,
                            lxa_html_source_attribute_callback_t callback, void *context,
                            lxa_html_result_t *parser_result, lxa_limit_reason_t *reason)
{
    extraction_t owner = {.budget = budget,
                          .targets = targets,
                          .count = count,
                          .input_length = input.length,
                          .callback = callback,
                          .user = context,
                          .include_templates =
                              options != NULL && options->include_templates,
                          .reason = LXA_BUDGET_COUNT};
    lxa_buffer_t decoded = {0};
    lxa_decoder_t *name_decoder = NULL;
    lxa_html_tokenizer_t *tokenizer = NULL;
    lxa_html_result_t result = LXA_HTML_BAD_ARGUMENT;
    lxa_status_t status = LXA_ERROR_ARGUMENT;
    if (parser_result != NULL)
        *parser_result = result;
    if (reason != NULL)
        *reason = LXA_BUDGET_COUNT;
    if (allocator == NULL || allocator->allocate == NULL || allocator->resize == NULL ||
        allocator->release == NULL || budget == NULL ||
        budget->used[LXA_BUDGET_INPUT] != 0 || budget->used[LXA_BUDGET_NODES] != 0 ||
        (input.data == NULL && input.length != 0) ||
        (count != 0 && (targets == NULL || callback == NULL)))
        return status;
    size_t target_bytes;
    status = lxa_size_mul(count, sizeof(*targets), &target_bytes);
    if (status != LXA_OK)
        goto done;
    status = lxa_buffer_init(&decoded, allocator);
    if (status == LXA_OK)
        status = lxa_buffer_bind_budget(&decoded, owner.budget);
    if (status == LXA_OK)
        status =
            lxa_vector_init(&owner.mapping, allocator, sizeof(lxa_html_input_mapping_t));
    if (status == LXA_OK)
        status = lxa_vector_bind_budget(&owner.mapping, owner.budget);
    if (status != LXA_OK)
        goto done;
    lxa_html_control_input_t selection;
    status = lxa_html_control_input_prepare(
        input, options == NULL ? NULL : options->encoding,
        options == NULL ? 0 : options->prefix_length, owner.budget, &decoded,
        &owner.mapping, &selection, &owner.reason);
    if (status != LXA_OK)
        goto done;
    owner.encoding = selection.encoding;
    for (size_t i = 0; i < count; i++) {
        status = lxa_budget_charge(owner.budget, LXA_BUDGET_WORK, 1, &owner.reason);
        if (status == LXA_OK)
            status = name_check(targets[i].tag, true, allocator, &owner, &name_decoder);
        if (status == LXA_OK)
            status =
                name_check(targets[i].attribute, false, allocator, &owner, &name_decoder);
        if (status != LXA_OK)
            goto done;
    }
    lxa_decoder_destroy(name_decoder);
    name_decoder = NULL;
    status = lxa_html_control_context_init(&owner.control, allocator, owner.budget);
    if (status != LXA_OK)
        goto done;
    status = lxa_html_tokenizer_create_with_budget(allocator, observe, NULL, &owner,
                                                   owner.budget, &tokenizer);
    if (status != LXA_OK) {
        if (status == LXA_ERROR_LIMIT)
            owner.reason = LXA_BUDGET_MEMORY;
        goto done;
    }
    result =
        count == 0 ? LXA_HTML_OK : lxa_html_tokenizer_enable_source_ranges(tokenizer);
    if (result == LXA_HTML_OK)
        result = lxa_html_tokenizer_filter_attribute_values(tokenizer, retain_value, &owner);
    if (result == LXA_HTML_OK)
        result = lxa_html_tokenizer_feed_prepaid(tokenizer, selection.utf8);
    if (result == LXA_HTML_OK)
        result = lxa_html_tokenizer_finish(tokenizer);
    status = token_status(result, tokenizer);
    if (result == LXA_HTML_CALLBACK_FAILURE && !owner.callback_failed &&
        (status == LXA_OK || status == LXA_ERROR_ARGUMENT))
        status = LXA_ERROR_INTERNAL;
    if (result == LXA_HTML_LIMIT)
        owner.reason = lxa_html_tokenizer_limit_reason(tokenizer);
    if (status == LXA_OK && (owner.control.mode != LXA_TREE_DONE ||
                             owner.budget->used[LXA_BUDGET_INPUT] != input.length ||
                             owner.budget->used[LXA_BUDGET_NODES] != 0))
        status = LXA_ERROR_INTERNAL;
done:
    lxa_decoder_destroy(name_decoder);
    lxa_html_tokenizer_destroy(tokenizer);
    lxa_html_control_context_destroy(&owner.control);
    lxa_vector_destroy(&owner.mapping);
    lxa_buffer_destroy(&decoded);
    if (parser_result != NULL)
        *parser_result =
            owner.callback_failed ? LXA_HTML_CALLBACK_FAILURE : lxa_html_source_result(status);
    if (reason != NULL)
        *reason = owner.reason;
    return status;
}

lxa_status_t
lxa_html_extract_attributes(const lxa_allocator_t *allocator, const lxa_limits_t *limits,
                            lxa_span_t input, const lxa_html_extract_options_t *options,
                            const lxa_html_source_target_t *targets, size_t count,
                            lxa_html_source_attribute_callback_t callback, void *context,
                            lxa_html_result_t *parser_result, lxa_limit_reason_t *reason)
{
    lxa_budget_t budget;
    lxa_status_t status = lxa_budget_init(&budget, limits);
    if (status != LXA_OK)
        return status;
    return lxa_html_extract_attributes_with_budget(allocator, &budget, input, options,
        targets, count, callback, context, parser_result, reason);
}
