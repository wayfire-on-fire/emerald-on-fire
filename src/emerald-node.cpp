// emerald-node.cpp — Part 04: scene node, rendering, input, actions.
//
// The node composites Part 02's `emeraldwf_render_frame()` cairo surface into
// a GL texture, and routes pointer/touch through Part 03's EmeraldLayout to
// wayfire WM actions (overview D11).
//
// This TU uses the REAL wayfire toplevel-view type, so set
// EMERALDWF_HAVE_WAYFIRE_VIEW before including any emerald header to
// suppress the placeholder forward-declaration in emerald-types.hpp.
#define EMERALDWF_HAVE_WAYFIRE_VIEW 1

#include "emerald-node.hpp"

#include <algorithm>
#include <memory>
#include <string>

#include <linux/input-event-codes.h>

#include <wayfire/nonstd/wlroots.hpp>
// wlroots headers carry no C++ linkage guards; wrap in extern "C" so the
// symbols resolve against libwlroots (a bare include would C++-mangle
// wlr_xcursor_get_resize_name and the .so would fail to dlopen).
extern "C"
{
#include <wlr/xcursor.h>
}
#include <wayfire/geometry.hpp>
#include <wayfire/region.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-input.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/txn/transaction.hpp>
#include <wayfire/core.hpp>
#include <wayfire/opengl.hpp>
#include <cairo.h>
#include <wayfire/plugins/common/cairo-util.hpp>
#include <wayfire/scene-operations.hpp>

#include <pango/pango.h>
#include <pango/pangocairo.h>

#include "emerald-button-fade.hpp"
#include "emerald-decorator.hpp"
#include "emerald-engine.hpp"
#include "emerald-settings.hpp"
#include "emerald-shade.hpp"

namespace wf
{
namespace emeraldwf
{
// ── Coordinate helpers ──────────────────────────────────────────────────────
// The node's bounding box sits at offset {-margins.left, -margins.top} in the
// surface-root coordinate system (spec 04 §1): the frame's top-left is that far
// above-left of the client's top-left. Frame (node-local) coords = parent
// coords - offset.
static wf::pointf_t node_offset_of(const emerald_decorator_t & deco)
{
    // The frame's top-left in parent (surface-root) coords is
    // {-margins.left, -margins.top} (see frame_box_for) — the node's local
    // origin sits at that point, so the offset is NEGATIVE. A positive
    // offset here shifted every input coordinate by (-left, -top), landing
    // titlebar presses above the frame and killing move/resize (part 06).
    //
    // Uses pending().margins (the value wayfire itself uses to size the
    // client surface as shrink(geometry, margins)) so the input offset and
    // the bounding-box origin can never disagree.
    auto view = deco.view_for_offsets();
    if (view == nullptr)
    {
        return {0, 0};
    }

    const auto & pending = view->toplevel()->pending();
    return {-(double)pending.margins.left, -(double)pending.margins.top};
}

static wf::pointf_t to_local_of(const emerald_decorator_t & deco,
    wf::pointf_t parent)
{
    auto off = node_offset_of(deco);
    return {parent.x - off.x, parent.y - off.y};
}

// ── Node-local coord conversion + live view (used by state updates) ─────────
// frame coords = parent (surface-root) coords minus the node offset
// {-margins.left, -margins.top}; reuses to_local_of() for the math.
wf::pointf_t emerald_node_t::to_local(wf::pointf_t parent) const
{
    if (deco == nullptr)
    {
        return parent;
    }

    return to_local_of(*deco, parent);
}

// Finding 19b/19c: the layout's regions (buttons, titlebar band, resize
// bands) are in FULL-EXTENT frame coords (the engines draw against them,
// and layout.build() is fed full-extent ctx.width/height); the node surface
// is the CROPPED frame when maximized. to_local() alone lands input events
// too far toward the frame's outer edge, so the titlebar band (y >=
// top_space) and the buttons never register — the whole titlebar is inert
// on maximized+cropped windows. Add the surface's full-extent offset back:
// that is the TOTAL crop (shadow + border parts) — every layout region
// sits to the right/below of the cropped inner (border) band, and the
// render's reflow (the piecewise blit in emerald-engine.cpp) shifts that
// content by exactly the total crop.
wf::pointf_t emerald_node_t::to_local_full_extent(wf::pointf_t parent) const
{
    auto local = to_local(parent);
    if (deco == nullptr)
    {
        return local;
    }

    auto view = view_of();
    if (view == nullptr)
    {
        return local;
    }

    const auto & pending = view->toplevel()->pending();
    if (pending.tiled_edges != TILED_EDGES_ALL)
    {
        return local;
    }

    auto theme = deco->theme_ref();
    if ((theme == nullptr) || !theme->valid())
    {
        return local;
    }

    const auto *ws = theme->ws();
    if (ws == nullptr)
    {
        return local;
    }

    const auto crop = maximized_crop(*ws, deco->maximized_borders(),
        deco->maximized_shadows());
    return {local.x + (double)crop.left, local.y + (double)crop.top};
}

// The live toplevel view via the decorator (null if the decorator is gone).
wayfire_toplevel_view emerald_node_t::view_of() const
{
    if (deco == nullptr)
    {
        return nullptr;
    }

    return deco->view_for_offsets();
}

// ── Pure bounding box (spec 04 Tests) ───────────────────────────────────────
wf::geometry_t emerald_node_t::node_bounding_box(const emerald_decorator_t & deco)
{
    auto view = deco.view_for_offsets();
    if (view == nullptr)
    {
        return wf::geometry_t{0, 0, 0, 0};
    }

    const auto & pending = view->toplevel()->pending();

    // The frame is the CLIENT region expanded by the margins. The client
    // region in the node's parent (surface-root) coords is
    // {0, 0, client_w, client_h}: the surface root node IS the client's
    // top-left origin, so the client sits at (0,0) in its coordinate system.
    //
    // The client size is shrink(pending().geometry, pending().margins) — the
    // exact pair wayfire uses to size the client surface (xdg-toplevel.cpp:
    // `desired_size = shrink(desired_state.geometry, desired_state.margins)`).
    // pending().geometry is the FRAME (client + margins) at all times,
    // including after an interactive resize (the resize plugin writes the
    // frame geometry and never touches pending().margins).
    //
    // CRITICAL: do NOT derive the client from `sroot->get_bounding_box()`.
    // This node is attached as a child of the surface root
    // (add_back(sroot, node) in attach_node), and the surface root is a
    // translation node whose get_bounding_box() is the union of its children —
    // which includes us. Querying it here recurses
    // (get_bounding_box → node_bounding_box → sroot->get_bounding_box → …)
    // until the stack overflows (SEGV on any window map; part 06). The
    // parent-free derivation (pending geometry/margins → client → frame box)
    // is the pure frame_box_from_pending(), tested in test_render_node.cpp.
    auto box = frame_box_from_pending({
                (int)pending.geometry.width, (int)pending.geometry.height,
                (int)pending.margins.left, (int)pending.margins.right,
                (int)pending.margins.bottom, (int)pending.margins.top});

    return wf::geometry_t{(double)box.x, (double)box.y,
        (double)box.w, (double)box.h};
}

// ── Pure hit-test (spec 04 Tests) ────────────────────────────────────────────
std::optional<wf::scene::input_node_t> emerald_node_t::hit_test(
    const emerald_decorator_t & deco, const wf::pointf_t & at)
{
    auto view = deco.view_for_offsets();
    if ((view == nullptr) || deco.is_csd())
    {
        return std::nullopt;
    }

    const auto & pending = view->toplevel()->pending();
    if (pending.fullscreen)
    {
        return std::nullopt;
    }

    auto frame = node_bounding_box(deco);
    wf::regionf_t frame_region{frame};
    if (!frame_region.contains_pointf(at))
    {
        return std::nullopt;
    }

    // The frame region (client + titlebar + borders + resize bands) all live
    // inside the extents — emerald's shadow is inside the frame, so no extra
    // radius is needed (contrast pixdecor, which offsets by shadow thickness).
    return wf::scene::input_node_t{
        .node = deco.node_for_hit_test(),
        .local_coords = to_local_of(deco, at),
    };
}

// ── Constructor / destructor ────────────────────────────────────────────────
emerald_node_t::emerald_node_t(std::shared_ptr<emerald_decorator_t> deco_) :
    node_t(false)
    , deco(std::move(deco_))
{
    // Finding 43: buttons start idle (counter 0 = non-hovered look, the
    // upstream reset state); no fade runs until a hover/press event.
    button_fade_cfg_ = button_fade_config();
}

emerald_node_t::~emerald_node_t()
{
    fade_timer.disconnect();
    if (cache.surface != nullptr)
    {
        cairo_surface_destroy(cache.surface);
        cache.surface = nullptr;
    }

    // Review N2: the measure context is owned here (one 1×1 surface +
    // PangoLayout per node, reused across rebuilds — see render_frame).
    if (measure_pl != nullptr)
    {
        g_object_unref(measure_pl);
        measure_pl = nullptr;
    }

    if (measure_cr != nullptr)
    {
        cairo_destroy(measure_cr);
        measure_cr = nullptr;
    }

    if (measure_surface != nullptr)
    {
        cairo_surface_destroy(measure_surface);
        measure_surface = nullptr;
    }
}

// ── State updates ───────────────────────────────────────────────────────────
void emerald_node_t::damage_frame()
{
    // Damage our own node with the full frame region (parent/surface-root
    // coords). This is what paints the title bar; view->damage() alone only
    // covers the client area.
    wf::scene::damage_node(shared_from_this(), get_bounding_box());
}

void emerald_node_t::resize()
{
    size_dirty = true;
    if (auto view = view_of())
    {
        view->damage();
    }

    damage_frame();
}

// Review F2 (-Wall): `active` is unused (the node recomputes from the
// view's state on render); unnamed in the definition, named in the header.
void emerald_node_t::set_active(bool /*active*/)
{
    active_dirty = true;
    if (auto view = view_of())
    {
        view->damage();
    }

    damage_frame();
}

void emerald_node_t::set_title(std::string title)
{
    cache.title = std::move(title);
    title_dirty = true;
    if (auto view = view_of())
    {
        view->damage();
    }

    damage_frame();
}

void emerald_node_t::set_theme_dirty()
{
    theme_dirty = true;
    // Finding 43: a theme swap may change the fade config (steps, pulse,
    // on/off) — re-read it and reset all buttons to idle (upstream
    // reset_buttons_bg_and_fade).
    button_fade_reset();
    if (auto view = view_of())
    {
        view->damage();
    }

    damage_frame();
}

// Finding 43 — see emerald-button-fade.hpp for the semantics.
ButtonFadeConfig emerald_node_t::button_fade_config() const
{
    ButtonFadeConfig cfg;
    if (deco == nullptr)
    {
        return cfg;
    }

    if (auto theme = deco->theme_ref())
    {
        if (auto ws = theme->ws())
        {
            cfg.enabled   = ws->use_button_fade && ws->use_pixmap_buttons;
            cfg.pulse     = ws->button_fade_pulse_len_steps > 0;
            cfg.num_steps = std::max(1, (int)ws->button_fade_num_steps);
            cfg.pulse_len_steps  = ws->button_fade_pulse_len_steps;
            cfg.pulse_wait_steps = ws->button_fade_pulse_wait_steps;
            cfg.step_ms = std::max(1, (int)ws->button_fade_step_duration);
        }
    }

    return cfg;
}

void emerald_node_t::button_fade_reset()
{
    fade_timer.disconnect();
    button_fade_cfg_ = button_fade_config();
    for (int b = 0; b < B_T_COUNT; b++)
    {
        button_fade_state[b].counter   = 0;
        button_fade_state[b].pulsating = false;
    }
}

void emerald_node_t::button_fade_hover(int b, bool now_hovered)
{
    if (!button_fade_cfg_.enabled || (b < 0) || (b >= B_T_COUNT))
    {
        return;
    }

    ButtonFadeState before = button_fade_state[b];
    button_fade_on_hover(button_fade_cfg_, button_fade_state[b], now_hovered);
    if ((button_fade_state[b].counter == before.counter) &&
        (button_fade_state[b].pulsating == before.pulsating))
    {
        return;
    }

    theme_dirty = true;
    if (auto view = view_of())
    {
        view->damage();
    }

    damage_frame();
    if (button_fade_state[b].counter != 0)
    {
        button_fade_arm();
    }
}

void emerald_node_t::button_fade_pressed(int b)
{
    if (!button_fade_cfg_.enabled || (b < 0) || (b >= B_T_COUNT))
    {
        return;
    }

    button_fade_on_press(button_fade_state[b]);
    theme_dirty = true; // the pressed surface column must be drawn now
    if (auto view = view_of())
    {
        view->damage();
    }

    damage_frame();
}

void emerald_node_t::button_fade_arm()
{
    if (fade_timer.is_connected())
    {
        return; // one timer for all buttons (upstream: one g_timeout)
    }

    fade_timer.set_timeout(button_fade_cfg_.step_ms,
        [this] { button_fade_tick_all(); });
}

void emerald_node_t::button_fade_tick_all()
{
    if (deco == nullptr)
    {
        return;
    }

    bool any_changed   = false;
    bool any_animating = false;
    for (int b = 0; b < B_T_COUNT; b++)
    {
        auto r = button_fade_tick(button_fade_cfg_, button_fade_state[b]);
        any_changed   |= r.changed;
        any_animating |= r.animating;
    }

    if (any_changed)
    {
        theme_dirty = true;
        if (auto view = view_of())
        {
            view->damage();
        }

        damage_frame();
    }

    if (any_animating)
    {
        button_fade_arm(); // one-shot timer — reschedule the next step
    }
}

// Finding 31: set_shade_progress() was never called (dead); the render path
// reads the live transformer progression directly (see render_frame).

// ── wf::scene::node_t ───────────────────────────────────────────────────────
std::optional<wf::scene::input_node_t> emerald_node_t::find_node_at(
    const wf::pointf_t& at)
{
    if (deco == nullptr)
    {
        return std::nullopt;
    }

    return hit_test(*deco, at);
}

wf::geometry_t emerald_node_t::get_bounding_box()
{
    if (deco == nullptr)
    {
        return wf::geometry_t{0, 0, 0, 0};
    }

    return node_bounding_box(*deco);
}

void emerald_node_t::gen_render_instances(
    std::vector<wf::scene::render_instance_uptr>& instances,
    wf::scene::damage_callback push_damage, wf::output_t* /*output*/)
{
    class decoration_render_instance_t : public wf::scene::render_instance_t
    {
        emerald_node_t *self;
        wf::scene::damage_callback push_damage;

        wf::signal::connection_t<wf::scene::node_damage_signal> on_surface_damage =
            [=] (wf::scene::node_damage_signal *data)
        {
            push_damage(data->region);
        };

      public:
        // Review F2 (-Wshadow): parameters `owner`/`push_damage_cb` — the
        // members they initialize are named `self`/`push_damage`.
        decoration_render_instance_t(emerald_node_t *owner,
            wf::scene::damage_callback push_damage_cb) :
            self(owner)
            , push_damage(push_damage_cb)
        {
            owner->connect(&on_surface_damage);
        }

        void schedule_instructions(
            std::vector<wf::scene::render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::regionf_t& damage) override
        {
            auto our_region = self->get_bounding_box();
            wf::regionf_t our_damage = damage & our_region;
            if (!our_damage.empty())
            {
                instructions.push_back(wf::scene::render_instruction_t{
                            .instance = this,
                            .target   = target,
                            .damage   = std::move(our_damage),
                        });
            }
        }

        void render(const wf::scene::render_instruction_t& data) override
        {
            self->render_frame(data);
        }
    };

    instances.push_back(
        std::make_unique<decoration_render_instance_t>(this, push_damage));
}

void emerald_node_t::render_frame(
    const wf::scene::render_instruction_t& data)
{
    auto view = view_of();
    if ((view == nullptr) || (deco == nullptr))
    {
        return;
    }

    auto theme = deco->theme_ref();
    if ((theme == nullptr) || !theme->valid())
    {
        static bool logged_theme = false;
        if (!logged_theme)
        {
            LOGW("emeraldwf: render_frame: theme invalid (theme=%p) — "
                 "no decoration rendered; check ~/.emerald + engine load",
                static_cast<const void*>(theme.get()));
            logged_theme = true;
        }

        return;
    }

    auto box = get_bounding_box();
    int fw = (int)box.width, fh = (int)box.height;
    auto & pending    = view->toplevel()->pending();
    // Finding 27: no frame on fullscreen windows (margins are 0 → the frame
    // box equals the client box; drawing would composite the emerald
    // titlebar over the fullscreen client). The pure guard is tested in
    // test_render_node.cpp.
    if (!frame_should_render(pending.fullscreen, fw, fh))
    {
        return;
    }

    // Measure the title with Pango before emeraldwf_render_frame (spec §2).
    // Review N2: the 1×1 surface + PangoLayout are created ONCE per node and
    // reused across rebuilds (the old code allocated them on every rebuild);
    // only the font description + text are (re)set per render.
    std::string title = view->get_title();
    int title_width   = 0;
    auto ws = theme->ws();
    if ((ws != nullptr) && (ws->font_desc != nullptr))
    {
        if (measure_cr == nullptr)
        {
            measure_surface = cairo_image_surface_create(
                CAIRO_FORMAT_ARGB32, 1, 1);
            measure_cr = cairo_create(measure_surface);
            measure_pl = pango_cairo_create_layout(measure_cr);
        }

        pango_layout_set_font_description(measure_pl, ws->font_desc);
        pango_layout_set_text(measure_pl, title.c_str(), (int)title.size());
        int w = 0, h = 0;
        pango_layout_get_pixel_size(measure_pl, &w, &h);
        title_width = w;
    }

    layout.title_text_width_ = title_width;

    // Rebuild the DecorContext (state, active, client size, layout fields).
    auto & ctx = deco->ctx();
    // pending() (from the guard above) is the toplevel's pending state.
    // pending().geometry is the FRAME (client + margins); the client (base)
    // geometry is the frame shrunk by the margins. The engine needs the real
    // client size to position the title bar / borders correctly.
    auto base_geom =
        wf::shrink_geometry_by_margins(pending.geometry, pending.margins);
    ctx.client_width  = base_geom.width;
    ctx.client_height = base_geom.height;
    ctx.title  = title;
    ctx.active = view->activated;
    ctx.state  = emerald_state_from_view(pending, view->sticky);
    // Finding 31: EWS_SHADED drives the layout's shaded resize-band logic
    // (emerald main.c update_event_windows: shaded → only the middle row's
    // side bands are live). The state bit is not derivable from the toplevel
    // state — it comes from the live shade transformer (progress > 0).
    if (auto tr = deco->shade())
    {
        if (tr->get_progression() > 0.0)
        {
            ctx.state |= EWS_SHADED;
        }
    }

    ctx.decorated = !deco->is_csd();
    // Finding 19: feed the plugin's maximized options to the render crop
    // (emeraldwf_render_frame) — same values margins_for() uses.
    ctx.maximized_borders = deco->maximized_borders();
    ctx.maximized_shadows = deco->maximized_shadows();
    // The layout (button positions) and the engine draw against the
    // FULL-EXTENT frame; the node's bbox (fw × fh) already excludes the
    // maximized crop, so add it back for ctx.
    decor_crop_t crop = decor_crop_t{};
    if ((ws != nullptr) && (pending.tiled_edges == TILED_EDGES_ALL))
    {
        crop = maximized_crop(*ws, deco->maximized_borders(),
            deco->maximized_shadows());
    }

    ctx.width  = fw + crop.left + crop.right;
    ctx.height = fh + crop.top + crop.bottom;
    // Output scale (render_target_t.scale): the frame surface is rendered at
    // physical resolution (logical * scale) so the decoration composites
    // crisply on scaled outputs — the same contract wayfire's native deco
    // follows (deco-subsurface.cpp scales its title surface by
    // data.target.scale). The bounding box stays logical; wayfire's
    // logical->framebuffer dst-box mapping fills the physical surface 1:1.
    ctx.scale = data.target.scale;

    // Button interaction state (emerald's event-window flags): the hovered
    // button renders its S_ACTIVE_HOVER surface column, a pressed button its
    // S_ACTIVE_PRESS one (get_b_state). Without this the buttons never change
    // on hover/press (part 06 regression: "buttons used to change color when
    // hovered").
    for (int b = 0; b < B_T_COUNT; ++b)
    {
        ctx.button_states[b] = 0;
    }

    if (last_hover_button >= 0)
    {
        ctx.button_states[last_hover_button] |= IN_EVENT_WINDOW;
    }

    if (pressed_button >= 0)
    {
        ctx.button_states[pressed_button] |=
            (IN_EVENT_WINDOW | PRESSED_EVENT_WINDOW);
    }

    // Finding 43: publish the current per-button crossfade alphas.
    // Stable-state renders don't tick (the timer only runs while a
    // counter moves), so the state was last set by an event/reset.
    for (int b = 0; b < B_T_COUNT; ++b)
    {
        if (button_fade_cfg_.enabled)
        {
            auto a = button_fade_alphas(button_fade_state[b],
                button_fade_cfg_.num_steps);
            ctx.button_fade_normal[b] = a.normal;
            ctx.button_fade_hover[b]  = a.hover;
        } else
        {
            ctx.button_fade_normal[b] = 1.0;
            ctx.button_fade_hover[b]  = 0.0;
        }
    }

    layout.build(*theme, ctx);
    ctx.fs = ctx.active ? theme->fs_active() : theme->fs_inactive();

    bool active = view->activated;

    bool rebuild = theme_dirty || active_dirty || title_dirty || size_dirty ||
        needs_rebuild(cache, active, title, fw, fh,
        data.target.scale, last_hover_button);

    // The frame's GL texture is derived from the cached cairo surface, which
    // is immutable once rendered — so the texture stays valid until a rebuild
    // replaces the surface. On a page with continuous client damage (video)
    // the frame node is rendered at the client's framerate with an UNCHANGED
    // surface; creating a fresh texture on every such pass re-uploads the
    // whole frame buffer to the GPU per frame (the video-page CPU
    // regression). pick_frame_texture() decides: reuse on hit, create on
    // (re)build.
    std::shared_ptr<wf::texture_t> tex;
    if (rebuild)
    {
        theme_dirty = active_dirty = title_dirty = size_dirty = false;
        cairo_surface_t *surface = emeraldwf_render_frame(*theme, ctx);
        if (surface == nullptr)
        {
            static bool logged_nosurf = false;
            if (!logged_nosurf)
            {
                LOGW("emeraldwf: render_frame: emeraldwf_render_frame returned "
                     "nullptr (engine='%s') — engine .so may have failed to "
                     "load; check 'emerald_engine_load' log line",
                    theme->engine().c_str());
                logged_nosurf = true;
            }

            return;
        }

        auto surface_tex = wf::owned_texture_t{surface};
        tex = surface_tex.get_texture();
        if (cache.surface != nullptr)
        {
            cairo_surface_destroy(cache.surface);
            cache.surface = nullptr;
        }

        cache.surface = surface;
        cache.active  = active;
        cache.title   = title;
        cache.width   = fw;
        cache.height  = fh;
        cache.scale   = data.target.scale;
        cache.hover_button = last_hover_button;
        // cache_texture owns the GL texture across frames; cache.texture is
        // its opaque handle for pick_frame_texture().
        cache_texture = tex;
        cache.texture = tex.get();
    } else if (cache.surface != nullptr)
    {
        auto pick = pick_frame_texture(cache, cache.surface);
        if (pick.must_create)
        {
            // Defensive: surface with no recorded texture (e.g. texture
            // creation failed on the rebuild) — recreate it.
            auto surface_tex = wf::owned_texture_t{cache.surface};
            tex = surface_tex.get_texture();
            cache_texture = tex;
            cache.texture = tex.get();
        } else
        {
            // Cache hit: blit the existing texture — no re-upload.
            tex = cache_texture;
        }
    }

    if (tex != nullptr)
    {
        data.pass->add_texture(tex, data.target, box, data.damage);
    }
}

// ── pointer/touch interaction ───────────────────────────────────────────────
void emerald_node_t::handle_pointer_enter(wf::pointf_t position)
{
    last_pointer_pos = position;
    update_hover(to_local_full_extent(position));
}

void emerald_node_t::handle_pointer_leave()
{
    if (last_hover_button != -1)
    {
        // Finding 43: the pointer left the node — the hovered button fades
        // back to its non-hovered look.
        button_fade_hover(last_hover_button, false);
        last_hover_button  = -1;
        cache.hover_button = -1;
        theme_dirty = true;
        if (auto view = view_of())
        {
            view->damage();
        }

        damage_frame();
    }

    // Finding 19/20: the original requires the same window for a
    // double-click; leaving the node invalidates both the pending first
    // titlebar click and an armed pending move.
    titlebar_input = titlebar_input_state_t{};
    // We no longer own the pointer → restore the default cursor.
    wf::get_core().set_cursor("default");
}

void emerald_node_t::handle_pointer_motion(wf::pointf_t pointer_position,
    uint32_t /*time_ms*/)
{
    last_pointer_pos = pointer_position;
    // Finding 20: an armed titlebar move commits only on real motion —
    // commit_px of travel from the press position. The commit grabs the
    // pointer (WM move gesture); afterwards this node no longer receives
    // events, which is fine: a drag is not a double-click.
    if (on_titlebar_motion(titlebar_input, pointer_position.x,
        pointer_position.y))
    {
        handle_move();
    }

    update_hover(to_local_full_extent(pointer_position));
}

void emerald_node_t::handle_pointer_button(
    const wlr_pointer_button_event& event)
{
    if (auto view = view_of())
    {
        // Button/axis events carry no position; use the last known pointer
        // position (updated by enter/motion).
        auto local = to_local_full_extent(last_pointer_pos);
        int x = (int)local.x, y = (int)local.y;
        bool pressed = (event.state == WL_POINTER_BUTTON_STATE_PRESSED);

        if (event.button == BTN_LEFT)
        {
            if (pressed)
            {
                auto ctx = deco->ctx();
                int b    = layout.hit_button(ctx, x, y, view->activated);
                if (b >= 0)
                {
                    pressed_button = b;
                    // Finding 43: a pressed button stops its fade
                    // (counter → 0; the pressed column draws at full alpha).
                    button_fade_pressed(b);
                    // Redraw the S_ACTIVE_PRESS surface column while held.
                    theme_dirty = true;
                    view->damage();
                    damage_frame();
                } else
                {
                    auto edges = layout.resize_edges(ctx, x, y);
                    if (edges != 0)
                    {
                        // A border press is not a titlebar press: it must not
                        // count toward double-click (finding 19) nor arm a
                        // pending move (finding 20).
                        titlebar_input.dbl.last_press_ms = 0;
                        titlebar_input.pending_move = false;
                        handle_resize(edges);
                    } else if (layout.in_titlebar(ctx, x, y))
                    {
                        // Finding 19/20: the original emerald acts on the
                        // server-side GDK_2BUTTON_PRESS; wlr has no such
                        // event, so synthesise it — a second titlebar press
                        // within the timeout fires the configured
                        // double_click_action. The press must NOT commit the
                        // WM move gesture up front (finding 20):
                        // move_request() would grab the pointer and the
                        // double-click's second press would never reach the
                        // node. The pending move commits on real motion
                        // (handle_pointer_motion).
                        switch (on_titlebar_press(titlebar_input,
                            last_pointer_pos.x,
                            last_pointer_pos.y,
                            event.time_msec))
                        {
                          case titlebar_press_result::DOUBLE_CLICK:
                            handle_click_action(
                                layout.double_click_action(ctx));
                            break;

                          case titlebar_press_result::ARM_MOVE:
                            // Armed — nothing to do until motion.
                            break;
                        }
                    }
                }
            } else
            {
                // Finding 20: a left release drops a never-committed
                // pending move (a click, not a drag).
                on_button_release(titlebar_input);
                if (pressed_button >= 0)
                {
                    int b = layout.hit_button(deco->ctx(), x, y, view->activated);
                    if (b == pressed_button)
                    {
                        handle_button(b);
                        // Finding 43: the pointer is still on the button —
                        // re-apply the hover rule (counter 0 → 1, fade in
                        // back to the hovered look from the pressed state).
                        button_fade_hover(b, true);
                    }

                    pressed_button = -1;
                    // Drop the S_ACTIVE_PRESS state (handle_button may have
                    // closed the view; a dangling dirty flag is harmless).
                    theme_dirty = true;
                    view->damage();
                    damage_frame();
                }
            }

            return;
        }

        // Middle-click on the titlebar (settings.ini middle_click_action, D11).
        // (Left-button double-click is synthesised in the BTN_LEFT press
        // path above — finding 19.)
        if (pressed && (event.button == BTN_MIDDLE) &&
            layout.in_titlebar(deco->ctx(), x, y))
        {
            handle_click_action(layout.middle_click_action(deco->ctx()));
        }
    }
}

void emerald_node_t::handle_pointer_axis(const wlr_pointer_axis_event& event)
{
    if (event.orientation == WL_POINTER_AXIS_VERTICAL_SCROLL)
    {
        auto local = to_local_full_extent(last_pointer_pos);
        int x = (int)local.x, y = (int)local.y;
        if (layout.in_titlebar(deco->ctx(), x, y))
        {
            handle_scroll_shade(event.delta);
        }
    }
}

// Review F2 (-Wall): `time_ms`/`finger_id` are unused by the node (no
// per-finger state); unnamed in the definitions, named in the header.
void emerald_node_t::handle_touch_down(uint32_t /*time_ms*/, int /*finger_id*/,
    wf::pointf_t position)
{
    last_pointer_pos = position;
    update_hover(to_local_full_extent(position));
}

void emerald_node_t::handle_touch_up(uint32_t /*time_ms*/, int /*finger_id*/,
    wf::pointf_t lift_off_position)
{
    auto local = to_local_full_extent(lift_off_position);
    if (auto view = view_of())
    {
        int x = (int)local.x, y = (int)local.y;
        int b = layout.hit_button(deco->ctx(), x, y, view->activated);
        if (b >= 0)
        {
            handle_button(b);
        }
    }
}

void emerald_node_t::handle_touch_motion(uint32_t /*time_ms*/, int /*finger_id*/,
    wf::pointf_t position)
{
    last_pointer_pos = position;
    update_hover(to_local_full_extent(position));
}

// ── WM actions (overview D11) ───────────────────────────────────────────────
void emerald_node_t::handle_move()
{
    if (auto view = view_of())
    {
        wf::get_core().default_wm->move_request(view);
    }
}

void emerald_node_t::handle_resize(uint32_t edges)
{
    if (auto view = view_of())
    {
        wf::get_core().default_wm->resize_request(view, edges);
    }
}

void emerald_node_t::handle_button(int btype)
{
    if (auto view = view_of())
    {
        const auto & pending = view->toplevel()->pending();
        bool maximized = (pending.tiled_edges == TILED_EDGES_ALL);
        switch (btype)
        {
          case B_T_CLOSE:
            view->close();
            break;

          case B_T_MAXIMIZE:
            if (maximized)
            {
                wf::get_core().default_wm->tile_request(view, 0);
            } else
            {
                wf::get_core().default_wm->tile_request(view, TILED_EDGES_ALL);
            }

            break;

          case B_T_MINIMIZE:
            wf::get_core().default_wm->minimize_request(view, true);
            break;

          case B_T_SHADE:
            if (deco)
            {
                deco->toggle_shade();
            }

            break;

          case B_T_SUPERMAXIMIZE:
        {
            // Toggle fullscreen (D11). The window manager's fullscreen_request
            // handles the transaction + workarea bookkeeping.
            auto out = wf::get_core().seat->get_active_output();
            wf::get_core().default_wm->fullscreen_request(view, out,
                !pending.fullscreen);
            break;
        }

          case B_T_STICKY:
            view->set_sticky(!view->sticky);
            break;

          case B_T_ABOVE:
            // Always-on-Top: not implemented — wayfire 0.11 exposes no
            // always-on-top request on a toplevel (only view_bring_to_front, a
            // one-shot raise, which is not a persistent "stay above"). The
            // button is hidden by emerald_button_actions[B_T_ABOVE] = EWA_ABOVE
            // (the node never sets EWA_ABOVE), so this case is unreachable in
            // practice; it is left here (no-op) so the supported-action set is
            // explicit and re-enabling is a one-line fill-in, not a hunt.
            break;

          case B_T_MENU:
            // Window menu (emerald opens a popup with the window actions):
            // not implemented — the port has no menu UI, so a visible menu
            // button would be a click no-op. Hidden by
            // emerald_button_actions[B_T_MENU] = EWA_MENU (the node never
            // sets EWA_MENU), so this case is unreachable in practice; it is
            // left here (no-op) so the supported-action set is explicit and
            // re-enabling is a one-line fill-in, not a hunt.
            break;

          default:
            break;
        }
    }
}

// Finding 21: emerald's send-to-back (main.c restack_window(win, Below))
// = bottom of the stack, no focus change. Wayfire 0.11 exposes no "lower"
// API (only view_bring_to_front); the scene graph IS the stacking order,
// so lower it the same way — mirror of wf::view_bring_to_front
// (view-impl.cpp:306), pushing the node to the END of each floating
// parent's children list.
static void view_send_to_back(wayfire_view view)
{
    wf::scene::node_t *node = view->get_root_node().get();
    wf::scene::node_t *damage_from = nullptr;
    bool actual_update = false;

    while (node->parent())
    {
        wf::scene::node_t *raw_parent = node->parent();
        auto parent =
            dynamic_cast<wf::scene::floating_inner_node_t*>(raw_parent);
        if (parent && !node->is_structure_node())
        {
            if (!damage_from)
            {
                damage_from = raw_parent;
            }

            if (parent->get_children().back().get() != node)
            {
                wf::scene::remove_child(node->shared_from_this());
                wf::scene::add_back(
                    std::static_pointer_cast<wf::scene::floating_inner_node_t>(
                        parent->shared_from_this()),
                    node->shared_from_this());
                actual_update = true;
            }
        }

        node = raw_parent;
    }

    if (damage_from && actual_update)
    {
        wf::scene::damage_node(damage_from->shared_from_this(),
            damage_from->get_bounding_box());
    }
}

void emerald_node_t::handle_click_action(int action)
{
    if (auto view = view_of())
    {
        // Finding 21: the mapping is pure + unit-tested
        // (map_titlebar_action) — the old raw switch silently dropped case
        // 3 (send to back).
        switch (map_titlebar_action(action))
        {
          case titlebar_action_request::TOGGLE_SHADE:
            if (deco)
            {
                deco->toggle_shade();
            }

            break;

          case titlebar_action_request::MAXIMIZE_TOGGLE:
            handle_button(B_T_MAXIMIZE);
            break;

          case titlebar_action_request::MINIMIZE:
            handle_button(B_T_MINIMIZE);
            break;

          case titlebar_action_request::SEND_TO_BACK:
            view_send_to_back(view);
            break;

          case titlebar_action_request::CLOSE:
            handle_button(B_T_CLOSE);
            break;

          case titlebar_action_request::NONE:
          default:
            break;
        }
    }
}

void emerald_node_t::handle_scroll_shade(double delta)
{
    // part 06 finding 17: the guards (fullscreen, titlebar_no_scroll_shade)
    // and the direction semantics live in scroll_shade() — shared with the
    // global axis binding (plugin.cpp), so plain titlebar scrolling and the
    // <ctrl>+<super>+wheel gesture behave identically.
    if (deco)
    {
        deco->scroll_shade(delta);
    }
}

void emerald_node_t::update_hover(wf::pointf_t local)
{
    if (auto view = view_of())
    {
        int x = (int)local.x, y = (int)local.y;
        int b = layout.hit_button(deco->ctx(), x, y, view->activated);
        if (b != last_hover_button)
        {
            // Finding 43: fire the upstream hover events for the fade
            // crossfade — the old button fades back to its non-hovered
            // look, the new one fades in (the timer starts if needed).
            if (last_hover_button >= 0)
            {
                button_fade_hover(last_hover_button, false);
            }

            last_hover_button  = b;
            cache.hover_button = b;
            if (b >= 0)
            {
                button_fade_hover(b, true);
            }

            theme_dirty = true;
            view->damage();
            damage_frame();
        }

        // Cursor shape (part 04, D11): pointer over a titlebar button,
        // resize over a border band, default elsewhere (the title bar shows
        // the default arrow; emerald does the same). EMERALDWF_EDGE_* values
        // match WLR_EDGE_* bit-for-bit.
        std::string cursor = "default";
        if (b >= 0)
        {
            cursor = "pointer";
        } else
        {
            uint32_t edges = layout.resize_edges(deco->ctx(), x, y);
            if (edges != 0)
            {
                cursor = wlr_xcursor_get_resize_name((wlr_edges)edges);
            }
        }

        wf::get_core().set_cursor(cursor);
    }
}
} // namespace emeraldwf
} // namespace wf
