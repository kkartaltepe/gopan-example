project('gopan', 'c',
  version : '0.0',
  default_options : ['warning_level=3'])

srcs= files([
  'gp.c',
  'gp_props.c',
  ])

pub_hdrs = files([
  'gp.h',
  'gp_props.h',
  ])

harfbuzz = dependency('harfbuzz')
cairo = dependency('cairo')
fribidi = dependency('fribidi')
fontconfig = dependency('fontconfig')

install_headers(pub_hdrs, subdir: 'gp')
gp_lib = library('gp', srcs + pub_hdrs,
           dependencies : [harfbuzz, fribidi, cairo, fontconfig],
           install : true)

pkg = import('pkgconfig')
pkg.generate(libraries : gp_lib,
  requires: [harfbuzz, cairo, fontconfig], # exposed via header
  subdirs : 'gp',
  name : 'libgp',
  description : 'Simple userland font rendering')

# Simple example of use
executable('gopan', ['sample/gopan.c'], link_with: [gp_lib], dependencies: [cairo, harfbuzz, fontconfig])
