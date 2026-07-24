lxa_encoding_sources = files('stream.c')
lxa_encoding = static_library('liblexa_encoding',
  lxa_encoding_sources + files('../../generated/encoding/encoding-indexes.h'),
  dependencies: lxa_core_dependency,
  include_directories: [lxa_public_inc,
    include_directories('../../generated/encoding')],
  install: false)
lxa_encoding_dependency = declare_dependency(
  link_with: lxa_encoding, dependencies: lxa_core_dependency,
  include_directories: lxa_public_inc)
