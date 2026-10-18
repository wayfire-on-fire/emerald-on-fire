// emerald-decorator.cpp — per-toplevel decorator custom data.
// Part 01: constructor/destructor, margins, state mapping, signal stubs.
#include "emerald-decorator.hpp"

#include <wayfire/toplevel.hpp>
#include <wayfire/toplevel-view.hpp>

// Finding 26: make_margins_input() reads the live shade progression — the
// emerald_shade_t full type (inline get_progression()) is needed. The
// headless tests stub shade() to nullptr, so no scene symbols are linked.
#include "emerald-shade.hpp"

namespace wf
{
namespace emeraldwf
{

// ── State mapping (D4) ─────────────────────────────────────────────────────
// Maps wayfire toplevel view state → emerald's WnckWindowState bits.
//
//   fullscreen              → EWS_FULLSCREEN
//   tiled_edges == ALL      → EWS_MAXIMIZED_HORIZONTALLY | EWS_MAXIMIZED_VERTICALLY
//   view->sticky            → EWS_STICKY
//
// EWS_ABOVE / EWS_SHADED are not derivable from the wayfire state struct
// alone (Part 05 sets EWS_SHADED when shade animation runs).
//
// Takes (state, sticky) so it is testable without a live view: the plugin
// passes view->toplevel()->pending() and view->sticky.
EmeraldWindowState emerald_state_from_view(const wf::toplevel_state_t &state,
                                           bool sticky)
{
    uint32_t s = 0;
    if (state.fullscreen)
    {
        s |= EWS_FULLSCREEN;
    }
    if (state.tiled_edges == TILED_EDGES_ALL)
    {
        s |= EWS_MAXIMIZED_HORIZONTALLY | EWS_MAXIMIZED_VERTICALLY;
    }
    if (sticky)
    {
        s |= EWS_STICKY;
    }

    return static_cast<EmeraldWindowState>(s);
}

// ── Pure margin math (D8) ──────────────────────────────────────────────────
// Ported semantics (not code) from emerald + pixdecor:
//
//   CSD (view not decorated)          → {0,0,0,0}
//   fullscreen                        → {0,0,0,0}
//   t   = *_space + extra_extents.X   (per side; *_space = border + shadow)
//   top = top_space + extra_top + titlebar_height
//   maximized && !maximized_borders → {0, 0, 0, top}
//   else                             → {t, t, t, top}
wf::decoration_margins_t emerald_decorator_t::margins_for(const MarginsInput &in)
{
    if (in.csd || in.fullscreen)
    {
        return {0.0, 0.0, 0.0, 0.0};
    }

    // D8: the frame must be sized so the engine can draw its layout. The
    // engines position themselves from window_settings (truglass: the frame
    // content starts at (left_space - win_extents.left, top_space -
    // win_extents.top), the client starts top_space + titlebar_height below
    // the frame top, the shadow is drawn INSIDE the frame via
    // draw_shadow_background()). So the margins must be the *_space values
    // (border + shadow), NOT win_extents alone — with win_extents the
    // frame is too small, the title bar/borders land outside the client
    // area (gap below the window) and the title text is clipped.
    //   left   = left_space   + extra_left
    //   right  = right_space  + extra_right
    //   bottom = bottom_space + extra_bottom
    //   top    = top_space    + extra_top + titlebar_height
    const double t     = static_cast<double>(in.left_space + in.extra_left);
    const double right = static_cast<double>(in.right_space + in.extra_right);
    const double bot   = static_cast<double>(in.bottom_space + in.extra_bottom);
    const double top   = static_cast<double>(in.top_space + in.extra_top) +
                         static_cast<double>(in.titlebar_height);

    const bool maximized = (in.tiled_edges == TILED_EDGES_ALL);
    if (maximized)
    {
        // Finding 19: a maximized window must hug the screen edges — crop
        // the decoration per side. margin = (space + extra) − crop, where
        // crop drops the border part (maximized_borders off) and/or the
        // shadow spread (maximized_shadows off). Both on (default) → zero
        // crop → the full *_space rule above. Both off → {0,0,0,titlebar}
        // (titlebar strip only, matching emerald's use_decoration_cropping).
        // The render path applies the SAME crop (maximized_crop) as a cairo
        // translate, so the engine's full-extent coordinates stay valid.
        // Review F2 (-Wshadow): `left`, not `t` — the outer non-maximized
        // margin above is also named `t`.
        const double left = (in.left_space + in.extra_left) -
                           maximized_crop_side(in.left_space, in.win_ext_left,
                                               in.extra_left,
                                               in.maximized_borders,
                                               in.maximized_shadows);
        const double r    = (in.right_space + in.extra_right) -
                           maximized_crop_side(in.right_space, in.win_ext_right,
                                               in.extra_right,
                                               in.maximized_borders,
                                               in.maximized_shadows);
        const double b    = (in.bottom_space + in.extra_bottom) -
                           maximized_crop_side(in.bottom_space, in.win_ext_bottom,
                                               in.extra_bottom,
                                               in.maximized_borders,
                                               in.maximized_shadows);
        const double tp   = (in.top_space + in.extra_top) -
                           maximized_crop_side(in.top_space, in.win_ext_top,
                                               in.extra_top,
                                               in.maximized_borders,
                                               in.maximized_shadows) +
                           static_cast<double>(in.titlebar_height);
        return {left, r, b, tp};
    }

    // The bottom margin is STEADY through the whole shade animation.
    //
    // The old design (spec 05 §3) collapsed it with progress —
    // bottom = bot + (client_h − bot − titlebar) × p — so the logical
    // client shrank as the window rolled up. That chase is harmful for SSD
    // views: the emerald engine draws the frame texture from the LOGICAL
    // client size (ctx.client_height), so the border/shadow position inside
    // the texture follows the logical bottom margin. Apps re-commit their
    // own size mid-animation (they refuse to shrink below their minimum),
    // so the margin — and with it the texture layout — oscillated (e.g.
    // 18 → 37 → 0 logical). The shade transformer keeps a fixed bottom
    // strip sampled at [H − bottom_margin, H); with an oscillating layout
    // that strip lands on CLIENT ROWS and the shaded window shows a band of
    // window content under the titlebar (user report). With the margin
    // pinned to the theme value the texture layout is stable: the engine
    // always draws the client ending at H − bot, the bottom border +
    // shadow in [H − bot, H) — exactly the band the transformer keeps.
    //
    // The visual roll-up is done entirely by the shade transformer (which
    // folds the middle band to zero at p = 1); the logical client is never
    // resized during the animation, so apps are not disturbed, the frame
    // size is invariant, and there is no per-cycle drift (the finding 53/54
    // cap/observation machinery remains for the CSD shadow export, which
    // does progress its margins in shadow_margins_for).
    return {t, right, bot, top};
}

// ── Constructor / destructor ───────────────────────────────────────────────
// Review F2 (-Wshadow): parameters named `view_arg`/`theme_arg` — bare
// `view`/`theme` shadow the members of the same name.
emerald_decorator_t::emerald_decorator_t(::wayfire_toplevel_view view_arg,
                                         std::shared_ptr<const ThemeSettings> theme_arg)
    : view(view_arg)
    // The BASE shared_from_this() (view_interface_t is the
    // enable_shared_from_this base): the toplevel's own shared_from_this()
    // would hide it and drag in a dynamic_pointer_cast whose typeinfo
    // lives in libwayfire. Base liveness == toplevel liveness (same object).
    , weak_view(view_arg ? view_arg->view_interface_t::shared_from_this()
                         : std::weak_ptr<wf::view_interface_t>())
    , theme(std::move(theme_arg))
{
    ctx().active = view_arg ? view_arg->activated : false;
    if (view_arg)
    {
        ctx().state = emerald_state_from_view(view_arg->toplevel()->pending(),
                                              view_arg->sticky);
    }

    // For SSD windows the title bar height is the theme's (the engine draws
    // the title bar at ws->titlebar_height). CSD windows get the height from
    // the csd_titlebar_height option via set_titlebar_height().
    // NB: read the MEMBER `theme`, not the parameter `theme_arg` — the
    // initializer list already did `theme(std::move(theme_arg))`, so
    // `theme_arg` is a moved-from (null) shared_ptr here and `theme_arg &&`
    // would always be false (the titlebar height would never be seeded).
    if (!is_csd() && theme && theme->valid() && theme->ws() != nullptr)
    {
        titlebar_height = theme->ws()->titlebar_height;
    }
}

emerald_decorator_t::~emerald_decorator_t()
{
    detach_node();
}

// ── Accessors ──────────────────────────────────────────────────────────────
int emerald_decorator_t::get_titlebar_height() const
{
    return titlebar_height;
}

void emerald_decorator_t::set_titlebar_height(int height)
{
    titlebar_height = height;
}

void emerald_decorator_t::set_maximized_borders(bool on)
{
    maximized_borders_ = on;
}

void emerald_decorator_t::set_maximized_shadows(bool on)
{
    maximized_shadows_ = on;
}

bool emerald_decorator_t::maximized_borders() const
{
    // Finding 45: emerald's `use_decoration_cropping` — when set, a
    // maximized window keeps only the titlebar strip (upstream main.c:660-
    // 666: all maximized extents cropped). It overrides the WCM
    // maximized_borders/maximized_shadows options (which own the crop
    // while it is off, per the single-owner rule).
    // Review F2 (-Wshadow): `th` — `theme` is a member of this class.
    if (auto th = theme_ref())
        if (auto ws = th->ws())
            if (ws->use_decoration_cropping)
                return false;
    return maximized_borders_;
}

bool emerald_decorator_t::maximized_shadows() const
{
    // Review F2 (-Wshadow): `th` — `theme` is a member of this class.
    if (auto th = theme_ref())
        if (auto ws = th->ws())
            if (ws->use_decoration_cropping)
                return false;
    return maximized_shadows_;
}

bool emerald_decorator_t::is_csd() const
{
    return view != nullptr && !view->should_be_decorated();
}

DecorContext &emerald_decorator_t::ctx()
{
    return this->decor_ctx;
}

// ── Part 04 helpers used by the scene node ──────────────────────────────────
bool emerald_decorator_t::detach_should_remove_child(
    wf::scene::node_t *node_parent, wf::scene::floating_inner_node_t *sroot)
{
    // part 06 finding 16: pure pointer compare — no dereference, so it is
    // safe to call during view teardown (the sroot shared_ptr on the
    // caller's side is what keeps the right-hand side alive).
    return sroot != nullptr && node_parent == sroot;
}

::wayfire_toplevel_view emerald_decorator_t::view_for_offsets() const
{
    // part 06 finding 16: refuse to hand out a dangling view. The observer
    // `view` is non-owning; once the toplevel dies it points at freed
    // memory and any `view->toplevel()->pending()` from the emerald node's
    // render/hit-test segfaults. weak_view.lock() is the liveness check.
    auto live = weak_view.lock();
    if (live == nullptr)
    {
        return nullptr;
    }
    return this->view;
}

emerald_node_t *emerald_decorator_t::node_for_hit_test() const
{
    return node ? node.get() : nullptr;
}

std::shared_ptr<const ThemeSettings> emerald_decorator_t::theme_ref() const
{
    return theme;
}

// Review F2 (-Wshadow): parameter named `new_theme` — `theme` is a member.
void emerald_decorator_t::set_theme(std::shared_ptr<const ThemeSettings> new_theme)
{
    this->theme = std::move(new_theme);
    // Finding 42: re-point ctx.fs at the NEW theme now — pointer events
    // between this swap and the next render read it (see the helper docs:
    // a stale pointer into the destroyed ThemeSettings crashed wayfire on
    // the first hover after a live reload).
    if (this->theme)
        decor_ctx_set_theme(decor_ctx, *this->theme, decor_ctx.active);
}

std::shared_ptr<wf::config::option_t<wf::animation_description_t>>
emerald_decorator_t::shade_duration_opt() const
{
    return shade_duration_opt_;
}

void emerald_decorator_t::set_shade_duration_opt(
    std::shared_ptr<wf::config::option_t<wf::animation_description_t>> opt)
{
    shade_duration_opt_ = std::move(opt);
}

bool emerald_decorator_t::enable_shade() const
{
    return enable_shade_;
}

void emerald_decorator_t::set_enable_shade(bool enabled)
{
    enable_shade_ = enabled;
}

// ── Margins (full, live view) ──────────────────────────────────────────────
// NOTE: shade() / the shade transformer live in part 05 (a separate TU,
// emerald-decorator-shade.cpp). This part-01 TU deliberately does NOT call
// shade() so the headless margins test links without the scene/view-
// transformer symbols.
//
// Finding 52: the UNSHADED client height (used by the CSD shadow export) is
// the frame height minus the current top margin and the theme's unshaded
// bottom margin. The pending geometry is the FRAME (client + margins), so
// its height must never be fed to a formula as if it were the client. The
// top margin is stable during the shade animation (the bottom margin is
// steady too — see margins_for()), so this is exactly the client the window
// had before shading.
int emerald_decorator_t::unshaded_client_h() const
{
    if (view == nullptr)
        return 0;

    const auto &pending = view->toplevel()->pending();
    const int top = static_cast<int>(pending.margins.top);
    int bottom = 6;  // MarginsInput's default bottom_space
    if (theme && theme->valid() && theme->ws() != nullptr)
    {
        const auto *ws = theme->ws();
        bottom = ws->bottom_space + ws->extra_extents.bottom;
    }

    const int client = pending.geometry.height - top - bottom;
    return client > 0 ? client : 0;
}

MarginsInput emerald_decorator_t::make_margins_input(const wf::toplevel_state_t &state) const
{
    MarginsInput in;
    in.fullscreen  = state.fullscreen;
    in.tiled_edges = state.tiled_edges;
    in.csd         = is_csd();

    if (theme && theme->valid())
    {
        // Part 02 contract: geometry lives in window_settings. The frame
        // margins come from the *_space values (border + shadow, the layout
        // the engines draw against), not win_extents (border only) — see
        // margins_for().
        const auto *ws = theme->ws();
        if (ws != nullptr)
        {
            in.left_space   = ws->left_space;
            in.right_space  = ws->right_space;
            in.top_space    = ws->top_space;
            in.bottom_space = ws->bottom_space;
            in.extra_left   = ws->extra_extents.left;
            in.extra_right  = ws->extra_extents.right;
            in.extra_top    = ws->extra_extents.top;
            in.extra_bottom = ws->extra_extents.bottom;
            in.win_ext_left   = ws->win_extents.left;
            in.win_ext_right  = ws->win_extents.right;
            in.win_ext_top    = ws->win_extents.top;
            in.win_ext_bottom = ws->win_extents.bottom;
        }
    }
    // The margin must reserve the SAME titlebar height the engine draws with,
    // or the drawn titlebar overflows the reserved space and overlaps the
    // client (text descenders get clipped — the "cut from the bottom" bug).
    // The engine draws the SSD titlebar at ws->titlebar_height, so read it
    // from the LIVE theme here (not the member cached at construction, which
    // is stale when the theme loads/reloads after the decorator is built).
    // CSD windows use the csd_titlebar_height member (set by the option).
    if (!is_csd() && theme && theme->valid())
    {
        if (const auto *wsl = theme->ws())
            in.titlebar_height = wsl->titlebar_height;
    }
    else
    {
        in.titlebar_height = titlebar_height;
    }
    // Accessors (not the raw members): use_decoration_cropping (finding 45)
    // overrides both to false, and the render path uses the same accessors.
    in.maximized_borders = maximized_borders();
    in.maximized_shadows = maximized_shadows();
    return in;
}

wf::decoration_margins_t emerald_decorator_t::get_margins(const wf::toplevel_state_t &state) const
{
    return margins_for(make_margins_input(state));
}

// Part 04's node attach/detach/updates live in emerald-decorator-node.cpp and
// Part 05's shade hooks in emerald-decorator-shade.cpp, so the headless
// margins test (which compiles only this file) does not link against the
// scene node / view transformer (which need wayfire scene symbols).

} // namespace emeraldwf
} // namespace wf
