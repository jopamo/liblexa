/* Root construction order; owners supply creation, open admission and slots.
 * LXA_ROOT_ENSURE_{NAME,VIEW,HAS,CREATE,OPEN_LENGTH,OPEN_BODY,FRAMESET} belong
 * to the includer.
 */
static inline lxa_status_t
LXA_ROOT_ENSURE_NAME(html)(LXA_ROOT_ENSURE_VIEW *view)
{
    return LXA_ROOT_ENSURE_HAS(view, HTML) ? LXA_OK
                                           : LXA_ROOT_ENSURE_CREATE(view, LXA_HTML_ROOT_ELEMENT_HTML);
}

static inline lxa_status_t
LXA_ROOT_ENSURE_NAME(head)(LXA_ROOT_ENSURE_VIEW *view)
{
    lxa_status_t status = LXA_ROOT_ENSURE_NAME(html)(view);
    return status != LXA_OK || LXA_ROOT_ENSURE_HAS(view, HEAD) ? status
                                                               : LXA_ROOT_ENSURE_CREATE(view, LXA_HTML_ROOT_ELEMENT_HEAD);
}

static inline lxa_status_t
LXA_ROOT_ENSURE_NAME(body)(LXA_ROOT_ENSURE_VIEW *view)
{
    bool implicit = !LXA_ROOT_ENSURE_HAS(view, BODY);
    lxa_status_t status = LXA_ROOT_ENSURE_NAME(head)(view);
    if (status == LXA_OK && !LXA_ROOT_ENSURE_HAS(view, BODY))
        status = LXA_ROOT_ENSURE_CREATE(view, LXA_HTML_ROOT_ELEMENT_BODY);
    if (status == LXA_OK && implicit) LXA_ROOT_ENSURE_FRAMESET(view);
    return status != LXA_OK || LXA_ROOT_ENSURE_OPEN_LENGTH(view) != 0 ? status
                                                                      : LXA_ROOT_ENSURE_OPEN_BODY(view);
}
