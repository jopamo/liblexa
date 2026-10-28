# Liblexa

HTML and CSS parsing for C programs that don't need to become a browser.

Liblexa provides the machinery to read a stylesheet, inspect a recovered HTML
tree, or extract and rewrite HTML attributes. Your application keeps control of
allocation, resource limits, and what happens with the result. No JavaScript
engine comes along for the ride.

## What you can do with it

**Read CSS syntax.** Tokenize UTF-8 input, walk balanced blocks and functions,
and parse rules, declarations, and selector ranges. Tokens retain original byte
offsets alongside decoded text. This is syntax machinery, not a cascade,
selector matcher, or property validator.

**Build an HTML tree.** Parse a complete document with scripting disabled,
then use checked node references to traverse elements, attributes, and text.
Tree construction handles malformed markup, foreign content, and templates.
The HTML library also provides document serialization.

**Leave the tree out of it.** Extract selected attributes in source order or
rewrite their values without constructing a DOM. Rewriting preserves bytes
outside replaced attribute spans; a keep-only pass reproduces the original
input. That's useful when changing a link should not also redecorate the page.
URL resolution and fetching remain your application's job.

The public interfaces live in [`include/liblexa/`](include/liblexa/):

| Header | Purpose |
| --- | --- |
| [`css.h`](include/liblexa/css.h) | CSS tokens, blocks, and syntax helpers |
| [`html.h`](include/liblexa/html.h) | Complete-document HTML parsing |
| [`dom.h`](include/liblexa/dom.h) | Node storage and checked traversal |
| [`html_source.h`](include/liblexa/html_source.h) | Source attribute extraction |
| [`html_rewrite.h`](include/liblexa/html_rewrite.h) | Source attribute replacement |
| [`serialize.h`](include/liblexa/serialize.h) | HTML output and escaping |
| [`base.h`](include/liblexa/base.h) | Allocators, buffers, statuses, and budgets |

## Know the boundaries

Liblexa is a library, not a browser or an HTML-cleaning policy. It does not
render pages, run scripts, load resources, or decide which URLs and CSS
properties an application should accept.

Resource budgets cover memory, input, work, and parser-specific limits.
Allocation failures, exceeded limits, and unsupported input have distinct
results. Ownership and borrowed-view lifetimes are documented in the headers.
Callbacks and output sinks can fail after producing effects, so callers that
need all-or-nothing output must stage it themselves.

**HTML is still a candidate implementation.** It ships as
`liblexa-html-candidate`, separately from the core/CSS library. Qualification
and performance work remain open; the candidate name is intentional.
The public HTML parser accepts complete documents, not incremental feeds or
fragment contexts. Do not read the feature list as a claim of complete browser
conformance or a settled HTML ABI.

## Build

You need a C17 compiler, Meson 1.1 or newer, and Ninja. Generated tables are
checked in; building the library does not require downloading a parser or
running the table generators.

From this checkout:

```sh
meson setup build --buildtype=release --prefix="$HOME/.local" --libdir=lib
meson compile -C build
meson install -C build
```

The build installs shared and static libraries, headers, and pkg-config files.
For a core/CSS-only build, add `-Dcandidate_html=false` to `meson setup`.

Link a CSS consumer with `liblexa`, or an HTML consumer with
`liblexa-html-candidate`:

```sh
export PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
cc app.c -o app $(pkg-config --cflags --libs liblexa-html-candidate)
```

For shared libraries installed in a private prefix, also configure your
platform's runtime library search path.

## License

Liblexa's authored code is MIT licensed. Generated standards and Unicode data
carry their own notices, included in [`LICENSE`](LICENSE).
