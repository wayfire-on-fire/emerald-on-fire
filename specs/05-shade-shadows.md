# Part 05 — Shade, shadows, CSD polish

**Read `specs/00-overview.md` first, plus parts 01, 04.** Emerald's
signature "scroll titlebar to shade" and its shadows-on-everything
behavior, ported to wayfire primitives.

## Goal

1. Emerald's **shade** (scroll on titlebar / axis binding / button) as a
   wayfire 2D view transformer that folds a view into its titlebar —
   emerald's look (the frame stays, the window collapses), not a
   minimize.
2. Export `wf-decoration-shadow-margin` custom data (overview D12) so a
   standalone shadow plugin can shadow CSD windows and shaded windows.
3. CSD edge cases nailed down (no frame, but shadow data + shade support
   for CSD windows with the `csd_titlebar_height` guess, like pixdecor).

## Deliverables

| File | Content |
|------|---------|
| `src/emerald-shade.hpp` | Per contract below (frozen for 06). |
| `src/emerald-shade.cpp` | Transformer implementation. |
| `src/emerald-decorator.{hpp,cpp}` | Implement `on_shade_progress()`, `toggle_shade()` (part-01 stubs); add shadow-margin custom data write. |
| `src/plugin.cpp` | Axis binding (`shade_modifier`), IPC activator `emeraldwf/shade_toggle` (pattern: pixdecor `decoration.cpp:68, 238-281`), option callbacks. |
| `tests/test_shade.cpp` | Margin-progression tests. |

## Contract (`src/emerald-shade.hpp`)

```cpp
namespace wf::emeraldwf {
// 2D view transformer port of pixdecor's pixdecor_shade (pixdecor
// shade.hpp): scales/positions the view's surface so that only the top
// titlebar strip remains visible, animated. Emerald flavor: the strip
// height = the EMERALD titlebar (or csd_titlebar_height for CSD views).
class emerald_shade_t : public wf::view_transformer_t {
  public:
    emerald_shade_t(wayfire_toplevel_view view, int titlebar_height);
    void set_titlebar_height(int);
    void init_animation(bool shade);      // animate to 1.0 (shaded) / 0.0
    double get_progression() const;       // 0 = unshaded, 1 = fully shaded
    bool get_direction() const;           // true = currently shaded
  private:
    wayfire_toplevel_view view;
    int titlebar_height;
    wf::animation::simple_animation_t progression;  // from shade_duration
};
}
```

(If the pixdecor `shade.hpp` API/behavior is simpler to adapt, copy its
structure: it already implements exactly this fold with a
`TRANSFORMER_2D` node — source: `/projects/contrib/pixdecor/src/shade.hpp`.
Adaptation is expected and fine; the public contract above is fixed.)

## Implementation spec

1. **Transformer**: port pixdecor `shade.hpp` (view + titlebar_height,
   progress-driven position/size transform of the client surface). Attach
   per view via `view->get_transformed_node()->add_transformer(node,
   wf::TRANSFORMER_2D, "emeraldwf-shade")` (pixdecor
   `deco-subsurface.cpp:184-197, 384-397`).
2. **Triggers** (all funnel into `emerald_decorator_t::toggle_shade()`):
   - titlebar scroll (part 04 axis hook) — honor
     `settings.ini [titlebars] no_scroll_shade`;
   - global axis binding: `shade_modifier` (default `<super>+<ctrl>`) +
     vertical scroll (pixdecor `decoration.cpp:236, 264-281`);
   - shade/unshade button in the emerald theme (action D11);
   - IPC: `emeraldwf/shade_toggle` activator (wcm bindable).
3. **Margins during shade** (fill `emerald_decorator_t::on_shade_progress`
   + `get_margins` adjustment): port pixdecor
   `deco-subsurface.cpp:672-701` formula: while shading, bottom margin
   = `shadow_thickness + (client_h - shadow_thickness - titlebar) *
   progress`; schedule the toplevel in the tx manager on progress change
   (idle-coalesced). `get_margins()` takes the current progression into
   account so a mid-animation transaction stays consistent.
4. **Shadow margin custom data** (D12): on every margins recomputation,
   store `wf_shadow_margin_t` (same struct shape as pixdecor
   `deco-subsurface.hpp:12-27`) under view data key
   `"wf-decoration-shadow-margin"`:
   `{thickness, thickness, thickness,
    thickness + (client_h - thickness - titlebar) * shade_progress}` —
   i.e. emerald's shadow extents (the frame's own border thickness, which
   contains emerald's shadow) as left/right/bottom, bottom extended with
   the shade-collapsed client. For CSD views (no decorator): a minimal
   decorator-less writer so CSD windows still get shadow margins (this is
   why pixdecor writes it from `get_margins` even for CSD-adjacent cases;
   implement as a free function `export_shadow_margins(view, thickness)`
   callable from the plugin for CSD views, thickness from
   `csd_titlebar_height`-independent shadow defaults — keep it
   `thickness = 0` + a configurable shadow pad if a shadow plugin needs
   nonzero values; document the behavior in 06's README).
5. **CSD + shade**: shade works on CSD views too (transformer on the
   client surface; titlebar strip = `csd_titlebar_height` option) — mirror
   pixdecor's handling (`csd_titlebar_height` usage,
   `deco-subsurface.cpp:536-542`).

## Tests (`tests/test_shade.cpp`)

- margin progression: with thickness 6, titlebar 23, client_h 300:
  progress 0 → bottom 264… (use the formula: `6 + (300 - 6 - 23) * p`);
  p ∈ {0, 0.25, 0.5, 0.75, 1.0} asserted against the formula;
- left/right/top margins unchanged by shade (top stays 23);
- `init_animation` direction bookkeeping: after shading, `get_direction()`
  true; toggle back → false (test the state machine without a GL context —
  factor the pure state into a `ShadeState` helper struct and test it);
- shadow custom data: after `export_shadow_margins(view, 0)` for a CSD
  view, the stored `wf_shadow_margin_t` values match the formula.
  (A live view is needed → build a tiny `wf::view` in-test if wayfire's
  test infra allows, else test the pure computation function
  `shadow_margins_for(client_h, thickness, titlebar, progress)`.)

## Acceptance criteria

- `meson test` green (all parts' tests);
- manual: scroll titlebar shades the window into its emerald titlebar and
  back, smoothly (shade_duration); axis binding works; shade button works;
  CSD window can be shaded; a shadow plugin (if installed, e.g. the
  wayfire-contrib shadow plugin) shadows CSD windows via the custom data;
- `emerald-shade.hpp` contract implemented exactly.

## Out of scope

- Rendering changes — none expected (shade is a transform, not a render).
- Live settings reload — part 06.
