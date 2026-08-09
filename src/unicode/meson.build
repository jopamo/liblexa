lxa_unicode_sources = files('lookup.c')
unicode_generated_inc = include_directories('../../generated/unicode')
lxa_unicode = static_library('liblexa_unicode',
  lxa_unicode_sources + files('../../generated/unicode/idna-table.h',
        '../../generated/unicode/joining-types.h',
        '../../generated/unicode/normalization-table.h'),
  dependencies: lxa_core_dependency,
  include_directories: [lxa_public_inc, unicode_generated_inc],
  install: false)
