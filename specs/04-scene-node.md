# Part 04 — Scene node & rendering (first visible emerald frame)

**Read `specs/00-overview.md` first, plus the contracts in parts 01-03.**
This part is the milestone: after it, wayfire users see emerald frames.

## Goal

An `emerald_node_t` scene node per decorated toplevel that:

- renders the emerald frame (via part 02's pure `emeraldwf_render_frame`)
  into a GL texture and composites it around the client,
- delivers pointer/touch input through part 03's layout to wayfire WM
  actions (overview D11),
- manages damage and hover/redraw state,
- plugs into part 01's `emerald_decorator_t` (`attach_node`/`detach_node`/
  `on_geometry_changed`/`on_activated_changed`/`on_theme_changed` — the
  stubs part 01 left).

## Deliverables

| File | Content |
|------|---------|
| `src/emerald-node.hpp` | Per §Contract below (frozen for 05/06). |
| `src/emerald-node.cpp` | Implementation. |
| `src/emerald-decorator.cpp` | Fill in part-01 stubs: node attach/detach/updates. |
| `tests/test_render_node.cpp` | Headless: region + texture-content tests. |
| `tests/INTEGRATION-CHECKLIST.md` | Manual wayfire QA (below). |

## Contract (`src/emerald-node.hpp`)

```cpp
namespace wf::emeraldwf {
class emerald_node_t : public wf::scene::node_t,
                       public wf::pointer_interaction_t,
                       public wf::touch_interaction_t {
  public:
    emerald_node_t(std::shared_ptr<emerald_decorator_t> deco);
    ~emerald_node_t();

    // Pure helpers (testable headless):
    static wf::geometry_t node_bounding_box(const emerald_decorator_t&);
    static std::optional<wf::scene::input_node_t>
        hit_test(const emerald_decorator_t&, const wf::pointf_t& at);

    void resize();                     // recompute ctx sizes, layout, region
    void set_active(bool);
    void set_title(std::string);       // invalidates title texture
    void set_theme_dirty();            // forces full re-render next frame
    void set_shade_progress(double);   // part 05

  private:
    // wf::scene::node_t
    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>&,
                              wf::scene::damage_callback,
                              wf::output_t * = nullptr) override;
    wf::geometry_t get_bounding_box() override;
    std::optional<wf::scene::input_node_t> find_node_at(const wf::pointf_t&) override;
    // pointer/touch interaction
    void handle_pointer_enter/leave/motion(...);
    void handle_pointer_button(const wlr_pointer_button_event&) override;
    void handle_pointer_axis(const wlr_pointer_axis_event&) override;
    void handle_touch_down/up/motion(...) override;
    void handle_action(int action_id, uint32_t edges);

    std::shared_ptr<emerald_decorator_t> deco;
    // cached render state:
    struct {
        cairo_surface_t *surface = nullptr;      // last rendered frame
        owned_texture_t tex;
        bool title_dirty, active_dirty, theme_dirty, size_dirty;
    } cache;
    EmeraldLayout layout;                        // part 03
    int last_hover_button = -1;
    wf::wl_timer<false> double_click_timer;
    // button fade animation (emerald settings.ini use_button_fade):
    int button_fade_state[9] = {0};              // per emerald main.c fade
    guint fade_timer = 0;                        // glib timeout -> damage
};
}
```

## Implementation spec

1. **Attach/detach** (in `emerald-decorator.cpp`): `wf::scene::add_back(
   view->get_surface_root_node(), node)` with node offset
   `(-thickness, -titlebar_top)` — note emerald's frame includes the
   shadow INSIDE the extents, so offset is exactly
   `{-margins.left, -margins.top}` (contrast with pixdecor which offsets by
   shadow thickness separately — do not copy that); `detach_node` =
   `wf::scene::remove_child`.
2. **Render instance** (`decoration_render_instance_t` style, pixdecor
   `deco-subsurface.cpp:261-317`): `schedule_instructions` intersects
   damage with node region; `render` → if any dirty flag: rebuild
   `DecorContext` (state from D4 mapping via decorator, active, client
   size, layout-filled fields from part 03), call
   `emeraldwf_render_frame(theme, ctx)` (part 02), upload result via
   `owned_texture_t{surface}` in `wf::gles::run_in_context`, destroy old
   surface; then `data.pass->add_texture(tex, target,
   geometry = construct_box({0,0}, frame size), data.damage)`.
   Title text width: measure with Pango before `emeraldwf_render_frame`
   (part 02's render uses `ctx.tobj_item_width[TBT_TITLE]` — pass the
   measured width into `layout.build(..., title_text_width)`).
3. **Damage**: connect `view_title_changed` (node already gets it via
   decorator), activate signal → `set_active` + damage full region;
   geometry changed → `resize()` + damage + `tx_manager->schedule_object`
   (margins may change); option/theme change → `set_theme_dirty()`.
4. **Input**: `find_node_at` = `hit_test`: node region extended by resize
   bands (layout.resize_edges != 0) accepts pointer (pixdecor
   `deco-subsurface.cpp:218-249` pattern, minus its shadow radius math —
   emerald shadow is inside the frame). `handle_action` per overview D11:
   - drag on titlebar → `default_wm->move_request(view)`;
   - drag on band → `default_wm->resize_request(view, edges)`;
   - button press/release pairs (track per-button press state like
     pixdecor `deco-layout.cpp:136-146` hover/press): release on same
     button → action (close/max/min/shade/supermax/stick);
   - press on titlebar: single click → nothing (except no-op),
     double-click (timer, GTK double-click time like emerald did via
     gdk — use `gtk_settings` "gtk-double-click-time" if available, else
     250ms) → `double_click_action()`; middle-click →
     `middle_click_action()`;
   - axis on titlebar: shade/unshade if `!no_scroll_shade` (settings.ini
     `[titlebars] no_scroll_shade`) and shade enabled (part 05 toggle;
     if part 05 not present, no-op);
   - hover: on hover change → damage only button regions (port emerald's
     fast path: `draw_only_buttons_region`, main.c:2055-2070) —
     implementation: mark `button_dirty` and re-render full frame (simple
     first cut; keep the fast path as an optimization note);
   - cursor: set pointer shape per `layout.cursor_region()` (X11 cursor
     names → wayfire `wf::cursor_manager`? wayfire has no per-node cursor
     API in 0.11 — use `wf::get_core().cursor_manager` via the
     `wf::input` plugin hooks if available, else document as a known gap
     and skip cursor shape; do NOT block the part on this).
5. **Button fade**: if `ws->use_button_fade`: on button state change run
   emerald's fade counter logic (main.c: grep `button_fade`,
   `stop_button_fade`, `button_fade_info`) as a glib timeout ticking
   `button_fade_step_duration` ms, each tick updating
   `ctx.button_states` + damage button regions until complete. Keep
   emerald's pulse behavior (`use_button_fade_pulse` in settings.ini).

## Tests

### `tests/test_render_node.cpp` (headless)

- `node_bounding_box` for a decorator with known margins/frame size
  (assert box = frame size at offset);
- `hit_test`: frame center top → node; deep in client → node (resize band
  extension); outside region → nullopt; CSD decorator → nullopt (no node
  at all — decorator test, part 01);
- render cache: two consecutive `render()` calls with unchanged state
  upload the SAME texture (no re-render); change title → re-render.
  (Drive the render instance with a mock `wf::render_target_t`? That needs
  GL — instead test the dirty-flag logic as a small pure helper
  `needs_rebuild(cache, ctx)` — spec: extract the invalidation predicate
  into a free function and test it directly.)

### `tests/INTEGRATION-CHECKLIST.md` (manual, real wayfire)

Run: `wayfire` with only `emeraldwf` enabled (test ini), default theme.
Check: [ ] SSD xwayland window shows emerald frame (border, titlebar,
buttons per `tobj_layout`); [ ] active/inactive states differ; [ ]
double-click titlebar maximizes (default action 0 = shade — verify
settings.ini mapping, adjust to match emerald defaults); [ ] close/min/
max buttons work; [ ] resize from all 8 edge directions incl. corners;
[ ] maximize/minimize/fullscreen via buttons; [ ] CSD window (e.g.
`weston-terminal` with CSD, or firefox) has NO emerald frame;
[ ] `always_decorate=app-id=...` forces double chrome; [ ] theme switch
(option `engine=` + restart) renders the other engine.

## Acceptance criteria

- `meson test` green (this part's targets + all earlier parts);
- integration checklist fully passed on a local wayfire;
- `emerald-node.hpp` contract above is implemented exactly (05/06 depend
  on `set_shade_progress` + `set_theme_dirty`);
- frame visuals match emerald goldens from part 02 (same renderer).

## Out of scope

- Shade transformer implementation (hook only) — part 05.
- Shadow custom data — part 05.
- Live settings reload — part 06.
