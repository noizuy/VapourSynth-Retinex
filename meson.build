project('Retinex', 'cpp',
  default_options: ['buildtype=release', 'b_ndebug=if-release', 'cpp_std=c++14'],
  meson_version: '>=0.49.0',
  version: '4'
)

sources = [
  'source/AssBlur.cpp',
  'source/BoxBlur.cpp',
  'source/BoxFilter.cpp',
  'source/Device.cpp',
  'source/GaussBlur.cpp',
  'source/GuidedFilter.cpp',
  'source/GuidedRCP.cpp',
  'source/IirBlur.cpp',
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

# Lets GCC turn the per-pixel selects of the hot loops (see include/Simd.h) into vector blends.
# Clang already defaults to it; FP exceptions are never inspected.
cpp = meson.get_compiler('cpp')
add_project_arguments(cpp.get_supported_arguments('-fno-trapping-math'), language : 'cpp')

# Keep the module mapped after dlclose(): with a statically linked C++ runtime (e.g. zig c++),
# runtime exit-time/thread-local destructors can run after VapourSynth unloads the
# plugin and crash the process on exit.
link_args = []
if host_machine.system() not in ['windows', 'darwin']
  link_args += ['-Wl,-z,nodelete']
endif

shared_module('retinex', sources,
  link_args : link_args,
  include_directories : include_directories('include'),
  install : true,
  install_dir : py.get_install_dir() / 'vapoursynth' / 'plugins',
  name_prefix : '',
  gnu_symbol_visibility : 'hidden'
)

# Exhaustive accuracy check of FastLog (include/Simd.h); built only for `meson test`
fastlog_test = executable('fastlog_test', 'tests/fastlog_test.cpp',
  include_directories : include_directories('include'),
  build_by_default : false
)
test('fastlog', fastlog_test, timeout : 300)
