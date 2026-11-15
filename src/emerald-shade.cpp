// emerald-shade.cpp — Part 05: shade view transformer implementation.
//
// The pure parts (ShadeState, the render instance, find_node_at,
// gen_render_instances) live in emerald-shade.hpp; this TU only provides the
// constructor / destructor / tick() which need a live view + output.
#include "emerald-shade.hpp"

namespace wf
{
namespace emeraldwf
{
// Review F2 (-Wshadow): parameters named *_arg — bare `view`/`strip_h`
// shadow the members they initialize (and `duration` shadows the base
// `timed_transition_t::duration` member).
emerald_shade_t::emerald_shade_t(::wayfire_toplevel_view view_arg, int strip_h_arg,
    std::shared_ptr<wf::config::option_t<wf::animation_description_t>>
    duration_opt) :
    wf::scene::view_2d_transformer_t(view_arg)
    , view(view_arg)
    // M18 (issue #1): the member-init bypasses set_strip_h(), so clamp
    // here too (see set_strip_h for why).
    , strip_h(std::clamp(strip_h_arg, 0, 1 << 20))
    , state(duration_opt)
{
    if (view_arg)
    {
        output = view_arg->get_output();

        // M17 (issue #1, ChatGPT follow-up): re-sync the tick hook
        // IMMEDIATELY when the view moves to a different output — not only
        // on the next tick()/init_animation(). If the old output stops
        // rendering (no damage) after the move, tick() never runs again and
        // the animation would freeze on the stale output. (Wayfire's animate
        // plugin uses the same pattern.)
        on_set_output = [=] (view_set_output_signal *)
        {
            this->sync_output();
        };
        view_arg->connect(&on_set_output);
    }

    if (output)
    {
        tick_hook = [=] ()
        {
            this->tick();
        };
        output->render->add_effect(&tick_hook, wf::OUTPUT_EFFECT_PRE);
        tick_installed_ = true;
    }
}

emerald_shade_t::~emerald_shade_t()
{
    if (output && tick_installed_)
    {
        output->render->rem_effect(&tick_hook);
        tick_installed_ = false;
    }
}

wf::output_t *emerald_shade_t::sync_output()
{
    if (view == nullptr)
    {
        return output;
    }

    auto *cur = view->get_output();
    if ((cur == nullptr) || (cur == output))
    {
        // Unattached (mid-move) or unchanged: keep the current output so the
        // hook keeps driving; re-sync when the view (re)attaches.
        return output;
    }

    // M17 (issue #1): the view moved to a different output. Migrate the tick
    // hook so the animation keeps being driven on the NEW output (and the
    // transformer can still be removed when it settles there). Pre-M17 the
    // hook stayed on the construction-time output: the animation froze, the
    // transformer was never removed (removal only happens in tick()), and
    // tick_installed_ stayed true so init_animation() could never re-install
    // → leaked transformer + frozen shadow margins.
    if (tick_installed_)
    {
        if (output != nullptr)
        {
            output->render->rem_effect(&tick_hook);
        }

        cur->render->add_effect(&tick_hook, wf::OUTPUT_EFFECT_PRE);
    }

    output = cur;
    return output;
}

// M18 (issue #1): strip_h is fed by the WCM-editable csd_titlebar_height
// option for CSD views (runtime-editable, no validation upstream). Clamp:
// a NEGATIVE value makes the shaded CSD window render nothing (all
// draw_band calls have dh <= 0 → the window vanishes but stays in the
// scene, hit-testing finds no node); a value near INT_MAX maps the whole
// client into the "strip" band. 1<<20 px is ~1000x the largest real
// titlebar — any real value passes through unchanged.
void emerald_shade_t::set_strip_h(int h)
{
    strip_h = std::clamp(h, 0, 1 << 20);
}

void emerald_shade_t::set_bottom_strip_h(int h)
{
    bottom_strip_h = std::clamp(h, 0, 1 << 20);
}

void emerald_shade_t::init_animation(bool shade)
{
    // M17 (issue #1): re-derive the output from the view (it may have moved
    // since the last animation, or been unattached at construction) and
    // migrate the hook if it is installed.
    sync_output();

    // The tick hook is only installed while an animation is live: tick()
    // drops it once the progression settles (both directions), so a fresh
    // animation on an existing transformer (unshading a shaded window,
    // reversing mid-idle) has no driver left. Re-install it before starting.
    if (output && !tick_installed_)
    {
        output->render->add_effect(&tick_hook, wf::OUTPUT_EFFECT_PRE);
        tick_installed_ = true;
    }

    state.init_animation(shade);
    if (view)
    {
        view->damage();
    }
}

void emerald_shade_t::tick()
{
    if (view == nullptr)
    {
        return;
    }

    // M17 (issue #1): re-derive the output (the view may have moved
    // mid-animation) and migrate the hook so the animation keeps driving on
    // the new output.
    sync_output();

    // Advance the animation (duration_t advances its internal clock).
    // `progression.shade` now holds the smoothed value for this frame.
    const double p = state.get_progression();

    // Propagate progress to the decorator so margins + shadow custom data
    // stay consistent mid-animation (spec 05 §3). CSD views have no
    // decorator: the plugin's shadow-update callback (Review R4) re-exports
    // the progress-dependent shadow margins per tick, so a standalone
    // shadow plugin tracks the animation instead of the pre-shade margins.
    if (auto toplevel = view->toplevel())
    {
        if (auto deco = toplevel->get_data<emerald_decorator_t>())
        {
            deco->on_shade_progress(p);
        } else if (shadow_update_)
        {
            shadow_update_();
        }
    }

    view->damage();

    // The tick hook only exists to DRIVE the animation: once the progression
    // is settled (running() == false) the value is constant, the decorator
    // data is up to date, and per-frame view->damage() would keep the whole
    // output re-rendering forever (100% CPU). Drop the effect whenever the
    // animation completes — in BOTH directions.
    if (!state.running())
    {
        if (output && tick_installed_)
        {
            output->render->rem_effect(&tick_hook);
            tick_installed_ = false;
        }

        // Only the transformer removal is exclusive to the unshaded end:
        // dropping it there returns input to the client (port of pixdecor
        // pre_hook cleanup). A SHADED window keeps its transformer (it carries
        // the rolled-up geometry) — just stops ticking.
        if (!state.get_shade_direction())
        {
            auto tmgr = view->get_transformed_node();
            if (auto tr = tmgr->get_transformer<emerald_shade_t>(
                SHADE_TRANSFORMER_NAME))
            {
                tmgr->rem_transformer<emerald_shade_t>(tr);
            }
        }
    }
}
} // namespace emeraldwf
} // namespace wf
