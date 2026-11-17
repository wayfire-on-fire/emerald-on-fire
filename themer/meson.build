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

# The themer runs from /usr/bin (or ~/.local/bin) — far from the private
# lib dir — so its rpath names the lib location absolutely. A single
# build-libdir entry is image-pinned in docker builds (Ubuntu:
# lib/x86_64-linux-gnu) and breaks on hosts with a different libdir
# (Gentoo lib64, Arch lib): "error while loading shared libraries:
# libemeraldengine.so.0". Cover the common distro libdirs instead — the
# loader skips entries whose directory does not exist, so the host's own
# entry wins. (No patchelf/ldconfig: the private dir is intentionally
# outside the system loader path.)
emeraldwf_libdirs = [
    get_option('libdir'),         # the build's own libdir (exact here)
    'lib',                        # Arch
    'lib64',                      # Gentoo, RHEL, OpenSUSE
    'lib/x86_64-linux-gnu',       # Ubuntu/Debian
    'lib64/x86_64-linux-gnu',
    'lib/aarch64-linux-gnu',
    'lib64/aarch64-linux-gnu',
]
emeraldwf_rpath_parts = []
foreach ld : emeraldwf_libdirs
    emeraldwf_rpath_parts += [
        join_paths(get_option('prefix'), ld, 'emeraldwf'),
        # the user-writable fallback install location (install.sh places
        # libemeraldengine.so in engines/ when <libdir>/emeraldwf is not
        # writable)
        join_paths(get_option('prefix'), ld, 'emeraldwf', 'engines'),
    ]
endforeach
# The build's own libdir may duplicate a hardcoded entry (e.g. lib64) —
# dedupe; the loader ignores duplicates, but a clean RUNPATH is nicer.
emeraldwf_rpath_unique = []
foreach part : emeraldwf_rpath_parts
    if not (part in emeraldwf_rpath_unique)
        emeraldwf_rpath_unique += [part]
    endif
endforeach
# Meson arrays have no join() — concat with a running separator.
emeraldwf_rpath = ''
sep = ''
foreach part : emeraldwf_rpath_unique
    emeraldwf_rpath += sep + part
    sep = ':'
endforeach

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
    # from the private lib dir — bake in the absolute rpath (the common
    # distro libdirs — see emeraldwf_rpath above; probe both the standard
    # lib location and the user-writable fallback).
    install_rpath: emeraldwf_rpath,
    # install.sh copies the binary with cp (meson's install-time rpath
    # rewrite never runs) — carry the same rpath into the build tree so
    # the installed copy finds libemeraldengine.so.
    build_rpath: emeraldwf_rpath,
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
