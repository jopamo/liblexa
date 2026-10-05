project('liblexa', 'c', version: '0.1.0',
  meson_version: '>=1.1.0',
  default_options: ['c_std=c17', 'default_library=both'])

cc = meson.get_compiler('c')
lxa_public_inc = include_directories('include', 'src')
candidate_html_enabled = get_option('candidate_html')
subdir('src/base')
subdir('src/dom')
subdir('src/encoding')
subdir('src/unicode')
subdir('src/css')
subdir('src')

install_headers('include/liblexa/base.h', subdir: 'liblexa')
install_headers('include/liblexa/dom.h', subdir: 'liblexa')
install_headers('include/liblexa/encoding.h',
  subdir: 'liblexa')
install_headers('include/liblexa/unicode.h', subdir: 'liblexa')
install_headers('include/liblexa/css.h', subdir: 'liblexa')
if candidate_html_enabled
  install_headers('include/liblexa/html.h', 'include/liblexa/html_source.h',
    'include/liblexa/serialize.h',
    subdir: 'liblexa')
endif
