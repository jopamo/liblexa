lxa_entropy_platform_dep = []
if host_machine.system() == 'windows'
  lxa_entropy_platform_dep = cc.find_library('bcrypt', required: true)
endif
lxa_core_sources = files('memory.c', 'region.c', 'bytes.c', 'vector.c', 'budget.c',
  'entropy.c', 'siphash.c', 'table.c')
lxa_core = static_library('liblexa_core',
  lxa_core_sources,
  include_directories: lxa_public_inc,
  dependencies: lxa_entropy_platform_dep,
  install: false)
lxa_core_dependency = declare_dependency(
  link_with: lxa_core, include_directories: lxa_public_inc,
  dependencies: lxa_entropy_platform_dep)
