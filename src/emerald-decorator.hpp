// emerald-decorator.hpp — per-toplevel decorator custom data.
// Owned by Part 01. Matches overview §5.2.
#pragma once

#include <memory>
#include <string>
#include <algorithm>

#include <wayfire/config/types.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/object.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/matcher.hpp>
#include <wayfire/scene.hpp>

#include "emerald-settings.hpp"
// emerald-types.hpp declares the enum. The live-view state mapping
// (emerald_state_from_view) is owned by part 01 and declared here with a
// signature that stays pure (no wayfire view), so headless tests can call
// it. Part 02's emerald-types.hpp keeps the enum values frozen.
//
// toplevel-view.hpp (above) already defines the real `wayfire_toplevel_view`
// alias, so suppress the placeholder forward-declaration in emerald-types.hpp.
#define EMERALDWF_HAVE_WAYFIRE_VIEW 1
#include "emerald-types.hpp"

namespace wf
{
namespace emeraldwf
{
// part 06 finding 18: wheel delta → shade direction. Wheel UP (negative
// delta) shades, wheel DOWN unshades. ONE shared pure mapping used by both
// entry points — the decorator (SSD views) and the plugin's CSD branch —
// so the convention exists exactly once and is covered by a single
// headless test against production code.
constexpr bool scroll_shades(double delta)
{
    return delta < 0.0;
}

// Pure state mapping (D4). Part 01 implements; part 02's frozen header
// declares the enum. Declared here (not in emerald-types.hpp) so the
// signature can reference wf::toplevel_state_t without pulling wayfire
// into the frozen contract.
EmeraldWindowState emerald_state_from_view(const wf::toplevel_state_t &state,
                                           bool sticky);
} // namespace emeraldwf
} // namespace wf

namespace wf
{
namespace emeraldwf
{

class emerald_node_t;    // fwd — implemented by Part 04
class emerald_shade_t;   // fwd — implemented by Part 05

/**
 * Plain input struct for the margin function.
 * Decouples the pure margin math from a live wayfire view so it can be
 * tested headlessly (tests/test_margins.cpp).
 */
struct MarginsInput
{
    // Theme geometry (window_settings: win_extents + extra_extents)
    int left_space   = 6;  // win_extents.X
    int right_space  = 6;
    int top_space    = 4;
    int bottom_space = 6;
    int extra_left   = 0;  // extra_extents.X
    int extra_right  = 0;
    int extra_top    = 0;
    int extra_bottom = 0;
    // Border-only extents (win_extents.X) — needed to split each side into
    // its shadow-spread and border parts for the maximized crop (finding 19).
    int win_ext_left   = 0;
    int win_ext_right  = 0;
    int win_ext_top    = 0;
    int win_ext_bottom = 0;

    int titlebar_height = 17;

    // Plugin options (overview D9, D8; finding 19: both are cropped per side
    // when maximized — see margins_for()).
    bool maximized_borders = true;
    bool maximized_shadows = true;

    // View state (from toplevel->pending())
    bool     fullscreen  = false;
    uint32_t tiled_edges = 0;  // WLR_EDGE_* bitmask

    bool csd = false;  // view->should_be_decorated() == false
};

// Finding 54: the app's committed size is the ground truth. XDG apps can
// refuse to shrink below their OWN minimum — invisible to wayfire, because
// the stable xdg-shell protocol carries no size hints (get_min_size() is
// {0,0}) — while wayfire re-derives the frame from the size the app
// ACTUALLY committed (xdg-toplevel_t::handle_surface_commit →
// expand_dimensions_by_margins(get_current_wlr_toplevel_size(), margins)).
// If the plugin requests a client below the app's floor, the app commits
// the floor, the frame adopts floor + margins, and the next cycle's
// client_h (derived from the frame) is larger → the requested collapse
// deepens again → unbounded per-cycle drift.
//
// Fix: track the app's observed floor. `adopted_client` is the client
// height wayfire last derived from the app's commit; `last_requested`
// is the client height the plugin last asked for. adopted > last
// requested means the app refused to go smaller — its floor is (at
// least) `adopted`. Pure decision, extracted for headless testing
// (tests/test_shade.cpp runs the full feedback loop against it).
inline int update_observed_min(int live_min, int adopted_client, int last_requested)
{
    if (adopted_client > last_requested)
        return std::max(live_min, adopted_client);
    return live_min;
}

inline int effective_min_client_h(int hint_min, int live_min)
{
    return std::max(hint_min, live_min);
}

// Review N6: settle detection must not depend on the EXACT float 0.0 —
// duration_t clamps progress to the exact terminal value, but that is an
// implementation detail (an asymptotic easing or a change to
// timed_transition_t would silently disable the settle-restore and the
// capture guard). 1e-3 is visually indistinguishable from settled
// (margins are off by < 0.1 % of the strip).
constexpr double SHADE_SETTLE_EPSILON = 1e-3;

class emerald_decorator_t : public wf::custom_data_t
{
  public:
    emerald_decorator_t(::wayfire_toplevel_view view,
                        std::shared_ptr<const ThemeSettings> theme);
    ~emerald_decorator_t();

    // ── Full implementation in Part 01 (pure, testable) ─────────────────────
    // Negotiates frame margins exactly per overview D8.
    wf::decoration_margins_t get_margins(const wf::toplevel_state_t &state) const;
    int                      get_titlebar_height() const;
    bool                     is_csd() const;

    // Pure margin math over a plain struct — extracted for headless tests.
    static wf::decoration_margins_t margins_for(const MarginsInput &in);

    // ── Implemented by Part 04 (node attach/detach, redraw, geometry) ──────
    void attach_node();          // contract: implemented by part 04
    void detach_node();         // contract: implemented by part 04
    // part 06 finding 16: pure decision used by detach_node() — may
    // remove_child(node) be called? Only when node's parent is exactly
    // the surface root the node was attached to. Both arguments are raw
    // pointer COMPARES (no dereference): the caller keeps the sroot
    // alive with a shared_ptr, so this is safe to evaluate even while
    // the view is being torn down (dead-parent case → false → the node
    // is simply out of the graph already, nothing to remove).
    static bool detach_should_remove_child(
        wf::scene::node_t *node_parent,
        wf::scene::floating_inner_node_t *sroot);
    void on_geometry_changed(); // contract: implemented by part 04
    void on_activated_changed(); // contract: implemented by part 04
    void on_theme_changed();    // contract: implemented by part 04

    // ── Implemented by Part 05 (shade) ──────────────────────────────────────
    // Implemented in emerald-decorator-shade.cpp (kept separate from
    // emerald-decorator.cpp so the headless margins test does not link the
    // scene/view-transformer symbols).
    void on_shade_progress(double progress);
    // part 06 finding 17: explicit-direction shade (the wheel gesture and
    // the IPC/WCM toggle both end up here).
    void toggle_shade(bool shade);
    // Flip the current state (no transformer = unshaded → shade).
    void toggle_shade();
    // Scroll-shade entry point (finding 17): wheel UP shades, wheel DOWN
    // unshades. Direction-based (not toggle) so slow scrolling settles
    // instead of re-triggering every tick — ShadeState::init_animation
    // already no-ops when the requested direction is the current one.
    void scroll_shade(double delta);

    // The view's shade transformer (null if no shade animation is in flight
    // or the view has no transformed node). Used by get_margins() and the
    // plugin's global axis/IPC triggers.
    std::shared_ptr<emerald_shade_t> shade() const;

    // ── Theme/titlebar geometry ─────────────────────────────────────────────
    // Part 01: default 17; set_titlebar_height() is called by the plugin
    // (csd_titlebar_height option) or by Part 02 once the theme is loaded.
    void set_titlebar_height(int height);

    // Finding 19: the emeraldwf/maximized_borders + maximized_shadows options
    // must reach the margin math (they were declared + callback-wired in the
    // plugin but never copied into MarginsInput — the WCM checkboxes did
    // nothing). The plugin calls these on every (re)created decorator;
    // margins_for() crops maximized windows per side accordingly.
    void set_maximized_borders(bool on);
    void set_maximized_shadows(bool on);
    bool maximized_borders() const;
    bool maximized_shadows() const;

    // Populates a MarginsInput from the theme + a live toplevel state.
    // Used by get_margins().
    MarginsInput make_margins_input(const wf::toplevel_state_t &state) const;

    // The UNSHADED client height for the pending frame geometry
    // (frame height − top margin − unshaded bottom margin). Feeds the CSD
    // shadow export (shadow_margins_for) — the frame margin math itself is
    // steady and takes no client input. Must never be the frame height
    // itself (the client is the frame shrunk by its margins).
    int unshaded_client_h() const;

    // Overview §5.2: `DecorContext &ctx()` — accessor to the decorator's
    // DecorContext (stored in the private member `decor_ctx` to avoid a
    // member/function name clash).
    DecorContext &ctx();

    // ── Part 04 helpers used by the scene node ──────────────────────────────
    // The live toplevel view (null if the decorator has no view).
    ::wayfire_toplevel_view view_for_offsets() const;
    // The scene node attached to this decorator (for input_node_t). Null
    // until attach_node() runs.
    emerald_node_t *node_for_hit_test() const;

    // Access to the theme (const) used by the node for rendering + layout.
    std::shared_ptr<const ThemeSettings> theme_ref() const;
    // part 06 finding 14: a live theme/engine reload must hand the new theme
    // to every decorator — the engine is a process-wide global (e_draw), so
    // rendering a decorator's STALE window_settings (whose engine_ws belongs
    // to a dlclose'd, *different* engine) is a foreign-struct read and the
    // root of the repeated engine-switch crash.
    void set_theme(std::shared_ptr<const ThemeSettings> theme);

    // Part 05 (spec 05 §2): the plugin registers this option; the shade
    // transformer + margin math read it. Null until the plugin sets it.
    std::shared_ptr<wf::config::option_t<wf::animation_description_t>>
        shade_duration_opt() const;
    void set_shade_duration_opt(
        std::shared_ptr<wf::config::option_t<wf::animation_description_t>> opt);
    // Finding 23: mirror of the plugin's enable_shade option (see above).
    bool enable_shade() const;
    void set_enable_shade(bool enabled);

    // Finding 28: per-view activation signal (pixdecor parity). Without this
    // the titlebar's active/inactive colors froze at decoration time —
    // on_activated_changed() existed but was never wired. Connected in
    // attach_node() (SSD path only) and disconnected in detach_node().
    wf::signal::connection_t<wf::view_activated_state_signal> on_view_activated;

  private:
    ::wayfire_toplevel_view              view;
    // part 06 finding 16: liveness of the view. `view` is an observer_ptr
    // (non-owning); when the view dies it dangles. weak_view lets
    // view_for_offsets() report "dead" instead of handing out a dangling
    // pointer (which segfaulted the next render of the emerald node —
    // the node outlives the view's data map in some destruction orders
    // and renders against the dead view).
    // Type is weak_ptr<view_interface_t> (NOT toplevel_view_interface_t):
    // the toplevel's weak_from_this() goes through dynamic_pointer_cast,
    // whose typeinfo lives in libwayfire (unavailable to headless tests);
    // view_interface_t is the enable_shared_from_this base, so
    // shared_from_this() needs no RTTI. Liveness is identical (same object).
    std::weak_ptr<wf::view_interface_t> weak_view;
    std::shared_ptr<const ThemeSettings> theme;
    DecorContext                         decor_ctx;
    std::shared_ptr<emerald_node_t>      node;  // Part 04
    // part 06 finding 16: the surface root the node was attached to.
    // detach_node() must only remove_child(node) when node's parent is
    // STILL this live node — in the destruction order where the view's
    // scene nodes die before its data map, node->parent() is a dead
    // surface root and remove_child() on it is a use-after-free.
    // Holding a shared_ptr keeps the parent alive for that check/call.
    std::shared_ptr<wf::scene::floating_inner_node_t> sroot;

    // Part 05: shade animation duration (set by the plugin).
    std::shared_ptr<wf::config::option_t<wf::animation_description_t>>
        shade_duration_opt_;
    // Finding 23: the plugin's enable_shade option, published here so the
    // node-level shade entry points (titlebar scroll, click actions) honor
    // it — the plugin-side checks only cover IPC + the global wheel
    // binding.
    bool enable_shade_ = true;
    int                                  titlebar_height = 17;
    // Finding 19: plugin options (defaults match emerald's maximized behavior
    // of keeping the full border + shadow).
    bool                                 maximized_borders_ = true;
    bool                                 maximized_shadows_ = true;
    // Finding 54: the app's observed min client height (its real floor —
    // invisible via get_min_size() on XDG). See update_observed_min().
    // Persisted across cycles: once observed, the cap keeps requesting
    // sizes the app honors, so the frame stops drifting.
    int                                  live_min_client_h_ = 0;
    // Finding 54: the client height the plugin last requested (for the
    // adopted-vs-requested comparison in update_observed_min()).
    int                                  last_requested_client_h_ = 0;
    // Finding 54: the last smoothed progress, to tell the animation
    // direction. The floor is only recorded while heading TOWARD full
    // shade (progress rising): during unshade the app follows the growing
    // requests, and a client-initiated grow (adopted > requested while
    // progress is falling) is not a floor — recording it would permanently
    // cap the collapse depth to the grown size.
    double                               last_shade_progress_ = 0.0;
    // Finding 54: the frame geometry at the start of the shade animation.
    // When the unshade settles, pending geometry is restored to it: during
    // the shade the app may have committed a size larger than requested
    // (its own minimum) and wayfire adopted that into the frame — without
    // the restore the window would keep the grown size.
    wf::geometry_t                       shade_ref_geom_{};
    // True once shade_ref_geom_ has been captured (toggle_shade). The
    // restore in on_shade_progress() must be skipped until then: for a
    // never-shaded window shade_ref_geom_ is the default {} and an
    // unconditional restore (e.g. the enable_shade off-path's
    // on_shade_progress(0.0) reset) would zero the window's geometry.
    // Cleared after the settle-restore so a later reset cannot restore a
    // STALE reference (the window may have been resized in the meantime).
    bool                                 has_shade_ref_ = false;
};

} // namespace emeraldwf
} // namespace wf
