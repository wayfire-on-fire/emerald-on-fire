# Build rules only: the engine C sources live in third_party/emerald/engines.
# Each engine becomes a shared module linked against the SHARED
# libemeraldengine (one settings machinery per process — see
# libengine/meson.build). The rpath keeps the module bound to the
# installed copy: libemeraldengine.so normally lives in <libdir>/emeraldwf
# ($ORIGIN/..); on boxes where that dir is not writable (and install.sh
# falls back), it is installed NEXT TO the engines, so $ORIGIN is probed
# as well.

emerald_engine_deps = [glib, gdk, gtk, cairo, pango, pangocairo, x11]

emerald_engine_libs = []
foreach eng : engine_names
    # emerald names its engine modules lib<name>.so (no emerald_ prefix);
    # keep that naming so dlopen("lib<name>.so") finds them.
    # Produce lib<name>.so via soversion symlink so dlopen("lib<name>.so")
    # finds it. Meson creates lib<name>.so -> lib<name>.so.<soversion>.
    emerald_engine_libs += [shared_library(eng,
        files('../third_party/emerald/engines/' + eng + '.c'),
        include_directories: [emerald_inc,
            include_directories('../third_party/emerald/engines')],
        # Review S3: vendored, unmodified emerald C — out of scope for the
        # -Wall -Wextra bar (and noisy under meson's default -Wall:
        # deprecated g_memdup, unused vars, ...). -w is C-only, so it
        # suppresses the build log without touching the C++ warning bar.
        c_args: ['-w'],
        dependencies: emerald_engine_deps,
        link_with: [libemeraldengine],
        name_prefix: 'lib',
        version: '0.1.0',
        soversion: '0',
        # Part 06: the dlopen system_dir the plugin resolves at runtime
        # as <plugin-so-dir>/../emeraldwf/engines (emerald-settings.cpp).
        # install.sh places the engines ATOMICALLY (temp + rename) — it
        # never runs meson's install: a running wayfire mmap's these
        # files, and meson's copy (truncate + write) would SIGBUS it.
        install: true,
        install_dir: join_paths(get_option('libdir'), 'emeraldwf', 'engines'),
        # libemeraldengine.so: <libdir>/emeraldwf ($ORIGIN/..), or next
        # to the engines ($ORIGIN) when install.sh's user-writable
        # fallback places it there. Private lib — no system ldconfig.
        install_rpath: '$ORIGIN/..:$ORIGIN',
        build_rpath: '$ORIGIN/..:$ORIGIN',
    )]
endforeach
