// emerald-shade.hpp — Part 05: shade view transformer (frozen for 06).
//
// Port of pixdecor's `pixdecor_shade` (pixdecor/src/shade.hpp): a 2D view
// transformer that folds the view's surface into its top titlebar strip,
// animated by `emeraldwf/shade_duration`. Emerald flavor: the strip height
// is the EMERALD titlebar (or `csd_titlebar_height` for CSD views — no
// shadow margin, since emerald's shadow lives inside the frame extents).
//
// STRIP HEIGHT CONVENTION (part 06 finding 15): `strip_h` is the height of
// the visible top strip of the view's BOUNDING BOX. For a decorated view
// the frame (shadow + border + emerald titlebar) is part of the view's
// bounding box, so the plugin passes the TOP MARGIN (shadow + border +
// titlebar), not the titlebar alone; for CSD views (no emerald frame) it
// is the CSD titlebar height. Passing the bare titlebar collapses the
// window to a sliver of shadow/border with the titlebar barely visible.
//
// The public contract is per spec 05 §Contract. The pure animation
// bookkeeping (direction state machine) is factored into `ShadeState` so it
// can be tested headlessly without a GL context / live view.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <algorithm>

#include <wayfire/geometry.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/core.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/util/duration.hpp>

#include "emerald-decorator.hpp"  // real wayfire_toplevel_view type

namespace wf
{
namespace emeraldwf
{
using namespace wf::scene;
using namespace wf::animation;

constexpr char SHADE_TRANSFORMER_NAME[] = "emeraldwf-shade";

// ── Pure animation bookkeeping (testable headlessly) ────────────────────────
// A `simple_animation_t`-derived class that owns the `timed_transition_t`
// for the shade progression. The `duration_t` is driven by wayfire core (the
// output's render loop advances it); `operator double()` returns the
// smoothed progression value (0 = unshaded, 1 = fully shaded).
// Factored out of `emerald_shade_t` per spec 05 "Tests" so the direction
// state machine can be tested headlessly without a GL context.
class ShadeState : public simple_animation_t
{
  public:
    ShadeState()
        : ShadeState(nullptr)
    {}

    // Finding 22: the emeraldwf/shade_duration option must drive the
    // animation length — a nullptr (legacy) duration completes the
    // transition instantly, which is why the option "did nothing".
    // Review F2 (-Wshadow): parameter named `duration_opt` — the base
    // `timed_transition_t` has a `duration` member.
    explicit ShadeState(
        std::shared_ptr<wf::config::option_t<wf::animation_description_t>>
            duration_opt)
        : simple_animation_t(duration_opt)
    {
        // Initialize the progression transition (0 → 1).
        set(0.0, 1.0);
    }

    // Pixdecor semantics (pixdecor shade.hpp init_animation):
    //   - same direction as before  → no-op (already animating there)
    //   - running                   → reverse (mid-animation flip)
    //   - idle with wrong direction → reverse, then start
    //   - idle with right direction → just start
    // After the call `last_direction` always equals @shade.
    void init_animation(bool shade)
    {
        if (shade == last_direction)
            return;

        if (running())
        {
            reverse();
        }
        else
        {
            if ((shade && !get_direction()) ||
                (!shade && get_direction()))
            {
                reverse();
            }
            duration_t::start();
        }

        last_direction = shade;
    }

    double get_progression() const  // 0 = unshaded, 1 = fully shaded
    {
        return operator double();
    }

    bool get_shade_direction() const  // true = currently shaded
    {
        return last_direction;
    }

    bool last_direction = false;
};

class emerald_shade_t : public wf::scene::view_2d_transformer_t
{
  public:
    emerald_shade_t(::wayfire_toplevel_view view, int strip_h,
        std::shared_ptr<wf::config::option_t<wf::animation_description_t>>
            duration);
    ~emerald_shade_t() override;

    // The strip height in view-bounding-box coords (see ctor).
    void set_strip_h(int h);
    // The bottom strip height: the SHADOW-ONLY part of the bottom margin
    // (bottom margin minus the bottom border). A shaded window keeps the
    // titlebar plus this glow below it — the bottom border is part of the
    // rolled-up frame and must not show (requirement: nothing but the soft
    // shadow may start below the titlebar's lower edge). 0 (the default)
    // reproduces the legacy top-strip-only fold.
    void set_bottom_strip_h(int h);
    void init_animation(bool shade);      // animate to 1.0 (shaded) / 0.0
    // Finding 26: defined inline (here, not emerald-shade.cpp) so the
    // headless-compiled emerald-decorator.cpp can read the progression in
    // make_margins_input() without linking the scene TU.
    double get_progression() const       // 0 = unshaded, 1 = fully shaded
    {
        return state.get_progression();
    }
    bool get_direction() const           // true = currently shaded
    {
        return state.get_shade_direction();
    }

    // Driven by the output's pre-render hook (pixdecor pattern): advances the
    // animation, propagates progress to the decorator (margins + shadow
    // custom data), and drops the transformer when the animation finishes
    // unshaded.
    void tick();

    // Review R4: CSD views have no decorator for tick() to propagate the
    // (progress-dependent) shadow margins to — the plugin registers this
    // callback at transformer creation so a standalone shadow plugin sees
    // the live margins through the animation, not just the pre-shade ones
    // exported at animation start. No-op for decorated views (the decorator
    // branch in tick() wins).
    void set_shadow_update(std::function<void()> update)
    {
        shadow_update_ = std::move(update);
    }

  private:
    // ── render instance: re-runs the child texture with the collapsed height
    // (port of pixdecor_shade::simple_node_render_instance_t, minus the
    // shadow-margin term — emerald's shadow is inside the frame).
    class shade_render_instance_t :
        public wf::scene::transformer_render_instance_t<
            wf::scene::transformer_base_node_t>
    {
        wf::signal::connection_t<wf::scene::node_damage_signal> on_node_damaged =
            [=] (wf::scene::node_damage_signal *ev)
        {
            push_to_parent(ev->region);
        };

        emerald_shade_t *self;
        damage_callback push_to_parent;

      public:
        // Review F2 (-Wshadow): parameter named `owner` — the member it
        // initializes is named `self`.
        shade_render_instance_t(emerald_shade_t *owner,
                                damage_callback push_damage,
                                wf::output_t *shown_on)
            : wf::scene::transformer_render_instance_t<
                  wf::scene::transformer_base_node_t>(owner, push_damage,
                  shown_on)
        {
            this->self = owner;
            this->push_to_parent = push_damage;
            owner->connect(&on_node_damaged);
        }

        void schedule_instructions(
            std::vector<render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::regionf_t& damage)
        {
            instructions.push_back(render_instruction_t{
                .instance = this,
                .target   = target,
                .damage   = damage & self->get_bounding_box(),
            });
        }

        void render(const wf::scene::render_instruction_t& data)
        {
            const auto full_box = self->get_children_bounding_box();
            auto src_tex =
                wf::scene::transformer_render_instance_t<
                    wf::scene::transformer_base_node_t>::get_texture(1.0);

            const double p = self->get_progression();
            const int top  = self->strip_h;        // top margin (shadow+border+titlebar)
            const int bot  = self->bottom_strip_h; // bottom margin (shadow+border)
            const int H    = full_box.height;
            const int mid0 = std::max(0, H - top - bot); // unshaded client (middle)
            const int mid  = (int)(mid0 * (1.0 - p));    // collapses as p → 1

            // Roll-up shift: the full texture is rendered shifted UP by this
            // much, so the middle (client) folds away and the bottom strip
            // moves up to sit right below the top strip. The client and the
            // bottom strip share the shift (they stay contiguous — the
            // emerald bottom shadow/border is preserved under the titlebar,
            // matching emerald). 0 at p = 0 (the full frame is shown), and
            // H - top - bot at p = 1 (only the two strips remain).
            const int shift = H - bot - top - mid;

            // Render the full texture (shifted up by `band_shift`), clipped to
            // the band [dy, dy + dh) relative to the frame top. The scissor
            // keeps the three bands disjoint; the shift + scissor together
            // produce the roll-up. (Same technique as the pre-fix client
            // fold — orientation-agnostic, no subtexture selection.)
            auto draw_band = [&](int dy, int dh, int band_shift)
            {
                if (dh <= 0)
                    return;
                auto region = data.damage;
                region &= wf::geometry_t{full_box.x, full_box.y + dy,
                                         full_box.width, (double)dh};
                if (region.empty())
                    return;
                wf::gles::run_in_context([&]
                {
                    wf::gles::bind_render_buffer(data.target);
                    data.pass->custom_gles_subpass(data.target, [&]
                    {
                        wf::gles::for_each_scissor_rect(
                            data.target, region, [=] ()
                        {
                            OpenGL::render_transformed_texture(
                                wf::gles_texture_t{src_tex},
                                {float(full_box.x),
                                 float(full_box.y - band_shift),
                                 float(full_box.x + full_box.width),
                                 float(full_box.y + H - band_shift)}, {},
                                wf::gles::render_target_orthographic_projection(data.target),
                                glm::vec4(1.0), 0);
                        });
                    });
                });
            };

            // 1. Top strip [0, top): unchanged, at the frame top.
            draw_band(0, top, 0);
            // 2. Client (middle) [top, top + mid): folds up.
            draw_band(top, mid, shift);
            // 3. Bottom shadow [top + mid, top + mid + bot): moved up to sit
            //    right below the collapsed client — the frame's bottom
            //    SHADOW band (border excluded) becomes the soft glow under
            //    the titlebar of the shaded window.
            draw_band(top + mid, bot, shift);
        }
    };

  public:
    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *shown_on) override
    {
        instances.push_back(std::make_unique<shade_render_instance_t>(
            this, push_damage, shown_on));
    }

    // Input only lands on the visible (collapsed) region: the top strip +
    // the collapsed client + the bottom strip (the emerald bottom shadow
    // stays visible when shaded). (Port of pixdecor find_node_at.)
    std::optional<wf::scene::input_node_t> find_node_at(
        const wf::pointf_t& at) override
    {
        auto bbox = this->get_children_bounding_box();
        const double p = get_progression();
        const int top = strip_h;
        const int bot = bottom_strip_h;
        const int mid0 = std::max(0, (int)bbox.height - top - bot);
        const int mid  = (int)(mid0 * (1.0 - p));
        // Visible height = the three bands: top strip + collapsed client +
        // bottom strip (the emerald bottom shadow stays visible when shaded).
        const double visible_h = (double)(top + mid + bot);
        if ((at.y - bbox.y < visible_h) && (at.y - bbox.y > 0.0))
        {
            return wf::scene::floating_inner_node_t::find_node_at(at);
        }

        return {};
    }

  private:
    ::wayfire_toplevel_view view;
    int strip_h; // top of view bbox = shaded strip (see ctor comment)
    int bottom_strip_h = 0; // bottom margin kept visible when shaded
    ShadeState state;
    // Review R4: per-tick CSD shadow-margin export (see set_shadow_update).
    std::function<void()> shadow_update_;
    wf::output_t *output = nullptr;
    wf::effect_hook_t tick_hook;
    // True while the pre-render tick hook is installed on the output.
    // tick() drops it when the animation settles (in BOTH directions — it is
    // only needed to drive the progression), so init_animation() must
    // re-install it for any subsequent animation.
    bool tick_installed_ = false;
};

} // namespace emeraldwf
} // namespace wf
