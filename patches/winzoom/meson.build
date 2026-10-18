# emeraldwf — patched window-zoom (winzoom) plugin.
#
# Upstream (wayfire-plugins-extra v0.11.2) keeps a raw view pointer in its
# per-output plugin instance for every zoomed window and only cleans it up in
# fini(). A zoomed window that gets closed (or a zoom returned to scale 1.0)
# leaves a dangling pointer; disabling the plugin then dereferences the
# destroyed view in fini() -> segfault -> the whole wayfire session dies.
#
# The patched source (window-zoom.cpp) cleans the map up on view_unmapped.
# Built standalone and installed over the dist libwinzoom.so by install.sh
# (atomic temp+rename — a running wayfire has the .so mmap'd).

project('winzoom-patch', 'cpp',
    version: '0.11.2-emeraldwf-patched',
    meson_version: '>=0.59.0',
    default_options: ['cpp_std=c++17'],
)

add_project_arguments(['-DWLR_USE_UNSTABLE'], language: 'cpp')

wayfire = dependency('wayfire', version: '>=0.11.0')

shared_module('winzoom',
    'window-zoom.cpp',
    dependencies: [wayfire],
    # install.sh places it into wayfire's plugindir atomically; meson's
    # install would truncate+write over the running session's mapping.
    install: false)
