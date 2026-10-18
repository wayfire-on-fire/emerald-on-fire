# Part 03 — Layout & input model (pure)

**Read `specs/00-overview.md` first.** This part is pure geometry: it ports
emerald's titlebar layout engine and hit-testing with **zero wayfire
dependency**, so it is fully unit-testable in isolation. It runs in
parallel with part 02 (it only needs `emerald-types.hpp`, overview §5.1).

## Goal

Given emerald's theme settings (spaces, radii, `tobj_layout` string,
button surfaces) and a client size, produce exactly the same titlebar
object placement emerald computed on compiz — and answer "what is under
the pointer" (move / resize edge / which button).

## Deliverables

| File | Content |
|------|---------|
| `src/emerald-layout.hpp` | Per overview §5.5 (frozen). |
| `src/emerald-layout.cpp` | The port. |
| `tests/test_layout.cpp` | gtest suite (below). |

## Implementation spec

Ports from emerald `src/main.c` (all geometry math is plain ints/rects;
no X11, no libwnck — only `window_settings`, `decor_extents_t`,
`button_region_t`, `B_T_COUNT`/`TBT_COUNT` from `emerald.h`):

1. **`build()`** — port `layout_title_objects()` (main.c:2954-3060):
   - parse `ws->tobj_layout` (e.g. `"IT::HNXC"`): 3 zones (left, center,
     right) split by `:`; `(N)` = N px extra space; unknown chars end a
     zone; center zone is centered: port the `tobj_pos[1]` formula
     (main.c:2996-2998) exactly;
   - `get_title_object_width()` (main.c: grep; per-object widths from
     button surface sizes / icon 16 / title width from
     `ws->text_height`-based max name width — port
     `max_window_name_width`'s width contribution; the wayfire title text
     width comes from a Pango measurement — accept it as input
     `build(..., int title_text_width)`; part 04 measures it, tests pass
     explicit values);
   - set `ctx.tobj_pos/size/item_pos/item_state/item_width`;
   - port `update_button_regions()` (main.c:1653-1767): per button,
     base rect (`ws->button_offset`, `button_hoffset`, icon sizes
     `c_icon_size[B_T_COUNT]`, `button_glow_surface` sizes,
     `use_button_glow`/`use_button_inactive_glow`, `use_decoration_cropping`
     clamp, `min_drawn_buttons_region`), into both
     `ctx.button_region[]` and `ctx.button_region_inact[]`;
   - honor `BUTTON_NOT_VISIBLE` (main.c:74): state 3 or
     `!(actions & button_actions[btype])` → skip (state stays 3,
     rects invalid `-100` sentinel as emerald uses).
2. **Hit testing** (frame coordinates, origin = frame top-left):
   - `hit_button(x, y, active)`: rect containment over
     `button_region/[]_inact` — glow rect if
     `use_button_glow(+inactive) && active` else base rect (mirror
     main.c:1861-1881 logic); return `btype` (enum `btypes`,
     include/titlebar.h) or -1; buttons of state 3 never hit;
   - `resize_edges(x, y)`: port the 3×3 cursor grid math
     (main.c:309-370 `update_window_extents` + `pos[3][3]`): edges are
     `ws->win_extents + extra_extents` bands around the client rect
     (plus `left_corner_space` etc.); return `WLR_EDGE_LEFT|TOP|…`
     bitmask, 0 in the middle 3×3 (the middle band is move, not resize);
     top band stops at `t_ext_nott` (main.c:324: the titlebar itself is
     move, not resize);
   - `in_titlebar(x, y)`: inside the titlebar band
     (`top_space - win_extents.top … titlebar area`);
   - `cursor_region(x, y)`: 0..8 grid index (port `ws->pos[3][3]`
     region lookup main.c:540-560) for cursor shape in part 04.
3. **`double_click_action()` / `middle_click_action()`** — read from
   `window_settings` (loaded by part 02 from settings.ini
   `[titlebars]`).

No state beyond what's in `DecorContext` + `ThemeSettings`; the class is
re-entrant, copy-cheap (part 04 keeps one per decorator).

## Tests (`tests/test_layout.cpp`)

Use a fixture ThemeSettings loaded from
`third_party/emerald/defaults` (settings.ini + theme/theme.ini, engine
`legacy` — deterministic spaces: left/right 6, top 4, bottom 6,
titlebar_height from theme.ini, `tobj_layout = "IT::HNXC"`).

1. **layout parse**: default `"IT::HNXC"` — assert left zone = icon+title
   (I+T present, states 0/1… per zone), right zone = H N X C in order;
   `tobj_item_state` matches presence; swap layout to `"CNX:IT:M"` and
   assert zones re-placed;
2. **`(N)` spacing**: layout `"(10)C"` adds 10px before close (assert
   `tobj_item_pos[B_T_CLOSE]` delta == 10);
3. **centering**: with a narrow client (300px) and wide title, center zone
   x follows the `MAX((right_pos + left_size - center_size)/2, 0)` formula;
4. **visibility**: actions mask without MINIMIZE → minimize button state 3
   and its `hit_button` returns -1;
5. **hit_button**: point inside close-button base rect → B_T_CLOSE;
   point inside its glow (with `use_button_glow=true`) → B_T_CLOSE; point
   on title → -1; inactive set differs from active set when glow disabled
   for inactive;
6. **resize_edges**: point 2px into left border → WLR_EDGE_LEFT; 2px top of
   client below titlebar → WLR_EDGE_TOP; corner → BOTH; point in titlebar
   → 0 (move); point in client → 0;
7. **cursor_region**: 9 regions return 0..8 as the 3×3 grid;
8. **maximized**: build with maximized state → corners not rounded is an
   engine concern (not here), but resize bands use the same extents (assert
   unchanged edges);
9. degenerate: client 1×1 (emerald clamps to 1), zero-width zones.

Run: `meson test` (target `emeraldwf-layout`).

## Acceptance criteria

- `emerald-layout.hpp` matches overview §5.5 verbatim;
- no wayfire/GL headers included (pure: emerald types + WLR_EDGE constants
  from `<wayfire/geometry.hpp>`? — NO: to stay headless-free-of-wayfire,
  use plain `#define`-free uint32_t masks with the numeric WLR_EDGE values
  1/2/4/8 documented; part 04 casts);
- all tests green; layout output for the default theme is byte-stable
  (deterministic across runs).

## Out of scope

- Rendering anything — part 02/04.
- Pointer grab semantics, cursor shape application, double-click timing —
  part 04 (this part only classifies).
- Wayfire actions execution — part 04 (D11).
