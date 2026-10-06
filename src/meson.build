# An instrumented shared object resolves the compiler sanitizer runtime in
# its final executable.
shared_overrides = []
if get_option('b_sanitize') != 'none'
  shared_overrides = ['b_lundef=false']
endif
lxa_library = both_libraries('lexa', css_consumer_sources,
  link_whole: [lxa_core, lxa_encoding, lxa_unicode, lxa_dom],
  include_directories: lxa_public_inc,
  dependencies: lxa_entropy_platform_dep + css_math_deps,
  override_options: shared_overrides, install: true)

pkg = import('pkgconfig')
pkg.generate(lxa_library,
  filebase: 'liblexa',
  name: 'liblexa',
  description: 'Bounded core for the consumer-scoped HTML/CSS parser',
  version: meson.project_version())

# HTML keeps its candidate ABI name while its consumed grammar is qualified.
# This target is independently installable.
if candidate_html_enabled
  html_generated_inc = include_directories('../generated/html')
  html_candidate_sources = files(
    'html/tokenizer.c', 'html/tokenizer_tree.c', 'html/input_encoding.c',
    'html/foreign_names.c',
    'html/tree.c', 'html/tree_policy.c', 'html/control_stack.c', 'html/root_context.c',
    'html/template_context.c', 'html/control_context.c',
    'html/formatting_store.c', 'html/document.c', 'html/extract.c', 'html/rewrite.c')
  html_candidate_sources += files('serialize/escape.c',
    'serialize/document.c')
  lxa_html_candidate = both_libraries('lexa-html-candidate',
    html_candidate_sources,
    include_directories: [lxa_public_inc, html_generated_inc],
    link_with: lxa_library,
    override_options: shared_overrides, install: true)
  pkg.generate(lxa_html_candidate,
    filebase: 'liblexa-html-candidate',
    name: 'liblexa HTML candidate',
    description: 'Consumer-scoped HTML parser',
    version: meson.project_version(), requires: 'liblexa')
endif
