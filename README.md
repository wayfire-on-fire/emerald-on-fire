# Emerald On Fire

`emeraldwf` is **Emerald On Fire** — it reproduces
what **emerald** does on Compiz: emerald's themes,
emerald's theme engines (truglass, vrunner, oxygen, zootreeves, pixmap,
legacy, line), emerald's ini-based configuration (`~/.emerald/settings.ini`,
`~/.emerald/theme/`), and emerald's button layout model — delivered through
wayfire's scene-graph decorator API (wayfire ≥ 0.11) instead of the X11/Compiz
decoration protocol.

Emerald's sources are vendored, unmodified, into `third_party/emerald/`; the
engines are compiled as `.so` files and loaded by name (dlopen) exactly like
emerald does. The only backend difference vs emerald is the surface: emerald
drew into X pixmaps, `emeraldwf` renders a per-toplevel cairo `ARGB32` image
surface that the scene node uploads as a texture.

## Build

Three ways to get the plugin — pick by what's on the box:

| Target | Toolchain | Output | Requires on host |
| ------ | --------- | ------ | ---------------- |
| `make binary` | Docker (ephemeral) | `dist/` (plugin + 7 engines + `libemeraldengine` + WCM metadata + theme editor) | Docker |
| `make build-local` | Native | `build/…` | wayfire ≥ 0.11, gtk3, meson ≥ 1.1, ninja |
| `wayfire-plugin install <url>` | WPM | managed prefix | wayfire + a C toolchain |

### Docker — `make binary` (recommended)

Requirements: **Docker only** — nothing else needs to be installed. `make
binary` builds the toolchain image ([Dockerfile](Dockerfile): ubuntu 24.04 +
wayfire ≥ 0.11 from source) and compiles the plugin in an ephemeral container,
landing the plugin, the 7 emerald engines, `libemeraldengine`, the WCM
metadata, and the theme editor in `dist/`.

```sh
make binary   # -> dist/wayfire/libemeraldwf.so, dist/emeraldwf/engines/, …
make test     # run the gtest suite in the toolchain container
```

### Native — `make build-local`

Requirements: **wayfire ≥ 0.11** (`wayfire-dev`), **gtk+-3.0**, **pangocairo**,
**gdk-pixbuf-2.0**, **x11**, **meson ≥ 1.1**, **ninja**.

```sh
make build-local   # -> build/src/libemeraldwf.so
```

### WPM

```sh
wayfire-plugin install https://github.com/wayfire-on-fire/emerald-on-fire
```

### Tests

Seven headless gtest suites (margins, settings, render goldens, layout,
render-node, shade, linkage) — `meson test -C build` (also runs in CI, along
with the Docker build, a native build against wayfire-from-source, and an
uncrustify code-style check).

## Install

### Dependencies

- wayfire `>= 0.11` (scene API)
- gtk+-3.0, gdk-3.0, pangocairo, cairo, glib-2.0, gdk-pixbuf-2.0, x11
- meson, ninja
- gtest (tests only; configure with `-Dtests=false` to skip)

No `libwnck` (or any other window-manager) dependency: the Wnck state/action
types the vendored emerald headers use come from the `compat/libwnck/` shim
(compile-time typedefs, see `compat/README`).

### Install (recommended)

```sh
./install.sh                # build + tests + install + seed ~/.emerald
./install.sh --skip-tests   # skip the test suite
./install.sh --no-seed      # don't touch ~/.emerald
```

The script does everything a fresh machine needs:

- configures the `build/` tree (meson) and builds;
- runs the test suite;
- installs the plugin, engines, and WCM metadata itself — the runtime `.so`
  files are placed **atomically** (temp file + `rename`): a running wayfire
  has them `mmap`'d, and an in-place copy (what `cp` and meson's installer
  do) would unmap live code pages and `SIGBUS` the session mid-install.
  Meson's own install rules exist for WPM (below) and are never run from
  `install.sh`;
- installs the WCM plugin icon into the wayfire icondir — falling back to
  `~/.local/share/wayfire/icons/` when the system dir isn't writable (WCM
  probes the user dir first);
- seeds `~/.emerald/settings.ini` and `~/.emerald/theme/` from the vendored
  emerald defaults **only if absent** (never overwrites an existing theme).

Manual equivalent:

```sh
meson setup build && ninja -C build
meson test -C build
# meson install is fine while wayfire is NOT running. While wayfire is
# running, use install.sh (atomic .so placement) instead — a plain
# `ninja install` truncates+writes the live .so files (SIGBUS).
ninja -C build install
```

### WPM (`wayfire-plugin install <url>`)

The project is WPM-compatible: `wayfire-plugin install <this repo>` runs
`meson setup --prefix <managed-prefix> -Dtests=false` (via
`wayfire-plugin.json`), compiles, and `meson install`s the plugin, the 7
engines, `libemeraldengine`, and the WCM metadata into the managed prefix
(`~/.local/share/wayfire/plugin-manager/install`) + the user metadata dir.
The engine directory the plugin resolves at runtime
(`<plugin-so-dir>/../emeraldwf/engines`) matches that layout, so the
WPM-installed set is self-contained. The tests suite is excluded
(`-Dtests=false`), so no gtest is needed for the install.

### (Note) window-zoom (winzoom) crash

If you run wayfire-plugins-extra's `window-zoom` plugin: versions up to
current master crash the whole session (use-after-free in `fini()`) when
the plugin is disabled after a zoomed window was closed — reproduced
deterministically under ASan in `results/09-winzoom-crash.md`. Reported
upstream (issue #351, fix PR #352 in WayfireWM/wayfire-plugins-extra).
A local patched build (detaches the transformer on `view_unmapped`)
lives in `patches/winzoom/` — it is **not** installed by `install.sh`
(winzoom is unrelated to emeraldwf); build and install it yourself only
if you use that plugin and have not yet picked up the upstream fix.

### Install layout

- `$(libdir)/wayfire/libemeraldwf.so` — the wayfire plugin
- `$(libdir)/emeraldwf/engines/lib{legacy,truglass,vrunner,oxygen,zootreeves,pixmap,line}.so`
  — the 7 emerald engines (the dlopen system_dir searched by the bridge)
- `$(datadir)/wayfire/metadata/emeraldwf.xml` — wcm metadata
- `plugin-emeraldwf.svg` — wcm plugin icon (system wayfire icondir, or
  `~/.local/share/wayfire/icons/` via `install.sh`)
- `emerald-theme-manager` — the theme editor (part 07): `/usr/bin` (or
  `~/.local/bin`), its desktop entry, the `default` system theme
  (`$(datadir)/emeraldwf/themes/default`, or `~/.local/share/emeraldwf/themes`),
  and the app icon — all via `install.sh` with user-dir fallbacks
- `~/.emerald/settings.ini` + `~/.emerald/theme/` — seeded by `install.sh`
  (also seeded at runtime on first run if absent, review R8: the theme from
  the bundled defaults — the installed copy at
  `<so-dir>/emeraldwf-defaults/theme` when present, the build tree's vendored
  defaults otherwise)

### Enabling the plugin (important)

Installing the `.so` + metadata is **not enough** — the plugin must also be in
wayfire's `core/plugins` list, which is what wayfire's plugin-loader actually
`dlopen`s at startup.

- **Via wcm:** open wcm → emeraldwf page. The emeraldwf metadata
  (`metadata/emeraldwf.xml`) is what makes the page appear and its options
  editable. Enabling the plugin means adding `emeraldwf` to `core/plugins`
  (wcm manages that list; the `[emeraldwf]` options block alone does **not**
  load the plugin — a common gotcha).
- **Via ini:** add `emeraldwf` to the `plugins =` line in `[core]`.

Also **remove `decoration`** (wayfire's built-in server-side decorator) from
`core/plugins` if it's present — otherwise it draws frames that mask emeraldwf.

**No wayfire restart is needed to enable the plugin.** Wayfire live-reloads
plugins: the `core/plugins` option has a runtime callback
(`plugin-loader.cpp` → `reload_dynamic_plugins()`) that `dlopen`s newly-listed
plugins and calls `init()`. So enabling emeraldwf in wcm (which appends it to
`core/plugins`) loads it immediately — the same behavior as pixdecor.

> If emeraldwf doesn't appear with an enable checkbox in wcm, **quit and
> relaunch wcm** so it re-scans `$(datadir)/wayfire/metadata/` (it builds its
> plugin list at startup). The `[emeraldwf]` options block alone does not load
> the plugin — the enable checkbox (which edits `core/plugins`) does.

## Reloading

- **Enabling/disabling the plugin** (`core/plugins`) — **live**, no wayfire
  restart (wayfire's `core/plugins` runtime callback).
- **Theme/settings changes** (`~/.emerald/settings.ini`, `theme.ini`, the theme
  dir) and **`emeraldwf/*` option changes** — **live**, no restart. The plugin
  runs `g_file_monitor` on the emerald ini files (250 ms debounce) and reacts
  to wayfire option callbacks. This is what makes the bundled theme editor
  (`emerald-theme-manager`, part 07) work without a restart.
- **Plugin code changes** (a rebuilt `libemeraldwf.so`) — toggle the plugin
  off/on in wcm (the runtime reload re-`dlopen`s it), or restart wayfire.

> **Live-reload caveat (wayfire):** wayfire watches its config file with an
> inotify `IN_CLOSE_WRITE` watch bound to the file's **inode at boot**. wcm
> saves config files atomically (write-temp + rename), which gives the file a
> **new inode** — so wayfire's file watch goes stale and subsequent edits are
> not detected (no reload, no plugin load). This is a wayfire bug, not an
> emeraldwf defect. If a wcm change (e.g. enabling emeraldwf) doesn't take
> effect live, **restart wayfire**. The `core/plugins` list itself is live-reloaded
> by wayfire when a reload *does* fire; the problem is the stale inode watch
> preventing the reload from firing at all.

## Usage

Minimal `wayfire.ini` (only `emeraldwf`):

```ini
[core]
plugins = emeraldwf

[emeraldwf]
engine = truglass
theme_dir = ~/.emerald/theme
maximized_borders = true
maximized_shadows = true
enable_shade = true
```

### `~/.emerald` layout (D10)

```
~/.emerald/
├── settings.ini          # emerald-format compatibility file: emeraldwf
│                         # reads nothing from it (all former global keys
│                         # moved to WCM, part 08). Kept seeded so X11
│                         # emerald and .emerald packages keep working
└── theme/
    ├── theme.ini          # engine sections + button PNG refs
    └── *.png              # buttons.<btype>.png, pixmaps.*, etc.
```

This is identical to emerald's layout. The Emerald Theme Manager (part 07)
edits `theme.ini` only; every global setting lives in WCM (part 08).
Because of the live reload (below), every theme edit applies with no
restart and no signal: the themer just writes `~/.emerald/theme/` and the
plugin's poller picks it up (~1 s).

### Theme install

Copy an emerald theme into `~/.emerald/theme/` (the `theme.ini` + PNGs) —
or open `emerald-theme-manager` and Import a `.emerald` package / pick a theme
from the list (it does the copy and live-applies it). The engine is
chosen by the `[engine]` section in `theme.ini`; the WCM `engine` option
is an override: the default uses the theme's engine, a different value
forces that engine with emerald's default theme (part 07). Engines are
loaded from
`~/.emerald/engines/` first, then the installed
`$(libdir)/emeraldwf/engines/`.

### Downloading emerald themes

Emerald's theme collection lives at
[compiz/emerald-themes](https://gitlab.com/compiz/emerald-themes) — each theme
ships as a **`.emerald` package** (a gzip tarball, e.g.
`Numix.emerald` → `theme.ini` + button PNGs). These must be **unpacked** —
a symlink of a `.emerald` file does not work, because the theme scan
(`theme_scan_dir`) looks for a *directory* named `<name>` containing
`theme.ini`, not a package file.

Clone and unpack all themes into the writable user tier
(`~/.emerald/themes/<name>/`, the tier the themer lists **without** the `*`
read-only prefix, so Save/Delete are available):

```sh
git clone https://gitlab.com/compiz/emerald-themes.git
mkdir -p ~/.emerald/themes
cd emerald-themes/themes
for f in *.emerald; do
  name="${f%.emerald}"
  mkdir -p ~/.emerald/themes/"$name"
  tar -xof "$f" -C ~/.emerald/themes/"$name"
done
```

This mirrors the upstream `import.sh` (which does the same unpack, then
deletes the packages); the loop above keeps the clone intact. Verify each
theme unpacked with a `theme.ini`:

```sh
for d in ~/.emerald/themes/*/; do
  [ -f "$d/theme.ini" ] || echo "MISSING theme.ini: $d"
done
```

Open `emerald-theme-manager` — every theme is now in the list; select one and
it copies the files into `~/.emerald/theme/` and live-applies (no restart).
To activate one from the shell instead:

```sh
name=Numix
rm -rf ~/.emerald/theme && mkdir -p ~/.emerald/theme
tar -xof emerald-themes/themes/$name.emerald -C ~/.emerald/theme
```

(For a one-off: the WCM `Import...` button also accepts a single
`.emerald` file and unpacks it for you.)

## Options (wcm: `emeraldwf`)

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `engine` | string | `no_override` | **Engine Override.** "- No Override -" (default) = the engine the selected theme pins (theme.ini; change it in `emerald-theme-manager`, Frame Engine tab). A value that differs from the theme's engine forces that engine with **emerald's default theme** (its own stock look) — a theme's colors/settings are engine-specific. |
| `theme_dir` | string | `~/.emerald/theme` | Theme dir (theme.ini + PNGs). Core settings live in `~/.emerald/settings.ini`. Themes are edited/imported with the Emerald Theme Manager app. |
| `maximized_borders` | bool | `true` | Draw the border on maximized (tiled) windows. `false` crops the border off all sides (the window hugs the screen edges; the top keeps the titlebar). |
| `maximized_shadows` | bool | `true` | Keep shadow space around maximized windows. `false` crops the shadow spread off all sides. Both `false` → titlebar-only strip, no gaps (emerald's decoration cropping). |
| `ignore_views` | view matcher | `none` | Disable decoration for matching windows. |
| `always_decorate` | view matcher | `none` | Force decoration for matching windows (e.g. double-chrome a CSD window). |
| `enable_shade` | bool | `true` | Enable scroll-to-shade (titlebar scroll, modifier scroll, shade button, IPC). |
| `shade_modifier` | key | `<super>+<ctrl>` | Hold + scroll to shade/unshade. |
| `shade_toggle` | activator | *(empty)* | IPC binding to toggle shade on the active window. |
| `shade_duration` | animation | `1000ms sigmoid` | Shade animation duration. |
| `csd_titlebar_height` | int | `32` | Shade strip height for CSD windows. |
| `shadow_pad` | int | `0` | Non-zero shadow thickness exported for CSD windows (D12). |
| `double_click_action` | string | `shade` | **Double-click Action.** Titlebar double-click: `shade` (default), `maximize`, `minimize`, `send_to_back`, `close`, `none`. |
| `middle_click_action` | string | `send_to_back` | **Middle-click Action.** Titlebar middle-click: same values as `double_click_action`. |
| `no_scroll_shade` | bool | `false` | Prevent scrolling over the titlebar from shading/unshading the window (part 08). |
| `use_decoration_cropping` | bool | `false` | Maximized windows keep only the titlebar strip; overrides `maximized_borders`/`maximized_shadows` while on (part 08, finding 45). |
| `use_button_fade` | bool | `true` | Fade the titlebar button highlight in/out on hover (part 08). |
| `use_button_fade_pulse` | bool | `true` | Hovered button highlight keeps breathing between full and the pulse minimum opacity (part 08). |
| `button_fade_total_duration` | int | `250` | Button fade in/out time, ms (part 08). |
| `button_fade_step_duration` | int | `50` | One fade step, ms; the total is split into steps of this size (part 08). |
| `button_fade_pulse_wait_duration` | int | `0` | Pulse hold at full highlight between breaths, ms (part 08). |
| `button_fade_pulse_min_opacity` | int | `0` | Lowest pulse highlight opacity, % (also sets the pulse cycle length — upstream emerald's design) (part 08). |

Everything else (buttons, titlebar colors, borders, shadow radius, engine
parameters) stays in the emerald ini files, under `~/.emerald/theme/`
(`theme.ini`, edited via `emerald-theme-manager`). The titlebar click
actions and the global behavior settings (no-scroll-shade, decoration
cropping, button fade/pulse + durations) are global settings WCM defines;
`emeraldwf` reads **no** keys from `~/.emerald/settings.ini` at all
(part 08, one setting, one editor; `install.sh` migrates existing values
once).

## CSD behavior (D6)

Self-decorating (CSD) windows — e.g. Firefox on Wayland — are skipped: no
emerald frame, margins `{0,0,0,0}`. Set `always_decorate=app-id=...` to force a
second emerald chrome around a CSD window (double chrome, by design).

Shade works on CSD windows too (the transformer is attached directly to the
client surface with `csd_titlebar_height`), and a standalone wayfire shadow
plugin can shadow CSD/shaded windows via the `wf-decoration-shadow-margin`
custom data (D12; thickness = `shadow_pad`).

## Live settings reload (part 06)

`emeraldwf` monitors `~/.emerald/settings.ini`, `<theme_dir>/theme.ini`, and the
theme directory itself with `g_file_monitor`. On change (debounced 250 ms) it
rebuilds the `ThemeSettings`, swaps the engine (`emerald_engine_load/unload`),
and re-renders every decorator — no restart, no SIGUSR1. This is what makes
the bundled theme editor (`emerald-theme-manager`, part 07) work live.

## E2E

```sh
./e2e/run.sh               # full scripted E2E (needs wayfire, x11-apps, ImageMagick)
./e2e/run.sh --make-goldens # regenerate e2e/golden/*.png
```

The suite starts wayfire headless (Xvfb + `LIBGL_ALWAYS_SOFTWARE=1`), maps SSD
(xterm via xwayland) and CSD (`weston-terminal`) windows, screenshots with
`xwd`/`grim`, compares against `e2e/golden/` with ImageMagick `-fuzz 5%`, and
checks the vendored emerald rev (`VENDOR_SHA`). See `e2e/CHECKLIST.md` for the
manual QA checklist (buttons, resize, shade, themer, reload, packaging).

## Testing (headless)

```sh
meson test -C build
```

Seven headless gtest suites: margins, settings, render (goldens),
layout, render-node, shade, linkage (the plugin `.so` must reference
wlroots C symbols un-mangled). One display-gated suite: engine-pane
(engine module lifetime + shared settings registry — findings 46/47;
skips when no display). Render goldens live in `tests/golden/`
(`<engine>_<state>_<mode>.png`, 28 files); regenerate with
`./build/tests/make_goldens`.

## Known gaps

- **Per-node cursor shapes** — wayfire 0.11 has no per-node cursor API, so the
  node does not set resize/move cursor shapes (part 04), and the emerald
  "Button Hover Cursor" setting has no effect (dropped from the theme
  editor, finding 41).
- **Window-switcher decoration, titlebar blur, Compiz window-menu/force-quit
  atoms** — out of scope (overview D13).
- **`emeraldwf_render_frame` title text** — the bridge renders a placeholder
  title ("emerald") since `DecorContext` carries no name field; the live node
  (part 04) supplies the real title.
- Scroll-to-shade E2E is manual-only when `wtype`/`winput` are absent.

## License

GPL-2.0-or-later (see `LICENSE`). The WCM plugin icon
(`metadata/plugin-emeraldwf.svg`) is emerald's theme-manager gem artwork from
[compiz/emerald](https://gitlab.com/compiz/emerald) (GPLv2) — resized with a
32×32 canvas; the engine code under `third_party/emerald/` is likewise
GPLv2.
