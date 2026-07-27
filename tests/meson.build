test_support_inc = include_directories('..')
subdir('unit/generators')

foreach name : ['budget', 'bytes', 'entropy', 'memory', 'region',
                'region_random', 'siphash', 'size', 'table', 'vector']
  base_test = executable('liblexa_base_' + name + '_test',
    files('unit/base/' + name + '.c'),
    include_directories: lxa_public_inc, link_with: lxa_library)
  test('base ' + name, base_test)
endforeach

encoding_test = executable('liblexa_encoding_stream_test',
  files('unit/encoding/stream.c'),
  include_directories: lxa_public_inc, link_with: lxa_library)
test('encoding stream', encoding_test)

foreach name : ['bytes', 'components', 'declaration', 'declarations',
                'errors', 'filter', 'import', 'media', 'rule', 'scan', 'selector_key',
                'selector_list', 'token', 'value_text']
  css_test = executable('liblexa_css_' + name + '_test',
    files('unit/css/' + name + '.c'),
    include_directories: lxa_public_inc, link_with: lxa_library)
  test('CSS ' + name, css_test)
endforeach

if get_option('candidate_html')
  html_fuzz_smoke = executable('liblexa_html_fuzz_smoke',
    files('fuzz/html_parse.c'),
    include_directories: lxa_public_inc,
    link_with: [lxa_html_candidate, lxa_library])
  test('HTML bounded parse smoke', html_fuzz_smoke, suite: 'fuzz', timeout: 30)
  tree_dump = executable('liblexa_html_tree_dump',
    files('conformance/html/tree_dump.c'),
    include_directories: lxa_public_inc,
    link_with: [lxa_html_candidate, lxa_library])
  test('HTML pinned trees', find_program('python3'),
    args: [files('conformance/html/tree_cases.py'), tree_dump],
    suite: 'conformance', timeout: 30)
  foreach name : ['tokenizer', 'input_encoding']
    html_unit_test = executable('liblexa_html_' + name + '_test',
      files('unit/html/' + name + '.c'),
      include_directories: lxa_public_inc,
      link_with: [lxa_html_candidate, lxa_library])
    test('HTML ' + name, html_unit_test)
  endforeach
  foreach name : ['document', 'escape']
    serialization_test = executable('liblexa_serialization_' + name + '_test',
      files('unit/serialization/' + name + '_test.c'),
      include_directories: [lxa_public_inc, test_support_inc],
      link_with: [lxa_html_candidate, lxa_library])
    test('serialization ' + name, serialization_test)
  endforeach
  html_candidate_test = executable('liblexa_html_candidate_test',
    files('unit/html/tree_test.c'),
    include_directories: [lxa_public_inc, test_support_inc],
    link_with: [lxa_html_candidate, lxa_library])
  test('HTML candidate', html_candidate_test)
  html_document_test = executable('liblexa_html_document_test',
    files('unit/html/document_test.c'),
    include_directories: [lxa_public_inc, test_support_inc],
    link_with: [lxa_html_candidate, lxa_library])
  test('HTML document candidate', html_document_test)
  html_template_failure_test = executable(
    'liblexa_html_template_failure_test',
    files('unit/html/template_failure_test.c'),
    include_directories: lxa_public_inc,
    link_with: [lxa_html_candidate, lxa_library])
  test('HTML template allocation failures', html_template_failure_test)
  html_foreign_names_test = executable('liblexa_html_foreign_names_test',
    files('unit/html/foreign_names.c'),
    include_directories: [lxa_public_inc, html_generated_inc],
    link_with: [lxa_html_candidate, lxa_library])
  test('HTML foreign names candidate', html_foreign_names_test)
endif

core_test = executable('liblexa_core_consumer',
  files('integration/core_consumer.c'),
  include_directories: lxa_public_inc, link_with: lxa_library)
test('core consumer', core_test)
dom_test = executable('liblexa_dom_store',
  files('unit/dom/node_store_test.c'),
  include_directories: [lxa_public_inc, test_support_inc], link_with: lxa_library)
test('DOM store', dom_test)
dom_attributes_test = executable('liblexa_dom_attributes',
  files('unit/dom/attribute_test.c'),
  include_directories: [lxa_public_inc, test_support_inc], link_with: lxa_library)
test('DOM attributes', dom_attributes_test)
foreach name : ['element', 'node_validate']
  dom_unit_test = executable('liblexa_dom_' + name + '_test',
    files('unit/dom/' + name + '_test.c'),
    include_directories: [lxa_public_inc, test_support_inc],
    link_with: lxa_library)
  test('DOM ' + name, dom_unit_test)
endforeach
unicode_lookup_test = executable('liblexa_unicode_lookup',
  files('unit/unicode/lookup.c'),
  include_directories: [lxa_public_inc, include_directories('../generated/unicode')],
  link_with: lxa_library)
test('Unicode lookup', unicode_lookup_test)
