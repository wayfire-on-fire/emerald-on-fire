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
    # emerald-config-sync.cpp (finding 24): pure ini→option sync, headless
    # (GKeyFile + wf::config::option_t only, no wayfire core symbols).
    ['test_settings.cpp', '../src/emerald-config-sync.cpp'],
    include_directories: [src_inc, emerald_inc, emerald_test_inc],
    # wayfire: wf-config (option_t / from_string) for the sync tests — the
    # wayfire dep has no Libs of its own, so it does not pull the core lib.
    dependencies: emerald_test_deps + [wayfire],
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
