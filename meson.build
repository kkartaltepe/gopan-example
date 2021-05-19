project('gopan', 'c',
  version : '0.0',
  default_options : ['warning_level=3', 'c_std=c99'])

srcs= files([
  'gp.c',
  'gp_ttf.c',
  'gp_props.c',
  ])

pub_hdrs = files([
  'gp.h',
  'gp_ttf.h',
  'gp_props.h',
  ])

harfbuzz = dependency('harfbuzz')
cairo = dependency('cairo')
fribidi = dependency('fribidi')
cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)

install_headers(pub_hdrs, subdir: 'gp')
gp_lib = library('gp', srcs + pub_hdrs,
           dependencies : [harfbuzz, cairo, fribidi, m_dep],
           install : true)

pkg = import('pkgconfig')
pkg.generate(libraries : gp_lib,
  requires: [harfbuzz, cairo], # exposed via header
  subdirs : 'gp',
  name : 'libgp',
  description : 'Simple userland font rendering')

# Simple example of use
executable('gopan', ['sample/gopan.c'], link_with: [gp_lib], dependencies: [ harfbuzz, cairo])
