# libemeraldengine: SHARED lib from third_party/emerald/libengine.
#
# Must be a shared library (upstream builds it as one — see
# emeraldengine.pc.in: Libs: -lemeraldengine). The engine .so's and the
# theme editor all reference this library's symbols; with one shared
# copy the whole settings machinery is a single instance in the process:
# one SettingList, one global_theme_file (init_key_files), one apply
# flag, one do_engine. That is the upstream invariant.
#
# The part-07 port vendored it as a static archive into every engine
# .so. That gave each .so its own copy of the globals, which broke two
# things (findings 46/47):
#   1. append_engine() dlcloses the engine .so after building the pane;
#      with the refcount at zero the module is unmapped while the pane's
#      widgets keep signal callbacks into the module's code — the first
#      click in the engine pane is a SEGV_MAPERR (upstream is safe
#      because the emerald X11 plugin keeps the active engine dlopen'd
#      in-process; on wayfire the plugin runs in wayfire, a separate
#      process).
#   2. the engine .so's cb_apply_setting_real ran with its own
#      uninitialized global_theme_file and its own `apply` flag (false)
#      — engine-pane edits were silently never written to theme.ini.
# With the shared library, dlclose(engine.so) only unloads the engine's
# layout code (referenced only during init); the callbacks live in
# libemeraldengine.so, which the executable keeps loaded, and all
# writes go through the one initialized machinery.
#
# themer.c is included because the engines' layout_engine_settings()
# symbols are referenced by their .so's and need themer.c's
# table_new/register_setting/build_notebook_page etc. at link time
# (dlopen RTLD_NOW). setup_dbus() is stubbed via compat/dbus-stub.c to
# avoid the dbus-glib dependency (USE_DBUS is not defined, so themer.c's
# own setup_dbus is not compiled).
#
# DEFSETTINGSFILE / DEFTHEMEDIR are normally defined by emerald's
# autotools build (libengine/Makefile.am); they are unused on the
# wayfire port (ThemeSettings resolves the vendored defaults itself)
# so we define empty placeholders to keep the vendored source
# unmodified.
#
# Part 07: ENGINE_DIR is the real system engine dir so the theme
# editor's engine scan (init_engine_list: ~/.emerald/engines +
# ENGINE_DIR) finds the installed engines.
#
# Part 08 (crash fix): installed to <libdir>/emeraldwf (private dir —
# no system ldconfig needed; consumers carry an $ORIGIN rpath).
libemeraldengine = shared_library('emeraldengine',
    ['../third_party/emerald/libengine/emerald.c',
     '../third_party/emerald/libengine/themer.c'],
    include_directories: [emerald_inc],
    c_args: ['-w', # Review S3: vendored C, out of scope for the warning bar
             '-DDEFSETTINGSFILE=0',
             '-DDEFTHEMEDIR=0',
             # Absolute path: get_option('libdir') alone is relative
             # ("lib64") on Gentoo — engine_scan_dir() would then open a
             # CWD-relative dir and find no engines.
             '-DENGINE_DIR="' +
                 join_paths(get_option('prefix'), get_option('libdir'),
                             'emeraldwf', 'engines') + '"',
             '-include',
             '../compat/themer-version-stub.h'],
    # x11: send_reload_signal's X11 fallback path (interposed at runtime
    # by the themer/test executables — emerald-of.c's no-op wins).
    dependencies: [glib, gdk, gtk, cairo, pango, pangocairo, x11],
    version: '0.1.0',
    soversion: '0',
    # The engines' DT_NEEDED resolves via their $ORIGIN/.. rpath.
    # install.sh places it ATOMICALLY (temp + rename) — <libdir>/emeraldwf
    # if writable, else next to the engines (both are probed by the
    # engines' and the themer's rpaths); it never runs meson's install.
    install: true,
    install_dir: join_paths(get_option('libdir'), 'emeraldwf'))
