# Part 07: emerald-theme-manager — the emeraldwf theme editor.
#
# Port of emerald/themer/main.c, linked against the shared
# libemeraldengine (the vendored libengine/themer.c settings registry:
# SettingItem, debounced apply, table helpers, engine scan).
#
# send_reload_signal() interposition: emerald-of.c defines a no-op
# (emeraldwf's mtime poller makes file writes the live-reload
# mechanism). At runtime the executable's definition precedes
# libemeraldengine.so's X11 version in the global symbol scope, so the
# no-op wins for every call site, including apply_settings' internal
# call inside the library (standard ELF interposition — no
# multiple-definition flag needed with a shared library).
#
# DATA_DIR must be a C string literal (main.c concatenates
# DATA_DIR "/emeraldwf/themes/" at compile time).

emerald_of_deps = [glib, gdk, gtk, cairo, pango, pangocairo, gdk_pixbuf, x11]

# NOT installed by meson (install.sh does, with user-dir fallbacks —
# /usr/bin & /usr/share are not writable without root on this system,
# same reason as the WCM icon in the top-level meson.build):
#   emerald-theme-manager      → /usr/bin (fallback ~/.local/bin)
#   emerald-theme-manager.desktop → <datadir>/applications
#   system themes              → <datadir>/emeraldwf/themes
#   app icon                   → hicolor apps icondir
executable(
    'emerald-theme-manager',
    ['main.c', 'emerald-of.c'],
    include_directories: emerald_inc,
    dependencies: emerald_of_deps,
    link_with: libemeraldengine,
    # install.sh copies the binary to /usr/bin (or ~/.local/bin), far
    # from the private lib dir — bake in the absolute rpath. Probe both
    # the standard lib location and the user-writable fallback
    # (install.sh places libemeraldengine.so in engines/ when
    # <libdir>/emeraldwf is not writable).
    install_rpath: join_paths(get_option('prefix'), get_option('libdir'),
                              'emeraldwf') + ':' +
                   join_paths(get_option('prefix'), get_option('libdir'),
                              'emeraldwf', 'engines'),
    # install.sh copies the binary with cp (meson's install-time rpath
    # rewrite never runs) — carry the absolute rpath into the build tree
    # so the installed copy finds libemeraldengine.so.
    build_rpath: join_paths(get_option('prefix'), get_option('libdir'),
                            'emeraldwf') + ':' +
                 join_paths(get_option('prefix'), get_option('libdir'),
                            'emeraldwf', 'engines'),
    c_args: [
        # Absolute paths: get_option('datadir')/'localedir' are relative
        # on Gentoo ("share"), and main.c concatenates DATA_DIR with a
        # string literal at compile time (CWD-relative dirs would find
        # no system themes).
        '-DDATA_DIR="' +
            join_paths(get_option('prefix'), get_option('datadir')) + '"',
        '-DGETTEXT_PACKAGE="' + meson.project_name() + '"',
        '-DLOCALEDIR="' +
            join_paths(get_option('prefix'), get_option('localedir'),
                       'emeraldwf') + '"',
    ],
)
