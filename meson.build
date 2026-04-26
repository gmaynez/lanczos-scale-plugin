project(
  'lanczos-scale',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

cc = meson.get_compiler('c')

math_dep = cc.find_library('m', required: false)
plugin_opt = get_option('plugin')
gimpui_dep = dependency('gimpui-3.0', required: plugin_opt)
gegl_dep = dependency('gegl-0.4', required: plugin_opt)
optional_math_deps = []
if math_dep.found()
  optional_math_deps += [math_dep]
endif

src_inc = include_directories('src')

lanczos_core_sources = files(
  'src/lanczos-resample.c',
)

lanczos_plugin_sources = files(
  'src/lanczos-scale.c',
  'src/gimp-io.c',
) + lanczos_core_sources

if gimpui_dep.found() and gegl_dep.found()
  plugindir = get_option('plugindir')
  if plugindir == ''
    plugindir = get_option('prefix') / get_option('libdir') / 'gimp' / '3.0' / 'plug-ins' / 'lanczos-scale'
  endif

  executable(
    'lanczos-scale',
    lanczos_plugin_sources,
    include_directories: src_inc,
    dependencies: [
      gimpui_dep,
      gegl_dep,
    ] + optional_math_deps,
    install: true,
    install_dir: plugindir,
    win_subsystem: 'windows',
  )
else
  warning('Skipping lanczos-scale plug-in target because gimpui-3.0/gegl-0.4 development files were not found. Unit tests still build.')
endif

test_kernel = executable(
  'test-lanczos-kernel',
  files('tests/test-lanczos-kernel.c') + lanczos_core_sources,
  include_directories: src_inc,
  dependencies: optional_math_deps,
)

test_resample = executable(
  'test-resample',
  files('tests/test-resample.c') + lanczos_core_sources,
  include_directories: src_inc,
  dependencies: optional_math_deps,
)

test('lanczos-kernel', test_kernel)
test('resample', test_resample)
