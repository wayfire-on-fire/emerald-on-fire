# Part 06 — Integration, live reload, E2E, packaging, docs

**Read `specs/00-overview.md` first, plus all part files.** Final assembly:
make the sum of the parts *usable* as a complete emerald for wayfire —
installed, configurable via wcm, live-reloading themes, and proven by an
automated E2E suite.

## Goal

`emeraldwf` ships as a normal wayfire plugin, is configurable end-to-end
(ini + wcm + emerald-theme-manager), reloads theme changes live, and the
whole stack is verified by a scripted E2E run.

## Deliverables

| File | Content |
|------|---------|
| `meson.build` (root + `src/`) | Final: install `emeraldwf.so` to wayfire plugin dir, install 7 engine `.so`s to `$(libdir)/emeraldwf/engines/` (the dlopen system dir from part 02 — wire that path in), install `metadata/emeraldwf.xml` to wayfire metadatadir (pattern: pixdecor `metadata/meson.build`). |
| `metadata/emeraldwf.xml` | wcm metadata for every option in overview D9 (groups: General / Shade / Views; `<desc>` values for `engine` listing all 7 engines, `titlebar`-style options per emerald ini where we exposed them; mirror pixdecor `metadata/pixdecor.xml` format). |
| `src/plugin.cpp` | Live settings reload: `g_file_monitor` on `settings.ini` + `<theme_dir>/theme.ini` (+ engine dir changes for user engines) → rebuild `ThemeSettings`, `set_theme_dirty()` on all decorators, swap engine via `emerald_engine_load/unload`. Debounce 250ms. This replaces emerald's SIGUSR1/dbus reload (emerald main.c:5980, 6042, 4989) — emerald-theme-manager now works with no restart, no signal. |
| `e2e/wayfire.ini` | Minimal wayfire config: only `emeraldwf` (+ `wobbly`? no — keep minimal: `emeraldwf` alone, default workspaces). |
| `e2e/run.sh` | Scripted E2E (below). |
| `e2e/golden/` | Golden screenshots (SSD window, active+inactive, maximized). |
| `e2e/CHECKLIST.md` | The full manual QA checklist assembled from parts 04/05 checklists + CSD + themer + reload cases. |
| `README.md` | Install (meson, deps list), usage (wayfire.ini snippet, `~/.emerald` layout, theme install = copy emerald themes), options table, themer integration, CSD behavior + `always_decorate`, shade binding, E2E howto, known gaps (from parts' documented gaps, e.g. cursor shapes if 04 skipped them). |
| `third_party/emerald/VENDOR_SHA` check | E2E asserts vendored emerald sources match the pinned rev (guard against accidental engine edits). |

## E2E spec (`e2e/run.sh`)

Environment: wayfire under a headless GPU (`LIBGL_ALWAYS_SOFTWARE=1`
lavapipe; `wayfire` with `-c e2e/wayfire.ini`), display via a virtual
wayland (weston headless backend, or `weston --backend=headless` if
already in use; document the exact recipe in the script header).

Steps (each = numbered section, fail → non-zero exit + message):

1. **start**: launch wayfire; wait for compositor ready (`wayfire --help`
   smoke + `wlr-randr`/`weston-info` equivalent, or poll a known
   wayfire debug line).
2. **ssd window**: map an SSD test window — xwayland app (`x11-apps
   xterm` / `x11-apps clocktest` via xwayland) so it has a server-side
   frame; wait 500ms; `grim screenshot e2e/out/ssd-active.png`;
   `convert -crop` the window region (coords via wayfire debug log or
   `xwininfo -root` through xwayland) → compare against
   `e2e/golden/ssd-active.png` with ImageMagick `-fuzz 5%` (font
   hinting tolerance).
3. **inactive state**: map a second SSD window (activates it), screenshot
   the first → golden `ssd-inactive.png`.
4. **maximized**: maximize the active window (via xdotool through
   xwayland: `xdotool key super+Up` is NOT universal — use
   `_NET_WM_STATE` via `xprop -f _NET_WM_STATE` from the x11-apps side,
   or wayfire's `wm-actions` if enabled; document chosen method),
   screenshot → golden.
5. **csd**: map a CSD wayland client (`weston-terminal`); screenshot its
   region → assert it does NOT contain emerald frame pixels (compare
   against a plain-compositor golden taken with `emeraldwf` disabled —
   run wayfire twice, once without the plugin; diff must be ~zero).
6. **always_decorate**: set `always_decorate=app-id=weston-terminal` in a
   second config, restart, screenshot → golden shows emerald frame AROUND
   the CSD window (double chrome).
7. **actions**: via xdotool over xwayland: click close button region →
   window gone; open new, click max → maximized; min → minimized
   (window removed from view); assert each via window list
   (`weston-info` / xdotool search).
8. **shade**: scroll on titlebar (synthesize pointer axis events — use
   `wtype`/`winput` if available; else document as manual-only and mark
   the step SKIPPED), assert window height collapsed (screenshot diff vs
   unshaded golden > threshold).
9. **live reload**: change `~/.emerald/settings.ini`
   (`double_click_action` → `use_button_fade=false` visible effect, plus a
   theme color change in `theme.ini`), sleep 1s, screenshot → differs from
   step-2 golden beyond fuzz threshold (proves reload path works).
10. **vendor check**: `git -C third_party/emerald diff --quiet` / compare
    `VENDOR_SHA`.

`run.sh` must be idempotent and self-contained: `./e2e/run.sh` → PASS/FAIL
summary; golden regeneration mode `./e2e/run.sh --make-goldens`.

## Packaging contract

- `ninja -C build install DESTDIR=...` →
  `usr/lib/wayfire/emeraldwf.so`,
  `usr/lib/emeraldwf/engines/lib{legacy,truglass,vrunner,oxygen,zootreeves,pixmap,line}.so`,
  `usr/share/wayfire/metadata/emeraldwf.xml` (dir follows wayfire's
  `pkgconfig: metadatadir`, as pixdecor does);
- no `~/.emerald` writes at install (runtime only, per D10);
- deps declared in meson: `wayfire>=0.11`, `gtk+-3.0`, `gdk-3.0`,
  `pangocairo`, `cairo`, `glib-2.0`, `gtest` (test-only); optional
  `libwnck-3.0` (compile-time headers only, see part 02).

## Tests / verification

- full `meson test` green (all parts);
- `e2e/run.sh` PASS on a local box;
- `metadata/emeraldwf.xml` loads in wcm without warnings (manual: run
  `wcm`, open emeraldwf page — checklist item);
- themer E2E (manual, CHECKLIST): run `emerald-theme-manager` (build from
  `/projects/contrib/emerald`), change a color + layout, verify live in
  wayfire (part 06's file monitor), verify file contents on disk match
  emerald format (themer untouched, format stable).

## Acceptance criteria (project-level, = overview §7)

1. `emeraldwf` loads under wayfire; SSD windows decorated with emerald
   themes, pixel-consistent with emerald-on-compiz for the same theme.
2. CSD windows frameless by default; `always_decorate` forces.
3. All emerald theme buttons work per D11; double/middle-click per
   settings.ini; scroll-to-shade works (SSD and CSD).
4. `emerald-theme-manager` edits `~/.emerald/*` live, no restart.
5. `meson test` green; `e2e/run.sh` green; README complete.

## Out of scope

- New emerald features (blur, switcher, window menu) — confirmed dropped
  in overview D13.
- Wayland protocols beyond existing (no new xdg extensions).
