lxa_unicode_sources = files('lookup.c')
lxa_unicode = static_library('liblexa_unicode',
  lxa_unicode_sources + files('../../generated/unicode/idna-table.h',
        '../../generated/unicode/joining-types.h',
        '../../generated/unicode/normalization-table.h'),
  dependencies: lxa_core_dependency,
  include_directories: [lxa_public_inc, include_directories('../../generated/unicode')],
  install: false)
