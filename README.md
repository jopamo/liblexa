# Liblexa

CSS and HTML parsing libraries for C17.

Liblexa provides CSS tokenization and syntax parsing, HTML document parsing,
DOM traversal, serialization, and source-level attribute extraction and
rewriting. Callers control allocation, resource limits, and I/O.

## Features

### CSS

- UTF-8 tokenization with original byte offsets and decoded text
- Balanced block and function parsing
- Rule, declaration, and selector-range parsing
- Syntax traversal without constructing a CSS object model

CSS support does not include selector matching, cascading, or property
validation.

### HTML

- Complete-document parsing with scripting disabled
- Tree construction with malformed-markup recovery
- Foreign content and template handling
- Checked node references for element, attribute, and text traversal
- Document serialization

### Source processing

- Attribute extraction in source order without building a DOM
- Attribute value replacement while preserving surrounding source bytes
- Byte-identical output when all attributes are kept unchanged

URL resolution and resource fetching are left to the caller.

## Public API

Headers are located in [`include/liblexa/`](include/liblexa/).

| Header | Purpose |
| --- | --- |
| [`css.h`](include/liblexa/css.h) | CSS tokens, blocks, and syntax parsing |
| [`html.h`](include/liblexa/html.h) | HTML document parsing |
| [`dom.h`](include/liblexa/dom.h) | Node storage and checked traversal |
| [`html_source.h`](include/liblexa/html_source.h) | Source attribute extraction |
| [`html_rewrite.h`](include/liblexa/html_rewrite.h) | Source attribute replacement |
| [`serialize.h`](include/liblexa/serialize.h) | HTML serialization and escaping |
| [`base.h`](include/liblexa/base.h) | Allocation, buffers, status codes, and resource budgets |

## Resource handling

Liblexa supports memory, input, work, and parser-specific limits.
Allocation failures, exceeded limits, and unsupported input are reported
separately.

Ownership rules and borrowed-view lifetimes are documented in the headers.

Callbacks and output sinks may fail after producing partial output.
Callers requiring atomic output must buffer or stage it before committing.

## Limitations

Liblexa does not render HTML, execute scripts, fetch resources, sanitize
documents, or enforce URL and CSS security policies.

**HTML support is experimental.** It is provided through
`liblexa-html-candidate`, separately from the core/CSS library.

Current HTML limitations:

- Complete documents only; no incremental parsing or fragment contexts
- Browser-level conformance is not guaranteed
- HTML ABI stability is not guaranteed
- Qualification and performance testing are ongoing

## Building

Requirements:

- C17 compiler
- Meson 1.1 or newer
- Ninja

Generated tables are included in the repository. Building does not require
running generators or downloading parser dependencies.

```sh
meson setup build --buildtype=release --prefix="$HOME/.local" --libdir=lib
meson compile -C build
meson install -C build
```

The build installs shared and static libraries, headers, and pkg-config files.

To build only the core/CSS library:

```sh
meson setup build -Dcandidate_html=false --buildtype=release
meson compile -C build
```

### Linking

Use `liblexa` for CSS or `liblexa-html-candidate` for HTML.

For an installation under `$HOME/.local`:

```sh
export PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
cc app.c -o app $(pkg-config --cflags --libs liblexa-html-candidate)
```

Shared libraries installed in a private prefix may also require runtime
library search path configuration.

## License

Liblexa's authored code is MIT licensed. Generated standards and Unicode
data retain their respective notices.

See [LICENSE](LICENSE) for details.
