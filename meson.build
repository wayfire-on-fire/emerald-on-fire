project(
    'emeraldwf',
    'c',
    'cpp',
    version: '0.1.0',
    license: 'GPL-2.0-or-later',
    meson_version: '>=1.1.0',
    default_options: [
        'cpp_std=c++17',
        'c_std=gnu11',
    ],
)

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

add_project_arguments(['-DWLR_USE_UNSTABLE'], language: ['cpp', 'c'])
add_project_arguments(['-DWAYFIRE_PLUGIN'], language: ['cpp', 'c'])
# Review F2: wayfire upstream builds with -Wall -Wextra; the plugin lives
# inside the compositor, so match its warning bar (the vendored emerald C
# engine is out of scope — C++ only; the vendored C targets carry
# c_args: ['-w'] — Review S3 — so `ninja` output is warning-clean).
add_project_arguments(['-Wall', '-Wextra', '-Wshadow'], language: 'cpp')
add_project_link_arguments(['-rdynamic', '-fPIC'], language: ['cpp'])

wayfire = dependency('wayfire', version: '>=0.11.0')
glib = dependency('glib-2.0')
gdk = dependency('gdk-3.0')
gtk = dependency('gtk+-3.0')
cairo = dependency('cairo')
pango = dependency('pango')
pangocairo = dependency('pangocairo')
gdk_pixbuf = dependency('gdk-pixbuf-2.0')
x11 = dependency('x11')
# include order: compat first so <decoration.h> AND <libwnck/libwnck.h>
# resolve to our shims (the WnckWindowState/WnckWindowActions typedefs the
# vendored emerald headers need — no libwnck-3.0 dependency, see
# compat/libwnck/libwnck.h).
emerald_inc = include_directories('compat',
                                  'third_party/emerald/include')
src_inc = include_directories('src')

emerald_deps = [glib, gdk, gtk, cairo, pango, pangocairo, x11]

# ---- emerald engine shared libraries (vendored, unmodified) ----
engine_names = ['legacy', 'line', 'vrunner', 'truglass',
                'oxygen', 'pixmap', 'zootreeves']
subdir('libengine')
subdir('engines')

subdir('src')

# The test suite (gtest). WPM's default flow configures without it
# (-Dtests=false, see wayfire-plugin.json) so a machine without gtest
# can still install the plugin.
if get_option('tests')
    gtest = dependency('gtest', required: true)
    # The margin test compiles emerald-decorator.hpp, which pulls in
    # emerald-types.hpp -> emerald.h (X11/GTK headers). Add the wayfire
    # dependency (for toplevel.hpp) and the GTK stack (for emerald.h) to
    # the test deps.
    gtest_main = dependency('gtest_main')
    emerald_test_extra = [gtest, wayfire, gtk, glib, cairo, pango, pangocairo]
    subdir('tests')
endif
# Part 07: emerald-theme-manager (theme editor) — standalone binary, linked
# against libemeraldengine.
subdir('themer')

# Part 06: wcm metadata → wayfire metadatadir (pixdecor metadata pattern).
install_data('metadata/emeraldwf.xml',
    install_dir: wayfire.get_variable(pkgconfig: 'metadatadir'))

# Part 07: the system theme (emerald defaults) is installed by
# install.sh, not here — /usr/share is not writable without root on
# this system (same reason as the WCM icon above). install.sh falls
# back to ~/.local/share/emeraldwf/themes, which the themer scans as
# the per-user "system" tier.

# NOTE: the WCM plugin icon (metadata/plugin-emeraldwf.svg) is installed by
# install.sh, not here: the system icondir may not be writable without
# root, and install.sh falls back to ~/.local/share/wayfire/icons (which
# WCM probes first) — a meson rule would abort the whole `ninja install`
# in that case.
