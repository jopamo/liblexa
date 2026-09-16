static lxa_status_t
LXA_ROOT_FINISH_NAME(LXA_ROOT_FINISH_VIEW *view, lxa_html_root_mode_t *mode)
{
    bool frameset = lxa_html_tree_frameset_mode(*mode);
    lxa_status_t status = frameset ? LXA_OK : LXA_ROOT_FINISH_BODY(view);
    if (status == LXA_OK) status = LXA_ROOT_FINISH_POP(view);
    if (status == LXA_OK) *mode = LXA_TREE_DONE;
    return status;
}
