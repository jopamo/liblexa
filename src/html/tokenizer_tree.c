#include "html/tokenizer_internal.h"
#include "html/tokenizer_tree.h"
#include "html/tree.h"

#define LXA_HTML_ENGINE_NAME(name) lxa_html_tree_tokenizer_##name
#define LXA_HTML_ENGINE_ACCEPTS(tokenizer) \
    ((tokenizer) == NULL || ((tokenizer)->on_token == lxa_html_tree_on_token && (tokenizer)->on_error == NULL))
#define LXA_HTML_EMIT_TOKEN(tokenizer, token) \
    lxa_html_tree_on_token((tokenizer)->context, tokenizer, token)
#define LXA_HTML_HAS_DIAGNOSTICS(tokenizer) false
#define LXA_HTML_EMIT_DIAGNOSTIC(tokenizer, code, offset) LXA_OK
#define LXA_HTML_BORROW_TREE_INPUT 1
#include "html/tokenizer_engine.inc"
#undef LXA_HTML_BORROW_TREE_INPUT
#undef LXA_HTML_EMIT_DIAGNOSTIC
#undef LXA_HTML_HAS_DIAGNOSTICS
#undef LXA_HTML_EMIT_TOKEN
#undef LXA_HTML_ENGINE_ACCEPTS
#undef LXA_HTML_ENGINE_NAME
