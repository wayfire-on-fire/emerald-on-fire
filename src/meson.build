# The plugin (part 01 skeleton; parts 04/05 will extend).
# Part 02 additionally provides emerald-settings / emerald-engine, which
# are compiled into a shared lib that the plugin links against so the
# tests can use them without wayfire symbols.

# emerald.c (load_*_setting helpers) is compiled into the theme lib so the
# symbols are always present. Built as a STATIC lib and linked into the plugin
# (shared_module) so libemeraldwf.so is self-contained — no external
# libemerald_theme.so DT_NEEDED that would be missing at dlopen() time.
# (A shared lib here would add a DT_NEEDED that is never installed, so
# wayfire's dlopen() of the plugin fails and nothing loads.)
emerald_theme_lib = static_library('emerald_theme',
    ['emerald-settings.cpp', 'emerald-engine.cpp',
     'emerald-button-fade.cpp',
     files('../third_party/emerald/libengine/emerald.c')],
    include_directories: [src_inc, emerald_inc,
        include_directories('../third_party/emerald/engines')],
    # -w: the one vendored C file here (libengine/emerald.c) is out of scope
    # for the warning bar (Review S3); c_args is C-only, the C++ sources in
    # this target keep the full -Wall -Wextra -Wshadow bar.
    c_args: ['-w', '-DDEFSETTINGSFILE=0', '-DDEFTHEMEDIR=0'],
    # Review F2: the vendored libengine.h API takes non-const `gchar *`
    # (key/section names); our loader calls pass string literals, which
    # is a C++ conversion -Wwrite-strings flags at the CALL SITES
    # (emerald-settings.cpp). The strings are only read (GKeyFile), and
    # the vendored API is out of scope — suppress for this target only.
    cpp_args: ['-Wno-write-strings'],
    dependencies: emerald_deps,
)

# The plugin itself (part 01 skeleton + part 02 theme glue + part 04 node
# + part 05 shade + part 06 live reload).
emeraldwf = shared_module('emeraldwf',
    ['plugin.cpp', 'emerald-decorator.cpp',
     'emerald-decorator-node.cpp', 'emerald-node.cpp',
     'emerald-config-sync.cpp', 'emerald-layout.cpp',
     'emerald-node-pure.cpp', 'emerald-shade.cpp',
     'emerald-decorator-shade.cpp', 'emerald-shadow-margins.cpp'],
    include_directories: [src_inc, emerald_inc],
    dependencies: [wayfire] + emerald_deps,
    link_with: [emerald_theme_lib],
    # <libdir>/wayfire == wayfire's plugindir on standard layouts; the
    # "wayfire" dir name is what WPM's install-log classifier requires.
    # install.sh NEVER runs meson's install for this file — it places it
    # ATOMICALLY (temp + rename): wayfire has the .so mmap'd, and meson's
    # copy (truncate + write) would SIGBUS the running session.
    install: true,
    install_dir: join_paths(get_option('libdir'), 'wayfire'))
