#ifndef LIBLEXA_HTML_TEMPLATE_CONTEXT_H
#define LIBLEXA_HTML_TEMPLATE_CONTEXT_H

#include "html/formatting_list.h"

typedef struct {
    size_t boundary; /* first content entry, after the template owner */
    uint64_t identity;
    lxa_html_template_mode_t insertion_mode;
} lxa_html_template_control_frame_t;

typedef struct {
    lxa_vector_t frames; /* engines update only the active insertion_mode */
    lxa_html_control_stack_t *stack;
    lxa_html_formatting_list_t *formatting;
    size_t root_length;
    lxa_html_root_mode_t return_mode;
    lxa_status_t failure;
    lxa_limit_reason_t limit_reason;
} lxa_html_template_context_t;

/* Single-thread-confined owner of unpatched virtual-template frames. Borrowed
 * stack, formatting list, allocator context and budget outlive destroy.
 * Stack template entries own OPEN_ELEMENTS credits; list markers own ACTIVE
 * charges. Destroy frees only frames, then callers destroy list/stack owners.
 * Engines must not remove/move retained entries across frame boundaries.
 */
lxa_status_t lxa_html_template_context_init(lxa_html_template_context_t *context,
    lxa_html_control_stack_t *stack, lxa_html_formatting_list_t *formatting);
void lxa_html_template_context_destroy(lxa_html_template_context_t *context);

/* Admit an accepted template after namespace/recovery dispatch. Patched
 * contexts are unsupported and rejected before effects. Reserve frames before
 * marker/stack admission, publish only on success. Failures after validation
 * latch the context; retained capacity/WORK and completed effects belong to
 * the borrowed owners until destruction. Engines publish TEMPLATE_TEXT.
 */
lxa_status_t lxa_html_template_context_enter(lxa_html_template_context_t *context,
    lxa_html_root_mode_t mode, bool has_patch);
/* Close the active unpatched frame: plan, pop content, clear the newest marker,
 * pop its template owner, then publish the enclosing mode. EOF skips planning
 * scans. Failure preserves output and latches any completed effects. Output
 * storage must not alias context or borrowed owners.
 */
lxa_status_t lxa_html_template_context_leave(lxa_html_template_context_t *context,
    bool explicit_end, lxa_html_root_mode_t *mode);

#endif
