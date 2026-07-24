css_math_deps = []
if not cc.has_function('pow', prefix: '#include <math.h>')
  css_math_deps += cc.find_library('m', required: true)
endif
css_consumer_sources = files('token.c', 'filter.c', 'scan.c', 'bytes.c',
  'components.c', 'declaration.c', 'declarations.c', 'import.c', 'media.c',
  'rule.c', 'selector_key.c', 'selector_list.c', 'value_text.c')
