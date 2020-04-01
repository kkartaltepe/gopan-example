project('gopan', 'c',
  version : '0.0',
  default_options : ['warning_level=3'])

sources = files([
  'gopan.c',
  'gp_props.h',
  'gp_props_tables.h',
  ])

harfbuzz = dependency('harfbuzz')
cairo = dependency('cairo')
fribidi = dependency('fribidi')
fontconfig = dependency('fontconfig')

executable('gopan', sources,
           dependencies : [harfbuzz, fribidi, cairo, fontconfig],
           install : true)
