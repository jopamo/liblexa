executable('liblexa_regions_benchmark', files('base/regions.c'),
  include_directories: lxa_public_inc, link_with: lxa_library)
executable('liblexa_table_benchmark', files('base/table.c'),
  include_directories: lxa_public_inc, link_with: lxa_library)
executable('liblexa_css_tokenizer_benchmark', files('css/tokenizer.c'),
  include_directories: lxa_public_inc, link_with: lxa_library)

if get_option('candidate_html')
  executable('liblexa_html_tokenizer_benchmark',
    files('html/tokenizer.c'),
    include_directories: lxa_public_inc,
    link_with: [lxa_html_candidate, lxa_library])
endif
