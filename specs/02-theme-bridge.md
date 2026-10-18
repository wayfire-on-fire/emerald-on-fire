# Part 02 — Theme & engine bridge

**Read `specs/00-overview.md` first.** This part makes emerald's themes
render: it vendors emerald's code, loads emerald's ini configuration, and
exposes the pure `emeraldwf_render_frame()` that part 04 will composite.

## Goal

Emerald's entire theming stack (settings.ini + theme.ini + 7 theme engines +
button PNGs) works unchanged under a headless cairo context, with no X11
server, no libdecoration, no compiz.

## Deliverables

| File | Content |
|------|---------|
| `third_party/emerald/{include,engines,libengine,defaults}/` | Verbatim copy of the same directories from `/projects/contrib/emerald` (no edits; `Makefile.am` files may be dropped). Pin with a `third_party/emerald/VENDOR_SHA` file (git rev of emerald tree) so 06 can verify. |
| `compat/decoration.h` | Shim defining `typedef struct { int left, right, top, bottom; } decor_extents_t;` (matches GNOME libdecoration's definition) — the only symbol emerald's headers need from it. |
| `compat/libwnck/libwnck.h` | Shim `libwnck.h` defining `WnckWindow` (opaque) / `WnckWindowState` / `WnckWindowActions` — the ONLY source of the Wnck types (the build has no `libwnck-3.0` dependency). Enum values are a verbatim copy of libwnck 3.0's `window.h`, so the bridge↔engine bitmasks keep the exact bit positions the build always used. Resolved via `-I compat` ahead of any system include path. Choice recorded in `compat/README`. |
| `src/emerald-types.hpp` | Per overview §5.1, frozen: enums, `DecorContext`, `frame_width()/frame_height()` (D7). |
| `src/emerald-settings.hpp/.cpp` | `ThemeSettings` per overview §5.3. |
| `src/emerald-engine.hpp/.cpp` | `emerald_engine_load/unload` (port of emerald `src/engine_loader.c:32-84`, search order `~/.emerald/engines/lib<name>.so` then system dir param) and `emeraldwf_render_frame()` per overview §5.4. |
| `engines/`, `libengine/` (meson only) | Build rules: compile `third_party/emerald/engines/*.c` → 7 shared libs `lib<name>.so` (link glib, gtk-3, gdk-3, pangocairo, cairo; CFLAGS need `-I third_party/emerald/include -I compat`), compile `third_party/emerald/libengine/*.c` into a static lib `libemeraldengine` linked into the plugin (settings loaders `load_*_setting` used by `ThemeSettings`). |
| `tests/test_settings.cpp`, `tests/test_render.cpp`, `tests/make_goldens.cpp`, `tests/golden/*.png` | See below. |

### ThemeSettings loading spec

Port emerald's config loading:

- **settings.ini** (`~/.emerald/settings.ini`, defaults from
  `third_party/emerald/defaults/settings.ini`): `[buttons]`
  (enable_tooltips, use_button_fade*, button_fade_*, hover_cursor),
  `[titlebars]` (double_click_action, middle_click_action,
  no_scroll_shade), `[decorations]` (use_decoration_cropping, blur_type) →
  fill the corresponding `window_settings` fields (see the `load_*` calls in
  emerald `src/main.c` — grep `load_bool_setting(f,&ws->` /
  `load_int_setting(f,&ws->` / `load_string_setting(f,&ws->tobj_layout`).
  The default `tobj_layout` is `"IT::HNXC"` (emerald main.c:5960).
- **theme.ini** (`<theme_dir>/theme.ini`): call the engine's
  `load_engine_settings(GKeyFile*, window_settings*)` after engine load
  (that is how emerald does it: engine .so exports it, see
  `engine_loader.c:68`). Each engine also has its own per-theme
  `theme.ini` sections (e.g. `[zootreeves_settings]`).
- **Engine load**: `load_engine(engine_name, ws)` (emerald main.c:5984 uses
  `legacy`; our default from option D9 is `truglass`).
- **Button PNGs**: emerald's pixmap engine + main.c load
  `buttons.<name>.png` from the theme dir into
  `ws->button_surface[S_COUNT*B_COUNT]` via GdkPixbuf
  (emerald `src/main.c` grep `button_surface[` / `gdk_pixbuf`). Port that
  into `ThemeSettings` (it is theme data, so it belongs here, not in
  the engine).
- Missing files: fall back to `third_party/emerald/defaults/*` (emerald's
  `copy_from_defaults_if_needed()`, libengine/emerald.c:23-64 — port it).
- `ThemeSettings` is immutable after `load()` (shared_ptr<const> across
  decorators); loading must be thread-safe-free (all on main thread).

### emeraldwf_render_frame() spec

Reproduce emerald's draw path with a cairo image surface instead of an X
pixmap. Steps (all ports of emerald `src/main.c`, cited):

1. size = `frame_width/frame_height` (D7); create
   `cairo_image_surface(ARGB32, w, h)`;
2. `draw_shadow_background(d, cr)` — main.c (grep the function): gradient
   shadow inside the extents using `ws->shadow_radius/opacity/color/offset`;
3. `engine_draw_frame(d, cr)` via the dlopened engine (main.c:1798);
4. title text with halo — main.c:1807-1828 (PangoLayout,
   `fs->text_halo` stroke + `fs->text` fill, position
   `get_real_pos(ws, TBT_TITLE, d)`, vertical centering) — build a
   PangoLayout here (emerald kept it in `decor_t`; the bridge makes one per
   call and frees it);
5. icon — main.c:1830-1843 (16x16 clip/paint);
6. button regions background + glow — main.c:1845-2000 (port the
   `button_region_t` bg_surface logic; for the wayfire port the
   "copy bg into buffer" fast path can be simplified to always draw:
   correctness first, perf later);
7. button pixmaps — main.c (the `use_pixmap_buttons` draw block, grep
   `button_array`): paint `ws->button_surface`/glow into button rects,
   honoring `BUTTON_NOT_VISIBLE` (main.c:74) and states
   (active/inactive, `S_COUNT`).

`DecorContext` → `decor_t` mapping: the bridge constructs a real emerald
`decor_t` (original struct) copying all `DecorContext` fields,
`static_cast<WnckWindowState>(ctx.state)` (D4), `d->fs = ctx.fs`, leaves
X11 fields unused. `window_settings`/`frame_settings` come from
`ThemeSettings`.

## Tests

### `tests/test_settings.cpp` (headless)

- load `third_party/emerald/defaults/settings.ini` → assert:
  `enable_tooltips == true`, `use_button_fade == true`,
  `double_click_action == 0`, `middle_click_action == 3`,
  `use_decoration_cropping == true`, `blur_type == 0`,
  `tobj_layout == "IT::HNXC"`;
- `ThemeSettings::load` with a custom tmp theme dir: write a tiny
  `theme.ini` with `[truglass_settings] active_outer=#ff0000` and
  `titlebar_height=25` style keys → assert `ws()` fields updated;
- missing theme dir → defaults copied (assert `~/.emerald`-style fallback
  behavior by pointing at temp dirs, NOT at the real home);
- each engine name in {legacy, truglass, vrunner, oxygen, zootreeves,
  pixmap, line} loads (`emerald_engine_load` true).

### `tests/test_render.cpp` (headless, golden images)

For each engine × {active, inactive} × {windowed, maximized} at client
400×300 with default settings:

- call `emeraldwf_render_frame`, save PNG;
- compare against `tests/golden/<engine>_<state>_<mode>.png` (first-run
  goldens generated by `tests/make_goldens.cpp`, visually reviewed,
  committed).
- structural asserts (no goldens needed): surface size == D7 formula;
  surface non-empty (sum of pixels > 0); maximized rendering differs from
  windowed for engines with `enable_maximised_colors`.

Run: `meson test` (targets `emeraldwf-settings`, `emeraldwf-render`).

## Acceptance criteria

- All 7 engines compile from vendored sources unmodified;
- `ThemeSettings` round-trips the real default emerald theme;
- `emeraldwf_render_frame` produces goldens that a human can verify look
  like emerald themes (compare side-by-side with emerald on a compiz X
  session if one is available; otherwise verify structure);
- `emerald-types.hpp`, `emerald-settings.hpp`, `emerald-engine.hpp` match
  overview §5.1/5.3/5.4 verbatim (frozen for parts 03-06);
- `meson test` green for this part's tests.

## Out of scope

- Scene node, input, wayfire integration — part 04.
- Layout/hit-testing — part 03 (this part only fills theme fields;
  `DecorContext.tobj_*/button_region*` are filled by 03 before render).
  NOTE the dependency: `emeraldwf_render_frame` consumes those fields —
  until 03 lands, render tests fill them with a minimal layout (all items
  state 3) and goldens are structure-only. When 03 lands, 06 regenerates
  goldens.
- Fade animation (button fade steps) — part 04/05 (bridge draws
  static state; `button_states[]` is consumed).
