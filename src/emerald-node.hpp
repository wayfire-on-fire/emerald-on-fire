// emerald-node.hpp — Part 04 (frozen for parts 05/06, spec §Contract).
//
// Per-toplevel emerald scene node. Renders Part 02's pure
// `emeraldwf_render_frame()` cairo surface into a GL texture and composites
// it around the client; delivers pointer/touch input through Part 03's
// EmeraldLayout to wayfire WM actions (overview D11).
#pragma once

#include <memory>
#include <optional>
#include <string>

#include <cairo.h>

// pango's own forward typedef (pango/types.h) — only the opaque pointer is
// needed here (measure_pl is owned/used in emerald-node.cpp).
typedef struct _PangoLayout PangoLayout;

// Use the REAL wayfire toplevel-view type. Set EMERALDWF_HAVE_WAYFIRE_VIEW
// before including emerald-layout.hpp (→ emerald-types.hpp) so the
// placeholder forward-declaration there is suppressed.
#define EMERALDWF_HAVE_WAYFIRE_VIEW 1

#include <wayfire/geometry.hpp>
#include <wayfire/render.hpp>
#include <wayfire/region.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-input.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/util.hpp>

#include "emerald-button-fade.hpp"
#include "emerald-layout.hpp"
#include "emerald-node-pure.hpp"

namespace wf
{
namespace emeraldwf
{
class emerald_decorator_t;
class ThemeSettings;

class emerald_node_t : public wf::scene::node_t,
    public wf::pointer_interaction_t,
    public wf::touch_interaction_t
{
  public:
    explicit emerald_node_t(std::shared_ptr<emerald_decorator_t> deco);
    ~emerald_node_t() override;

    // ── Pure helpers (testable headless) ────────────────────────────────────
    // Bounding box of the node in parent (surface-root) coordinates: the
    // full frame size at offset {-margins.left, -margins.top} (spec 04 §1).
    static wf::geometry_t node_bounding_box(const emerald_decorator_t & deco);
    // Frame-coord hit test: returns a non-empty input node when the point
    // (in parent coordinates) is inside the frame region including the
    // resize bands (layout.resize_edges != 0) that surround the client.
    static std::optional<wf::scene::input_node_t> hit_test(const emerald_decorator_t & deco,
        const wf::pointf_t & at);

    // ── State updates (called by emerald_decorator_t, part 04) ─────────────
    void resize(); // recompute ctx sizes, layout, region
    void set_active(bool active); // invalidates + damages
    void set_title(std::string title); // invalidates + damages
    void set_theme_dirty(); // forces full re-render next frame

    // ── Cached render state (exposed for tests) ────────────────────────────
    node_cache_t cache;
    // Owns the GL texture derived from cache.surface across frames. The
    // surface is immutable once rendered, so the texture stays valid until a
    // rebuild replaces the surface — a cache hit reuses it instead of
    // re-uploading the full frame buffer to the GPU (pick_frame_texture).
    std::shared_ptr<wf::texture_t> cache_texture;

  private:
    // wf::scene::node_t
    void gen_render_instances(
        std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage,
        wf::output_t *output = nullptr) override;
    wf::geometry_t get_bounding_box() override;
    std::optional<wf::scene::input_node_t> find_node_at(const wf::pointf_t& at) override;

    // Route scene pointer/touch events to THIS node. Without these overrides
    // the base node_t returns a static noop, so no input (title-bar move,
    // border resize, button clicks) would ever reach the handle_* methods.
    wf::pointer_interaction_t& pointer_interaction() override
    {
        return *this;
    }

    wf::touch_interaction_t& touch_interaction() override
    {
        return *this;
    }

    // pointer/touch interaction
    void handle_pointer_enter(wf::pointf_t position) override;
    void handle_pointer_leave() override;
    void handle_pointer_motion(wf::pointf_t pointer_position,
        uint32_t time_ms) override;
    void handle_pointer_button(const wlr_pointer_button_event& event) override;
    void handle_pointer_axis(const wlr_pointer_axis_event& event) override;
    void handle_touch_down(uint32_t time_ms, int finger_id,
        wf::pointf_t position) override;
    void handle_touch_up(uint32_t time_ms, int finger_id,
        wf::pointf_t lift_off_position) override;
    void handle_touch_motion(uint32_t time_ms, int finger_id,
        wf::pointf_t position) override;

    // WM actions (overview D11).
    void handle_move();
    void handle_resize(uint32_t edges);
    void handle_button(int btype);
    void handle_click_action(int action); // double/middle click → D11 action
    void handle_scroll_shade(double delta);

    // Node-local coordinate conversion (frame coords = parent - offset).
    wf::pointf_t to_local(wf::pointf_t parent) const;
    // Finding 19b: input→layout coordinate conversion. The layout (button
    // regions, titlebar band, resize bands) is positioned in the
    // FULL-EXTENT frame coordinate system (the engines draw against it),
    // while the node's origin is the CROPPED surface corner when the window
    // is maximized + cropped — so raw to_local() misses the titlebar band
    // (which starts at y = top_space) and the buttons, making the whole
    // titlebar inert. This adds the maximized crop back, mapping node
    // surface coords to the full-extent frame coords the layout expects.
    wf::pointf_t to_local_full_extent(wf::pointf_t parent) const;

    // The live toplevel view (via the decorator), null if none.
    wayfire_toplevel_view view_of() const;

    // Render the (possibly rebuilt) frame texture into the target.
    void render_frame(const wf::scene::render_instruction_t& data);

    // Damage the node's full frame region (title bar + borders), not just the
    // client. view->damage() only covers the surface root's client bounding
    // box, so the title bar (node-local y < 0) would otherwise never repaint.
    // Mirrors the reference decor plugin (damage_node(deco, bbox) on attach).
    void damage_frame();

    // Hover change → damage button regions (first cut: full re-render).
    void update_hover(wf::pointf_t local);

    // Finding 43: button fade/pulse — upstream emerald's counters[]
    // state machine (src/main.c:1388-1607), run here on the scene node.
    // The crossfade is between the button's non-hovered and hovered
    // surface columns (the button never fades out); the pulse breathes
    // the highlight while a button is hovered. Events:
    // button_fade_hover(b, now_hovered) — pointer enter/leave
    // button_fade_pressed(b)            — pointer button down
    // The one-shot timer ticks all buttons per step and reschedules
    // itself while any counter is moving (or a pulse is active).
    void button_fade_hover(int b, bool now_hovered);
    void button_fade_pressed(int b);
    void button_fade_reset();
    void button_fade_tick_all();
    void button_fade_arm();
    ButtonFadeConfig button_fade_config() const;

    std::shared_ptr<emerald_decorator_t> deco;
    EmeraldLayout layout; // part 03
    bool theme_dirty  = true;             // set_theme_dirty() → full re-render
    bool active_dirty = false;
    bool title_dirty  = false;
    bool size_dirty   = true;

    // Review N2: title-measure context, reused across rebuilds. The old
    // code created a 1×1 cairo surface + PangoLayout on EVERY frame
    // rebuild (title/size/theme/hover changes) just to measure the title
    // string — pure allocation churn for objects whose only per-rebuild
    // input is the text (set on the cached layout below). Freed in the
    // dtor. (Measurements are font/text only, so a 1×1 surface suffices.)
    cairo_surface_t *measure_surface = nullptr;
    cairo_t *measure_cr     = nullptr;
    PangoLayout *measure_pl = nullptr;

    int last_hover_button = -1; // mirror of cache.hover_button

    // Button press tracking (per-button, like pixdecor deco-layout.cpp:136-146)
    int pressed_button = -1; // btype pressed, -1 if none

    // Last known pointer/touch position (parent coords). wlr button/axis
    // events carry no position, so the node tracks it from enter/motion.
    wf::pointf_t last_pointer_pos{0, 0};

    // Finding 43: button fade state (one per button; see
    // emerald-button-fade.hpp for the semantics).
    ButtonFadeState button_fade_state[B_T_COUNT] = {};
    ButtonFadeConfig button_fade_cfg_;
    wf::wl_timer<false> fade_timer;

    // Finding 19/20: titlebar press/move state machine (pure —
    // emerald-node-pure.hpp). A titlebar left-press only ARMS a pending
    // move; it commits on real motion (finding 20 — a committed move grabs
    // the pointer and would swallow the double-click's second press) and a
    // second press within the timeout fires double_click_action().
    titlebar_input_state_t titlebar_input;
};
} // namespace emeraldwf
} // namespace wf
