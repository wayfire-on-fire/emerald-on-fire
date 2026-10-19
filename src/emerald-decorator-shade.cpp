// emerald-decorator-shade.cpp — Part 05: shade hooks for emerald_decorator_t.
//
// Implements on_shade_progress() / toggle_shade() (part-01 stubs) and the
// shade() accessor. Kept in a separate TU from emerald-decorator.cpp so the
// headless margins test does not link the scene / view-transformer symbols.
#include "emerald-decorator.hpp"

#include <wayfire/output.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/txn/transaction-manager.hpp>

#include "emerald-node.hpp"
#include "emerald-shade.hpp"
#include "emerald-shadow-margins.hpp"

namespace wf
{
namespace emeraldwf
{
std::shared_ptr<emerald_shade_t> emerald_decorator_t::shade() const
{
    if (view == nullptr)
    {
        return nullptr;
    }

    auto tmgr = view->get_transformed_node();
    return tmgr->get_transformer<emerald_shade_t>(SHADE_TRANSFORMER_NAME);
}

void emerald_decorator_t::on_shade_progress(double progress)
{
    if (view == nullptr)
    {
        return;
    }

    auto toplevel = view->toplevel();
    if (toplevel == nullptr)
    {
        return;
    }

    // Update the toplevel's pending margins for the current shade progress
    // (spec 05 §3: `get_margins()` takes the progression into account so a
    // mid-animation transaction stays consistent). The tx manager is
    // idle-coalesced: scheduling repeatedly is cheap.
    auto & pending = toplevel->pending();
    pending.margins = get_margins(pending);

    // Finding 54: observe the app's real floor. `current()` is what wayfire
    // last derived from the app's committed size (the client inside the
    // frame); compare it with the client we last REQUESTED. If the app
    // refused to shrink below its own minimum, the adopted client exceeds
    // the request — remember that floor so the collapse cap (finding 53)
    // keeps requesting sizes the app will honor. (XDG: get_min_size() is
    // {0,0}, so without this the cap was a no-op and every cycle drifted.)
    //
    // Direction guard: only record while heading TOWARD full shade
    // (progress rising). During the unshade the app follows the growing
    // requests (adopted == requested); a client-initiated grow while
    // progress is FALLING (adopted > requested) is not a floor — recording
    // it would permanently cap the collapse depth to the grown size.
    {
        const auto & cur  = toplevel->current();
        const int adopted = cur.geometry.height -
            (int)cur.margins.top - (int)cur.margins.bottom;
        const bool heading_to_shade = (progress >= last_shade_progress_);
        last_shade_progress_ = progress;
        if (heading_to_shade)
        {
            live_min_client_h_ =
                update_observed_min(live_min_client_h_, adopted,
                    last_requested_client_h_);
        }

        last_requested_client_h_ = pending.geometry.height -
            (int)pending.margins.top -
            (int)pending.margins.bottom;
        if ((progress < SHADE_SETTLE_EPSILON) && has_shade_ref_ &&
            !pending.fullscreen && (pending.tiled_edges == 0))
        {
            // Finding 54: the unshade settled — restore the pre-shade
            // frame geometry. During the shade the app may have committed
            // a size LARGER than requested (its own minimum, invisible on
            // XDG); wayfire adopts committed sizes into the frame (apply /
            // handle_surface_commit → adjust_geometry_for_gravity), so the
            // frame now carries that growth. The margins at p = 0 are
            // exactly the unshaded ones, so the restored frame requests
            // exactly the unshaded client — which the app honors — and the
            // window returns to its original size. (The observed floor is
            // kept: it makes the next shade request only sizes the app
            // honors, so the restore is a no-op from the second cycle on.)
            //
            // Review N5: the restore must not clobber state that changed
            // DURING the ~500 ms animation — a mid-unshade maximize/tile
            // or user resize would be reverted to the old windowed
            // geometry. The !fullscreen && tiled_edges == 0 guard is the
            // same one adjust_new_decorations/remove_decoration use for
            // exactly this reason. (A maximize mid-unshade makes the
            // pending state maximized; the geometry then belongs to the
            // WM, not to the reference.)
            //
            // Review N6: epsilon, not an exact 0.0 (see SHADE_SETTLE_EPSILON).
            //
            // has_shade_ref_: only restore a geometry we actually captured
            // (a never-shaded window's default {} must not land here via
            // an on_shade_progress(0.0) reset). Cleared now so a later
            // reset cannot restore a stale reference.
            pending.geometry = shade_ref_geom_;
            has_shade_ref_   = false;
        }
    }

    // Export the shadow margins (D12) for a standalone shadow plugin.
    // Finding 52: the formula takes the UNSHADED client height, not the
    // frame height (see emerald-decorator.cpp).
    const int client_h  = unshaded_client_h();
    const int thickness =
        (theme && theme->valid() && theme->ws() != nullptr) ?
        theme->ws()->win_extents.left : 6;
    // Strip = top margin for SSD (shadow+border+titlebar), CSD titlebar
    // for CSD — the height the window collapses to (finding 15).
    int strip = titlebar_height;
    if (!is_csd())
    {
        strip = (int)get_margins(toplevel->pending()).top;
    }

    export_shadow_margins(view, client_h, thickness, strip, progress,
        effective_min_client_h(toplevel->get_min_size().height,
            live_min_client_h_));

    // Damage the emerald frame node (its bounding box shrinks with the
    // margin change) and schedule the toplevel in the tx manager.
    if (node != nullptr)
    {
        node->resize();
    }

    view->damage();
    wf::get_core().tx_manager->schedule_object(toplevel);
}

void emerald_decorator_t::toggle_shade()
{
    // Finding 23: the plugin's enable_shade option gates the input-driven
    // paths too (titlebar double/middle-click with action=shade), not just
    // the IPC + global wheel binding the plugin checks itself.
    if (!enable_shade_)
    {
        return;
    }

    // part 06 finding 17: flip the current state (no transformer ==
    // unshaded, so flip = shade).
    auto tr = shade();
    toggle_shade(tr == nullptr ? true : !tr->get_direction());
}

void emerald_decorator_t::scroll_shade(double delta)
{
    // Finding 23: same enable_shade gate as toggle_shade() — plain
    // titlebar scrolling must not shade when the option is off.
    if (!enable_shade_)
    {
        return;
    }

    if (view == nullptr)
    {
        return;
    }

    if (view->toplevel() == nullptr)
    {
        return;
    }

    if (view->toplevel()->pending().fullscreen)
    {
        return;
    }

    auto ts = theme_ref();
    if (ts && ts->valid())
    {
        auto ws = ts->ws();
        if (ws && ws->titlebar_no_scroll_shade)
        {
            return;
        }
    }

    if (delta == 0.0)
    {
        return;
    }

    // part 06 finding 17: wheel UP = shade, wheel DOWN = unshade (the
    // roll-up convention, same as pixdecor). Direction-based, NOT
    // toggle: a slow scroll produces many small deltas; with a toggle
    // every tick re-triggered (the window shaded "nonstop"). With a
    // fixed direction, ShadeState::init_animation() no-ops once the
    // animation has reached the requested state, so the window
    // settles. (A scroll in the opposite direction mid-animation
    // reverses it — responsive, as expected.)
    const bool to_shade = scroll_shades(delta);
    auto tr = shade();
    if ((tr == nullptr) && !to_shade)
    {
        return; // no transformer == unshaded; nothing to unshade
    }

    toggle_shade(to_shade);
}

void emerald_decorator_t::toggle_shade(bool shade)
{
    if (view == nullptr)
    {
        return;
    }

    if (view->toplevel() == nullptr)
    {
        return;
    }

    if (view->toplevel()->pending().fullscreen)
    {
        return;
    }

    // The shade strip is the top of the view's BOUNDING BOX (finding 15):
    // for a decorated view that is the whole TOP MARGIN — shadow + border +
    // emerald titlebar (emerald's shadow lives inside the frame extents,
    // so the frame origin is the shadow's top, not the titlebar's).
    // Passing the bare titlebar height collapsed the window to a sliver of
    // shadow/border with the titlebar barely visible. For CSD views (no
    // emerald frame, margin 0) the bounding box top IS the client top, so
    // the strip is the CSD titlebar.
    int strip = titlebar_height;
    int bottom_strip = 0;
    if (!is_csd())
    {
        auto m = get_margins(view->toplevel()->pending());
        strip = (int)m.top;
        // Below the titlebar only the BOTTOM SHADOW is kept visible — not
        // the whole bottom margin. Requirement: a shaded window is the
        // titlebar strip alone, with a soft glow starting directly from the
        // titlebar's lower edge; NO border band and NO window content may
        // show below it (the bottom border is part of the rolled-up frame).
        //
        // Bottom margin = border (win_extents.bottom) + shadow spread
        // (bottom_space − win_extents.bottom). margins_for() is STEADY
        // through the animation, so m.bottom is the theme value; subtracting
        // the border leaves the shadow band only. The transformer samples
        // [H − bottom_strip, H) of the frame texture — the texture's bottom
        // rows are exactly the shadow (the engine draws border + shadow
        // anchored to the frame edge, shadow fading to the edge) — so the
        // band is a pure glow, strongest right under the titlebar, fading
        // down to the desktop.
        int shadow_only = (int)m.bottom;
        if (auto ts = theme_ref();ts && ts->valid())
        {
            if (const auto *wsl = ts->ws())
            {
                shadow_only -= wsl->win_extents.bottom;
            }
        }

        bottom_strip = std::max(0, shadow_only);
    }

    // Ensure a transformer exists; pixdecor attaches it on-demand and
    // detaches it when the animation finishes unshaded (see emerald-shade.cpp
    // tick()).
    auto tmgr = view->get_transformed_node();
    std::shared_ptr<emerald_shade_t> tr =
        tmgr->get_transformer<emerald_shade_t>(SHADE_TRANSFORMER_NAME);
    // Finding 54: is the window currently FULLY unshaded? (No transformer,
    // or one present only transiently — settled at p = 0 before tick()
    // removes it.) Only then is the pending geometry the original unshaded
    // frame, safe to capture as the settle-restore reference. A re-shade
    // trigger (already shaded, e.g. another wheel-up tick) must NOT
    // re-capture the reference to the already-collapsed frame, and a
    // mid-unshade reverse must not capture a half-expanded frame.
    // Review N6: epsilon comparison, not an exact 0.0 (see
    // SHADE_SETTLE_EPSILON) — an asymptotic easing would never hit the
    // exact terminal value and this guard would stop capturing references.
    const bool fully_unshaded = (tr == nullptr) ||
        (!tr->get_direction() &&
            tr->get_progression() < SHADE_SETTLE_EPSILON);
    if (tr == nullptr)
    {
        // Finding 22: wire emeraldwf/shade_duration into the animation —
        // without this the transition ran with a null (instant) duration
        // and the option was a no-op.
        tr = std::make_shared<emerald_shade_t>(view, strip,
            shade_duration_opt_);
        tmgr->add_transformer(tr, wf::TRANSFORMER_2D, SHADE_TRANSFORMER_NAME);
    }

    tr->set_strip_h(strip);
    tr->set_bottom_strip_h(bottom_strip);
    // part 06 finding 17: explicit direction (scroll up / IPC toggle /
    // scroll down) — init_animation() is a no-op when the animation is
    // already heading that way, which is what settles slow scrolling.
    tr->init_animation(shade);

    // Finding 54: baseline the observed-floor tracking. last_requested is
    // the unshaded client (what the app currently has / will be asked on
    // the first unshade tick). The observed floor (live_min_client_h_) is
    // PERSISTED across cycles: once the app's minimum is known, the cap
    // keeps every request at or above it, so later cycles request only
    // sizes the app honors — no adoption drift, no per-cycle jump. (The
    // min-size hint is applied separately in make_margins_input().)
    last_requested_client_h_ = unshaded_client_h();
    // Finding 54: remember the frame at the start of the shade — the
    // unshade settles back to exactly this (see on_shade_progress()).
    // has_shade_ref_ is set together: the settle-restore is gated on it,
    // so a window that is never shaded keeps its default (unrestored)
    // geometry even when a on_shade_progress(0.0) reset runs.
    if (shade && fully_unshaded)
    {
        shade_ref_geom_ = view->toplevel()->pending().geometry;
        has_shade_ref_  = true;
    }

    // Initial margins + shadow export for the new direction (the animation
    // hook will keep them in sync as progress advances).
    auto & pending = view->toplevel()->pending();
    pending.margins = get_margins(pending);
    // Finding 52: unshaded client height, not the frame height (see
    // emerald-decorator.cpp).
    const int client_h  = unshaded_client_h();
    const int thickness =
        (theme && theme->valid() && theme->ws() != nullptr) ?
        theme->ws()->win_extents.left : 6;
    export_shadow_margins(view, client_h, thickness, strip,
        tr->get_progression(),
        effective_min_client_h(view->toplevel()->get_min_size().height,
            live_min_client_h_));
    wf::get_core().tx_manager->schedule_object(view->toplevel());
}
} // namespace emeraldwf
} // namespace wf
