# Test-trigger builds of the window-zoom plugin (regression harness).
#
# window-zoom-stock.cpp  = upstream v0.11.2 source + WZ_TEST_TRIGGER
# window-zoom-patched.cpp = emeraldwf patched source + WZ_TEST_TRIGGER
#
# The trigger emulates the user pressing the zoom binding when a toplevel
# view is mapped (XWayland/XTest keys never reach wayfire global bindings,
# so the repro cannot rely on synthetic X11 input).
#
# Build:
#   meson setup build-stock .  -Dwzvariant=stock   (needs a separate dir)
# Simpler: two dirs, see the Makefile-less manual steps in the parent dir.

project('winzoom-test', 'cpp',
    version: '0.11.2-test',
    meson_version: '>=0.59.0',
    default_options: ['cpp_std=c++17'],
)

add_project_arguments(['-DWLR_USE_UNSTABLE'], language: 'cpp')
add_project_arguments(['-DWZ_TEST_TRIGGER'], language: 'cpp')

wayfire = dependency('wayfire', version: '>=0.11.0')

variant = get_option('wzvariant')

if variant == 'stock'
  src = 'window-zoom-stock.cpp'
else
  src = 'window-zoom-patched.cpp'
endif

shared_module('winzoom',
    src,
    dependencies: [wayfire],
    install: false)
