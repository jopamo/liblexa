project('liblexa', 'c', version: '0.1.0',
  meson_version: '>=1.1.0',
  default_options: ['c_std=c17', 'default_library=both'])

cc = meson.get_compiler('c')
lxa_public_inc = include_directories('include', 'src')
run_target('verify-generated',
  command: [find_program('python3'), files('tools/validate/verify_generated.py'),
    '--build-dir', meson.current_build_dir()])
subdir('src/base')
subdir('src/dom')
subdir('src/encoding')
subdir('src/unicode')
subdir('src/css')
subdir('src')
subdir('tests')
subdir('benchmarks')

install_headers('include/liblexa/base.h', subdir: 'liblexa')
install_headers('include/liblexa/dom.h', subdir: 'liblexa')
install_headers('include/liblexa/encoding.h',
  subdir: 'liblexa')
install_headers('include/liblexa/unicode.h', subdir: 'liblexa')
install_headers('include/liblexa/css.h', subdir: 'liblexa')
if get_option('candidate_html')
  install_headers('include/liblexa/html.h', 'include/liblexa/serialize.h',
    subdir: 'liblexa')
endif
