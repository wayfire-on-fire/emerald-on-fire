# Part 04 — Integration checklist (manual wayfire QA)

Run: `wayfire` with only `emeraldwf` enabled (test ini), default theme.

## Setup

1. Build and install:
   ```
   meson setup build && ninja -C build
   # wayfire picks up the plugin from the install dir (or run from source tree)
   ```
2. Minimal `wayfire.ini` (test ini):
   ```ini
   core {
       plugin-path = <install>/lib/wayfire
   }
   emeraldwf {
       engine = legacy
       theme_dir = ~/.emerald/theme
       maximized_borders = true
       enable_shade = true
   }
   ```
3. Start wayfire, then open an SSD window (e.g. an xwayland app, or a GTK
   app with server-side decorations) and a CSD window (e.g. `weston-terminal`
   / firefox).

## Checklist

- [ ] **SSD xwayland window shows an emerald frame** — border, titlebar, and
      buttons laid out per the theme's `tobj_layout`.
- [ ] **Active/inactive states differ** — the focused window renders with the
      active frame settings (`fs_active`); an unfocused window renders with
      `fs_inactive`. Click between two SSD windows and confirm the chrome
      switches.
- [ ] **Double-click titlebar fires `double_click_action`** (default
      `double_click_action` 0 = shade — the original emerald default is
      SHADE, not maximize; set `[titlebars] double_click_action = 1` in
      theme.ini to maximize on double-click). Confirm the double-click
      action from `settings.ini` `[titlebars] double_click_action` fires
      (0 shade, 1 maximize, 2 minimize, 3 no-op, 4 close, 5 none).
- [ ] **Middle-click titlebar** fires `middle_click_action` from
      `settings.ini` (same mapping).
- [ ] **Close / Minimize / Maximize buttons work** — clicking each button on
      the titlebar performs the matching WM action (close, minimize,
      tile/un-tile).
- [ ] **Resize from all 8 edge directions** — drag each edge and each corner
      of the frame; the window resizes on the correct side(s). Corners resize
      both axes.
- [ ] **Maximize / Minimize / Fullscreen via buttons** — the maximize button
      toggles tiling; the super-maximize (fullscreen) button toggles
      fullscreen; minimize unmaps.
- [ ] **CSD window has NO emerald frame** — e.g. `weston-terminal` (CSD) or
      firefox renders without an emerald chrome.
- [ ] **`always_decorate=app-id=...` forces double chrome** — set the
      `always_decorate` view matcher to a CSD app's id; confirm a second emerald
      frame appears around it (by design).
- [ ] **Theme switch** — change the `engine=` option (e.g. `truglass`) and
      restart wayfire; the other engine renders the frame.
- [ ] **Scroll-to-shade (hook only, part 05)** — scrolling on the titlebar is a
      no-op until part 05 lands (the node calls `deco->toggle_shade()`, which
      is a part-05 stub). Confirm no crash; the transformer arrives in part 05.

## Notes

- The frame's visual output must match the emerald goldens from part 02 (same
  renderer — `emeraldwf_render_frame`). Compare a screenshot of the emeraldwf
  frame against `tests/golden/<engine>_<state>_<mode>.png`.
- Known gap: per-node cursor shape (wayfire 0.11 has no per-node cursor API).
  The node does not set resize/move cursor shapes; this is documented and does
  not block the part.
