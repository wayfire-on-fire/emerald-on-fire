# Part 01 — Plugin skeleton, view lifecycle, margins

**Read `specs/00-overview.md` first.** This part is the foundation every
other part builds on. It produces a loadable wayfire plugin with correct
frame *geometry* (margins) and view lifecycle, but **no visual output yet**.

## Goal

A wayfire plugin `emeraldwf` that:

- registers itself, declares all options from overview D9,
- for every toplevel that should be decorated (per D6) stores an
  `emerald_decorator_t` as custom data,
- negotiates frame size via `get_margins()` exactly per overview D8,
- reacts to all the signals that later parts depend on (map, unmap,
  activate, geometry, tiled, fullscreen, title, app-id, decoration state).

Parts 04/05 will attach the node and shade transformer to this skeleton;
this part leaves clearly-marked stubs for them.

## Deliverables

| File | Content |
|------|---------|
| `meson.build` | Project scaffold: `shared_module('emeraldwf', ...)` deps: `wayfire` (>= 0.11, `WLR_USE_UNSTABLE`, `WAYFIRE_PLUGIN` defines like pixdecor's `src/meson.build`), `gtk3`, `pangocairo`, `cairo`, `glib-2.0`; test suite wired (gtest). Only `plugin.cpp` + `emerald-decorator.{hpp,cpp}` for now. |
| `src/emerald-decorator.hpp` | Header exactly per overview §5.2. |
| `src/emerald-decorator.cpp` | Constructor/destructor, `get_margins()`, `get_titlebar_height()`, `is_csd()`, `ctx()`; stubs for part-04/05 methods (assert(false) + comment). |
| `src/plugin.cpp` | `wf::plugin_interface_t` impl: options, signal wiring, per-view decorator create/remove. |
| `tests/test_margins.cpp` | gtest coverage of the margin math (below). |

### Plugin wiring (contract for later parts)

- `wf::plugin_interface_t::init()`:
  - options (D9) via `wf::option_wrapper_t`, callbacks:
    `engine`/`theme_dir` → mark theme dirty (part 02 provides the loader;
    here just store + log), `maximized_borders`/`maximized_shadows` →
    `recreate()` loop over decorated views (call the part-04 stub).
  - connect core signals:
    `wf::txn::new_transaction_signal` → for each toplevel object in the
    transaction: if decorated and no decorator data → create
    `emerald_decorator_t`, call `attach_node()` (stub), set
    `toplevel->pending().margins = deco->get_margins(toplevel->pending())`
    and expand geometry by margins when not fullscreen/tiled (exact logic
    of pixdecor `decoration.cpp:76-114`); if data exists → update margins.
  - `view_decoration_state_updated_signal` → remove decorator data if now
    CSD (shrink geometry, zero margins, exactly pixdecor
    `decoration.cpp:116-126` + `remove_decoration` at 690-706) else create.
  - `view_tiled_signal`, `view_app_id_changed_signal`, `view_title_changed_signal`
    → `update_view_decoration(view)` (re-evaluate matchers) + `on_theme_changed`
    hooks for later parts.
- `view_matcher_t` for `ignore_views` / `always_decorate` (pixdecor
  `decoration.cpp:63-64, 659-668`).
- `fini()`: remove all decorator data, zero margins, schedule objects.

### State mapping (D4)

`emerald_state_from_view()` (lives in this part, in
`emerald-decorator.cpp`): from `toplevel->pending()` + view flags →
`EmeraldWindowState` (needs `emerald-types.hpp` — if part 02 hasn't landed,
include the header from the overview §5.1 verbatim into your test build;
it is frozen).

## get_margins() — implementation spec

Port the semantics, not the code (emerald had no wayfire state struct):

```
if CSD (view->should_be_decorated() == false)      -> {0,0,0,0}
if state.fullscreen                                -> {0,0,0,0}
t = ws->win_extents.X + ws->extra_extents.X   (per side, from ThemeSettings
    — during part 01 development ThemeSettings may be a stub that loads the
    defaults from third_party/emerald/defaults; coordinate with part 02)
top = t + titlebar_height
maximized (state.tiled_edges != 0) && !maximized_borders
    -> {0, 0, 0, top}
else
    -> {t, t, t, top}
```

`titlebar_height` = `ws->titlebar_height` (settings.ini `[titlebars]` has no
height; height comes from the engine/theme — until part 02, use the value
emerald computes: font-based height like pixdecor `deco-theme.cpp:68-84` is
NOT emerald's; emerald reads it from theme.ini per engine. For part 01:
expose `set_titlebar_height(int)` and a `titlebar_height()` getter; default
17; part 02 will populate it from theme loading.)

## Tests (`tests/test_margins.cpp`)

Margin math must be testable without a live view: factor it as
`margins_for(const MarginsInput&)` where `MarginsInput` is a plain struct
(theme spaces, titlebar height, maximized_borders/shadows flags,
`wf::toplevel_state_t` copy, csd flag). Cases:

1. normal windowed state → `{t,t,t,t+tb}` (verify with default-theme
   values: left_space… use explicit inputs, e.g. win_extents 6/6/4/6,
   extra 0, tb 17 → `{6,6,6,23}`);
2. fullscreen → zero;
3. CSD → zero;
4. maximized + `maximized_borders=true` → `{t,t,t,t+tb}`;
5. maximized + `maximized_borders=false` → `{0,0,0,t+tb}`;
6. extra_extents nonzero → added per side.

Run: `meson test` (test target `emeraldwf-margins`).

## Acceptance criteria

- `meson setup build && ninja -C build && meson test -C build` green.
- Plugin builds into `$libdir/wayfire/emeraldwf.so`; loads under a local
  wayfire (`core/enabled_plugins` contains `emeraldwf`) without crash and
  without visible decorations (nothing is drawn yet — that is expected).
- `emerald-decorator.hpp` matches overview §5.2 verbatim (later parts
  compile against it).

## Out of scope

- Anything visual (node, textures, rendering) — part 04.
- Settings/theme loading implementation — part 02 (stub loader ok here).
- Shade, shadows, CSD custom data — part 05.
- wcm metadata, docs, E2E — part 06.
