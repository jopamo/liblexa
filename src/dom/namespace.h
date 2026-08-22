#ifndef LIBLEXA_DOM_NAMESPACE_H
#define LIBLEXA_DOM_NAMESPACE_H

/* Parser namespaces are explicit, not inferred from spelling or ancestry. */
typedef enum {
    LXA_DOM_NS_HTML = 1,
    LXA_DOM_NS_MATHML = 2,
    LXA_DOM_NS_SVG = 3
} lxa_dom_namespace_t;

#endif
