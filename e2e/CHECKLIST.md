# emeraldwf — Manual QA / Integration Checklist

Assembled from parts 04/05 checklists + CSD, themer, and live-reload cases
(part 06). Run against a real wayfire session with `emeraldwf` enabled.

## Setup

1. Build + install:
   ```
   meson setup build && ninja -C build
   ninja -C build install          # plugin → libdir/wayfire, engines → libdir/emeraldwf/engines, metadata
   ```
2. Minimal `wayfire.ini` (only `emeraldwf`):
   ```ini
   [core]
   plugins = emeraldwf
   [emeraldwf]
   engine = truglass
   theme_dir = ~/.emerald/theme
   maximized_borders = true
   enable_shade = true
   ```
3. First run seeds `~/.emerald/settings.ini` + `~/.emerald/theme/` from the
   vendored defaults (part 02 `copy_from_defaults_if_needed`).

## SSD decoration

- [ ] **SSD xwayland window shows an emerald frame** — border, titlebar, and
      buttons laid out per the theme's `tobj_layout`.
- [ ] **Active/inactive states differ** — focused window renders `fs_active`;
      an unfocused window renders `fs_inactive`. Click between two SSD
      windows and confirm the chrome switches.
- [ ] **Maximized** — maximize a window (button or super+Up); with
      `maximized_borders=true` the emerald look (shadow inside the extents)
      is preserved; with `maximized_borders=false` the side/bottom borders drop.

## Buttons & titlebar (D11)

- [ ] **Close** — clicking the close button closes the window.
- [ ] **Maximize/Restore** — toggles tiling (tile all edges / untile).
- [ ] **Minimize** — unmaps the window.
- [ ] **Shade button** — folds the window into its titlebar.
- [ ] **Super-maximize** — toggles fullscreen.
- [ ] **Above/Stick/Help/Menu** — no-op (log once, no crash).
- [ ] **Double-click titlebar** — fires `settings.ini` `[titlebars]
      double_click_action` (0 shade, 1 maximize, 2 minimize, 3 send-to-back→no-op,
      4 close, 5 none).
- [ ] **Middle-click titlebar** — fires `middle_click_action` (same mapping).

## Resize

- [ ] **Resize from all 8 edges/corners** — drag each edge and corner; the
      window resizes on the correct side(s); corners resize both axes.

## CSD

- [ ] **CSD window has NO emerald frame** — e.g. `weston-terminal` (CSD) or
      firefox renders without an emerald chrome; margins `{0,0,0,0}`.
- [ ] **`always_decorate=app-id=...` forces double chrome** — set the
      `always_decorate` view matcher to a CSD app's id; a second emerald frame
      appears around it (by design, D6).
- [ ] **CSD shadow export (D12)** — a standalone shadow plugin shadows the CSD
      window using the `wf-decoration-shadow-margin` custom data (thickness =
      `shadow_pad`, default 0).

## Shade (part 05)

- [ ] **Scroll on titlebar** — scrolls the window down to shade, up to unshade
      (honors `settings.ini [titlebars] no_scroll_shade`; skipped when fullscreen).
- [ ] **Shade modifier** — hold `<super>+<ctrl>` and scroll to shade/unshade.
- [ ] **IPC** — bind `emeraldwf/shade_toggle` in wcm; it toggles the active window.
- [ ] **CSD shade** — shade works on CSD windows too (strip height =
      `csd_titlebar_height`); shadow custom data updates.
- [ ] **Shade + maximize coexist** — a maximized window shades without corrupting
      its tiling.

## Live reload (part 06)

- [ ] **`emerald-theme-manager` edits `~/.emerald/*` live, no restart** — change a
      color or layout in the themer; the change appears in wayfire within ~250ms
      (g_file_monitor on `settings.ini` + `theme.ini` + theme dir).
- [ ] **Engine switch via `emeraldwf/engine`** — change the option (wcm or ini) and
      the engine swaps live (dlopen/unload), no restart.
- [ ] **`theme_dir` change** — point at a new theme dir; monitors re-arm and the
      new theme loads.

## Themers / packaging

- [ ] **wcm loads `metadata/emeraldwf.xml` without warnings** — run `wcm`, open the
      emeraldwf page; all option groups (General / Shade) render.
- [ ] **themer file format stable** — after themer edits, `~/.emerald/settings.ini`
      and `~/.emerald/theme/theme.ini` are valid emerald ini (themer untouched).
- [ ] **Install layout** — `ninja -C build install` →
      `libdir/wayfire/libemeraldwf.so`, `libdir/emeraldwf/engines/lib{legacy,truglass,
      vrunner,oxygen,zootreeves,pixmap,line}.so`,
      `share/wayfire/metadata/emeraldwf.xml`. No `~/.emerald` writes at install
      (runtime only).

## E2E (scripted)

- [ ] `./e2e/run.sh` → PASS (steps 1–10).
- [ ] `./e2e/run.sh --make-goldens` regenerates `e2e/golden/*.png`.

## Known gaps (documented, non-blocking)

- Per-node cursor shapes: wayfire 0.11 has no per-node cursor API, so the node
  does not set resize/move cursor shapes (part 04 documented this).
- Scroll-to-shade E2E step is manual-only when `wtype`/`winput` are absent
  (run.sh marks it SKIPPED).
