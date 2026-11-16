# Part 02 tests (headless).
emerald_test_deps = [gtest, gtest_main, gdk_pixbuf] + emerald_deps

# Review R2: forward the fixture paths from meson. The tests used to resolve
# them from /proc/self/exe (three parent hops = <srcroot>), hardcoding
# <srcroot>/build/engines and <srcroot>/metadata — any OUT-OF-TREE build
# (meson setup <dir>) builds the engines into <builddir>/engines, so the
# suites went red on missing paths. configure_file() (test-paths.in →
# test-paths.h) stamps the real paths in: the engine dir is where meson PUT
# the built .so's (works in-tree AND out-of-tree); source-tree fixtures
# (vendored defaults, metadata, goldens) come from the source root.
emerald_test_paths = configure_file(
    input: 'test-paths.in',
    output: 'test-paths.h',
    configuration: {
        'SOURCE_ROOT': meson.project_source_root(),
        # The engine .so's build into <buildroot>/engines (subdir('engines'));
        # build_root() works in-tree AND out-of-tree.
        'ENGINE_DIR': meson.build_root() / 'engines',
        # The fake engine .so's (M1-M3 loader tests) build into
        # <buildroot>/tests, alongside this meson file's outputs.
        'FAKE_ENGINE_DIR': meson.build_root() / 'tests',
        'METADATA_DIR': meson.project_source_root() / 'metadata',
    })
emerald_test_inc = include_directories('.')

emeraldwf_settings = executable('emeraldwf-settings',
    ['test_settings.cpp'],
    include_directories: [src_inc, emerald_inc, emerald_test_inc],
    dependencies: emerald_test_deps,
    link_with: [emerald_theme_lib, libemeraldengine])

emeraldwf_render = executable('emeraldwf-render',
    ['test_render.cpp', '../src/emerald-layout.cpp'],
    include_directories: [src_inc, emerald_inc, emerald_test_inc],
    dependencies: emerald_test_deps,
    link_with: [emerald_theme_lib, libemeraldengine])

make_goldens = executable('make_goldens',
    ['make_goldens.cpp'],
    include_directories: [src_inc, emerald_inc, emerald_test_inc],
    dependencies: emerald_test_deps,
    link_with: [emerald_theme_lib, libemeraldengine])

test('emeraldwf-settings', emeraldwf_settings)
test('emeraldwf-render', emeraldwf_render)

# ---- part 01: margin math (headless, no emerald/wayfire symbols) ----
# Margin math lives in emerald-decorator.cpp; compile it directly into the
# test. The test calls the pure static margins_for() and the pure
# emerald_state_from_view(state, sticky). The live-view methods
# (toplevel(), should_be_decorated(), activated, sticky) are stubbed here
# so the decorator TU links headlessly. ThemeSettings (part 02) is stubbed
# in emerald-settings-stub.cpp.
view_stub = static_library('emeraldwf-view-stub',
    ['emerald-view-stub.cpp'],
    include_directories: [src_inc, emerald_inc],
    dependencies: [wayfire, gtk, glib, cairo, pango, pangocairo])

emeraldwf_margins = executable('emeraldwf-margins',
    ['test_margins.cpp', '../src/emerald-decorator.cpp',
     'emerald-settings-stub.cpp'],
    link_with: [view_stub],
    include_directories: [src_inc, emerald_inc],
    dependencies: emerald_test_extra + [gtest_main])
test('emeraldwf-margins', emeraldwf_margins)

# ---- part 03: layout + hit testing (headless, pure emerald structs) ----
emeraldwf_layout = executable('emeraldwf-layout',
    ['test_layout.cpp', '../src/emerald-layout.cpp'],
    include_directories: [src_inc, emerald_inc],
    dependencies: emerald_test_extra + [gtest_main])
test('emeraldwf-layout', emeraldwf_layout)

# R4 (issue #1): ASan build of the layout suite. An unknown
# titlebar_layout char made pass 2 write tobj_item_pos[-2] — a
# heap-buffer-overflow that is UB (silent corruption) in a plain build
# but a deterministic abort under ASan.
emeraldwf_layout_asan = executable('emeraldwf-layout-asan',
    ['test_layout.cpp', '../src/emerald-layout.cpp'],
    include_directories: [src_inc, emerald_inc],
    dependencies: emerald_test_extra + [gtest_main],
    c_args: ['-fsanitize=address'],
    cpp_args: ['-fsanitize=address'],
    link_args: ['-fsanitize=address'])
test('emeraldwf-layout-asan', emeraldwf_layout_asan)

# R5 (issue #1): static/script-logic tests for e2e/run.sh (no Xvfb on
# CI boxes): bash -n, the pure helpers sourced via run.sh's source
# guard (imgcmp/golden/vendor_check), and the structural contracts of
# the R5 fix (own Xvfb + exported DISPLAY, no hardcoded vendor path,
# guarded screenshots, missing golden = SKIPPED).
test('emeraldwf-e2e-script', find_program('../e2e/test_run.sh'))

# ---- engine dlopen bridge (M1-M3, issue #1) ----
# The fake engines are real .so's the bridge DLOPENS at runtime by name
# (lib<name>.so) — NOT linked into the test. C files: the loader
# dlsym()s unmangled names.
fake_engine_ok = shared_library('fakeok', ['engine-fake-ok.c'],
    name_prefix: 'lib')
fake_engine_alt = shared_library('fakealt', ['engine-fake-alt.c'],
    name_prefix: 'lib')
fake_engine_nodraw = shared_library('fakenodraw', ['engine-fake-nodraw.c'],
    name_prefix: 'lib')

emeraldwf_engine_loader = executable('emeraldwf-engine-loader',
    ['test_engine_loader.cpp', '../src/emerald-engine.cpp'],
    include_directories: [src_inc, emerald_inc, emerald_test_inc],
    dependencies: emerald_test_deps + [wayfire],
    link_with: [emerald_theme_lib, libemeraldengine])
test('emeraldwf-engine-loader', emeraldwf_engine_loader,
    depends: [fake_engine_ok, fake_engine_alt, fake_engine_nodraw])

# M4 (issue #1): source-structure test for the plugin TU (fini() must
# unload the engine after the decoration-removal loop). plugin.cpp
# cannot be linked headlessly (wayfire core symbols).
emeraldwf_plugin_source = executable('emeraldwf-plugin-source',
    ['test_plugin_source.cpp'],
    include_directories: [emerald_test_inc],
    dependencies: [gtest, gtest_main])
test('emeraldwf-plugin-source', emeraldwf_plugin_source)

# M9 (issue #1): source-structure test for the node TU's render_frame()
# ordering (rebuild decision before the per-frame work). emerald-node.cpp
# cannot be linked headlessly (wayfire scene symbols).
emeraldwf_node_source = executable('emeraldwf-node-source',
    ['test_node_source.cpp'],
    include_directories: [emerald_test_inc],
    dependencies: [gtest, gtest_main])
test('emeraldwf-node-source', emeraldwf_node_source)

# ---- part 04: scene node pure helpers (headless) ----
# Compiles only the wayfire-free pure TU (emerald-node-pure.cpp) so the test
# links without wayfire core symbols (libwayfire is not a linkable shared
# lib). It exercises needs_rebuild — the invalidation predicate extracted per
# spec 04 "Tests".
emeraldwf_render_node = executable('emeraldwf-render-node',
    ['test_render_node.cpp',
     '../src/emerald-node-pure.cpp'],
    include_directories: [src_inc, emerald_inc],
    dependencies: [gtest, gtest_main, cairo])
test('emeraldwf-render-node', emeraldwf_render_node)

# ---- part 05: shade + shadows (headless) ----
# The margin-steadiness tests compile emerald-decorator.cpp (for the pure
# margins_for) + emerald-shadow-margins.cpp (for the pure shadow_margins_for)
# + emerald-shade.hpp (for the pure ShadeState).
# The live-view methods are stubbed by emerald-view-stub.cpp.
emeraldwf_shade = executable('emeraldwf-shade',
    ['test_shade.cpp', '../src/emerald-decorator.cpp',
     '../src/emerald-shadow-margins.cpp',
     'emerald-settings-stub.cpp', 'emerald-view-stub.cpp'],
    link_with: [view_stub],
    include_directories: [src_inc, emerald_inc],
    dependencies: emerald_test_extra + [gtest_main])
test('emeraldwf-shade', emeraldwf_shade)

# M17 (issue #1): source-structure test for the shade TU's output
# re-derivation (sync_output in init_animation/tick). emerald-shade.cpp
# cannot be linked headlessly (wayfire scene + output symbols).
emeraldwf_shade_source = executable('emeraldwf-shade-source',
    ['test_shade_source.cpp'],
    include_directories: [emerald_test_inc],
    dependencies: [gtest, gtest_main])
test('emeraldwf-shade-source', emeraldwf_shade_source)

# M22 (issue #1): install.sh's sudo home resolution (shell-level test:
# extracts the resolve_user_home() block from install.sh and exercises
# it; the script itself builds first, so it is not run whole).
test('emeraldwf-install-home', find_program('test_install_home.sh'))

# M23 (issue #1): source-structure test for the themer's theme import
# (tar-slip: basename-derived name, pre-extraction member validation,
# temp-dir + rename). themer/main.c is a GTK TU — not headless-linkable.
emeraldwf_themer_source = executable('emeraldwf-themer-source',
    ['test_themer_source.cpp'],
    include_directories: [emerald_test_inc],
    dependencies: [gtest, gtest_main])
test('emeraldwf-themer-source', emeraldwf_themer_source)

# M23 (issue #1, ChatGPT follow-up): pin the TAR signatures the
# import validation relies on (entry-type chars in `tar -tvzf`, '..'
# and absolute member paths in `tar -tzf`) against REAL tar output,
# including malicious archives (symlink / hardlink / FIFO / '..' /
# absolute members) built with python3 tarfile.
test('emeraldwf-themer-import', find_program('test_themer_import.sh'))

# M25 (issue #1): run-kvm.sh fetch_image() must verify the downloaded
# .part BEFORE promoting it to the cache (shell-level test: extracts
# the function block and runs it with a stubbed curl - the real
# script needs qemu + network).
test('emeraldwf-run-kvm-fetch', find_program('test_run_kvm_fetch.sh'))

# Minor (issue #1): run-kvm.sh's bring_up() must refuse to launch a new QEMU
# when one is already running for the distro (a re-run against a live VM would
# silently target the stale guest via the deterministic port). Shell-level test.
test('emeraldwf-run-kvm-livewm', find_program('test_run_kvm_livewm.sh'))

# M26 (issue #1): matrix.sh must exit non-zero when any distro fails
# (shell-level test: extracts the collect_results() block and runs it
# with a stubbed wait - the real script needs qemu + /dev/kvm).
test('emeraldwf-matrix-collect', find_program('test_matrix_collect.sh'))

# M27 (issue #1): the Makefile's Docker `binary` target must build the
# dist artifacts against the prefix the Dockerfile installs wayfire
# into (/usr) - source-structure cross-check of Makefile + Dockerfile.
emeraldwf_makefile_docker = executable('emeraldwf-makefile-docker',
    ['test_makefile_docker.cpp'],
    include_directories: [emerald_test_inc],
    dependencies: [gtest, gtest_main])
test('emeraldwf-makefile-docker', emeraldwf_makefile_docker)

# Minor (issue #1): the Dockerfile and e2e/distros/provision.sh must install a
# PINNED meson via pip (an unpinned `meson` lets a future release silently
# change the build tool and break the from-source builds). Shell-level test.
test('emeraldwf-ci-meson-pin', find_program('test_ci_meson_pin.sh'))

# Minor (issue #1): the Makefile CI (Docker) path must build the plugin ONCE
# (a single release build serves both the artifact and the test suite), not
# twice (a release `binary` build + a separate debug `test` build).
# Shell-level test.
test('emeraldwf-makefile-single-build', find_program('test_makefile_single_build.sh'))

# Minor (issue #1): the wayfire-from-source recipe (wayland/wayland-protocols/
# libdrm/xkbcommon/pixman/wayfire) must live in ONE shared script, not be
# copy-pasted in the Dockerfile, ci.yml, and provision.sh. Shell-level test.
test('emeraldwf-wayfire-src-single', find_program('test_wayfire_src_single.sh'))

# Minor (issue #1): the ci.yml `arch` job must install packages without a full
# system upgrade (pacman -Syu is slow + non-deterministic). Shell-level test.
test('emeraldwf-ci-arch-no-upgrade', find_program('test_ci_arch_no_full_upgrade.sh'))

# M28 (issue #1): install.sh WF_PLUGDIR must be the dir the plugin was
# installed into ($LIBDIR/wayfire) - shell-level test: extracts the
# assignment and evaluates it with a stubbed pkg-config (fail + ok +
# non-default libdir).
test('emeraldwf-install-plugdir', find_program('test_install_plugdir.sh'))

# Minor (issue #1): install.sh must not hardcode the library version
# ("0.1.0") - a version bump must not silently break the install. Shell-level
# test: runs atomic_versioned_so against a non-0.1.0 version (stubbed helpers)
# and asserts the engine glob / libemeraldengine lookup / count are
# version-agnostic.
test('emeraldwf-install-version', find_program('test_install_version.sh'))

# Minor (issue #1): install.sh must detect a STALE build tree (configured with
# a different wayfire prefix/libdir) and reconfigure, not blindly reuse it -
# libemeraldengine bakes ENGINE_DIR at compile time. Shell-level test.
test('emeraldwf-install-builtree', find_program('test_install_builtree.sh'))

# Minor (issue #1): install.sh's default-theme refresh must be ATOMIC (temp
# dir + rename), not a bare rm -rf + cp -r that leaves a window where a
# running plugin sees a missing/incomplete theme. Shell-level test.
test('emeraldwf-install-defaulttheme', find_program('test_install_defaulttheme.sh'))

# Minor (issue #1): install.sh must clean up .install.XXXXXX / .link.XXXXXX
# temps (from an interrupted atomic_install/atomic_link) on exit via a trap.
# Shell-level test.
test('emeraldwf-install-tmpcleanup', find_program('test_install_tmpcleanup.sh'))

# Minor (issue #1): install.sh must normalize LIBDIR/DATADIR to absolute paths
# (a relative libdir in a wayfire .pc would make the install land in ./lib).
# Shell-level test.
test('emeraldwf-install-libdir', find_program('test_install_libdir.sh'))

# ---- part 07: engine pane / engine module lifetime (findings 46/47) ----
# Regression for the "Round Top Left Corner" SEGV_MAPERR: append_engine's
# dlclose unmapped the engine .so (static-vendored libemeraldengine) while
# the pane kept callbacks into its code, and the per-.so globals made
# engine-pane edits unpersisted. Pins the upstream invariant: libemerald
# engine is ONE shared library — one live registry, one write path.
# Needs a display (skips when none); dlopens the installed engines.
emeraldwf_engine_pane = executable('emeraldwf-engine-pane',
    ['test_engine_pane.cpp'],
    include_directories: [src_inc, emerald_inc, emerald_test_inc],
    dependencies: emerald_test_extra + [gtest_main],
    link_with: [libemeraldengine])
test('emeraldwf-engine-pane', emeraldwf_engine_pane)

# ---- part 06: plugin .so packaging (linkage) ----
# Regression: a bare `#include <wlr/...>` from C++ mangles the wlroots
# symbol, wayfire's dlopen() of the plugin fails, and the plugin is
# silently never loaded (nothing painted). Checks the built plugin
# references every wlroots C function with C (unmangled) linkage.
check_so_linkage = find_program('check_so_linkage.py')
test('emeraldwf-linkage', check_so_linkage, args: [emeraldwf])
