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
        duration_opt)
    : wf::scene::view_2d_transformer_t(view_arg)
    , view(view_arg)
    , strip_h(strip_h_arg)
    , state(duration_opt)
{
    if (view_arg)
        output = view_arg->get_output();
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

void emerald_shade_t::set_strip_h(int h)
{
    strip_h = h;
}

void emerald_shade_t::set_bottom_strip_h(int h)
{
    bottom_strip_h = h;
}

void emerald_shade_t::init_animation(bool shade)
{
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
        view->damage();
}

void emerald_shade_t::tick()
{
    if (view == nullptr)
        return;

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
            deco->on_shade_progress(p);
        else if (shadow_update_)
            shadow_update_();
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
