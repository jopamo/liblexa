generator_test_python = find_program('python3')
generator_test_env = environment()
generator_test_env.set('PYTHONPATH', meson.project_source_root())
generator_test_env.set('PYTHONDONTWRITEBYTECODE', '1')

foreach name : ['encoding_indexes', 'html_categories', 'html_charref_overrides',
                'html_entities', 'html_tags', 'idna', 'joining_types',
                'mathml3_tags', 'mathml_tags', 'normalization', 'provenance',
                'svg_tags', 'verify_generated']
  test('generated ' + name, generator_test_python,
    args: files('test_' + name + '.py'), env: generator_test_env, suite: 'data')
endforeach
