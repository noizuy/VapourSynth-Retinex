project('Retinex', 'cpp',
  default_options: ['buildtype=release', 'b_ndebug=if-release', 'cpp_std=c++14'],
  meson_version: '>=0.49.0',
  version: '4'
)

sources = [
  'source/Device.cpp',
  'source/Intensity.cpp',
  'source/MSR.cpp',
  'source/MSRCP.cpp',
  'source/MSRCR.cpp',
  'source/VSPlugin.cpp'
]

# The VapourSynth4/VSHelper4 headers are vendored under include/vapoursynth so that the
# plugin can be built without a system pkg-config/dev package, e.g. inside the isolated
# build environment used when packaging this project as a Python wheel.
py = import('python').find_installation()

if host_machine.cpu_family().startswith('x86')
  add_project_arguments('-mfpmath=sse', '-msse2', language : 'cpp')
endif

shared_module('retinex', sources,
  include_directories : include_directories('include'),
  install : true,
  install_dir : py.get_install_dir() / 'vapoursynth' / 'plugins',
  name_prefix : '',
  gnu_symbol_visibility : 'hidden'
)
