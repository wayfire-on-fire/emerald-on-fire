/*
 * emerald-types.hpp — shared frozen contract (overview §5.1, owned by part 02).
 *
 * Bit values of EmeraldWindowState / EmeraldWindowActions are identical to the libwnck values pinned in
 * overview D4, so the bridge can map into the original emerald WnckWindowState / WnckWindowActions fields.
 *
 * window_settings, frame_settings, button_region_t, rectangle_t, decor_extents_t, B_T_COUNT, TBT_COUNT etc.
 * come from the original emerald.h; the include paths are arranged so all parts see them.
 */
#ifndef EMERALDWF_TYPES_HPP
#define EMERALDWF_TYPES_HPP

#include <algorithm>
#include <cstdint>
#include <string>

#include <emerald.h> /* original emerald structs (window_settings, ...) */

namespace wf
{
// If the real wayfire toplevel-view header is already included (it defines
// `wayfire_toplevel_view` as nonstd::observer_ptr<toplevel_view_interface_t>),
// do not redeclare the placeholder. The placeholder exists so this frozen
// header can be used in TUs that do NOT include wayfire view headers (e.g.
// the headless layout test). Part 04's node is the first TU that needs both;
// it sets EMERALDWF_HAVE_WAYFIRE_VIEW before including any emerald header.
#ifndef EMERALDWF_HAVE_WAYFIRE_VIEW
using wayfire_toplevel_view = class wayfire_toplevel_view_t;
#endif
}

namespace wf::emeraldwf
{
// Bit values identical to libwnck (see D4) so the bridge can static_cast
// into original emerald WnckWindowState/WnckWindowActions fields.
enum EmeraldWindowState : uint32_t
{
    EWS_MAXIMIZED_HORIZONTALLY = 1 << 0,
    EWS_MAXIMIZED_VERTICALLY   = 1 << 1,
    EWS_SHADED                 = 1 << 2,
    EWS_ABOVE                  = 1 << 3,
    EWS_STICKY                 = 1 << 4,
    EWS_FULLSCREEN             = 1 << 5,
};

enum EmeraldWindowActions : uint32_t
{
    EWA_CLOSE      = 1 << 0,
    EWA_MAXIMIZE   = 1 << 1,
    EWA_MINIMIZE   = 1 << 2,
    EWA_SHADE      = 1 << 3,
    EWA_ABOVE      = 1 << 4,
    EWA_STICK      = 1 << 5,
    EWA_FULLSCREEN = 1 << 6,
    EWA_MENU       = 1 << 7,
};

// Port of emerald titlebar.h's NEED_BUTTON_ACTIONS table, mapped to the
// plugin's EWA_* bitmask (NOT libwnck WnckWindowAction values) so the
// `actions & emerald_button_actions[b_t]` visibility test matches what the
// node puts in DecorContext::actions. Used by both the layout (button
// regions) and the engine (button draw) — they must agree or a button that
// the layout lays out will be skipped by the draw pass.
// Buttons whose action the port does NOT implement are hidden by giving
// them a real EWA_* bit that the node never puts in DecorContext::actions:
// the standard `actions & emerald_button_actions[b_t]` visibility test
// (button_not_visible in emerald-engine.cpp + hit_button in emerald-layout.cpp
// + the layout's width/region passes) then fails, so the button takes zero
// width, is neither drawn nor hit-tested — the theme (layout string and PNGs)
// is untouched.
//
// B_T_ABOVE (Always-on-Top, layout chars 'A'/'U'): emerald implements a
// persistent always-on-top stacking request; emeraldwf does NOT — wayfire
// 0.11 exposes no always-on-top request on a toplevel (only
// view_bring_to_front, a one-shot raise), and handle_button()
// (emerald-node.cpp) has no B_T_ABOVE case, so the button would be a visual
// no-op. Hidden via EWA_ABOVE (never set by the node).
//
// B_T_MENU (layout char 'M'): emerald opens a window menu (the full list of
// window actions) on click; the port has no menu, so the button would be a
// visual no-op. Hidden via EWA_MENU (never set by the node).
//
// RE-ENABLING a button once its action is implemented: add the case to
// handle_button() and have the node set the button's EWA bit in
// DecorContext::actions. The button becomes visible and functional again
// with no other change (themes already carry the button; no theme edit).
static constexpr uint32_t emerald_button_actions[B_T_COUNT] = {
    EWA_CLOSE, // B_T_CLOSE
    EWA_MAXIMIZE, // B_T_MAXIMIZE
    EWA_MINIMIZE, // B_T_MINIMIZE
    1 << 20, // B_T_HELP (emerald FAKE_WINDOW_ACTION_HELP)
        EWA_MENU, // B_T_MENU: unimplemented (see note above) —
        // node never sets EWA_MENU, so it is hidden
        EWA_SHADE, // B_T_SHADE
        EWA_ABOVE, // B_T_ABOVE: unsupported (see note above) —
        // node never sets EWA_ABOVE, so it is hidden
        EWA_STICK, // B_T_STICKY
        EWA_FULLSCREEN, // B_T_SUPERMAXIMIZE
};

// Maps a wayfire toplevel view to emerald state (Part 01 implements).
// contract: fullscreen => EWS_FULLSCREEN, tiled_edges all => EWS_MAXIMIZED_*,
// stick/above/sticky from view flags.
EmeraldWindowState emerald_state_from_view(wayfire_toplevel_view view);

// Mirrors emerald's `struct _decor` (include/emerald.h:298-338) minus the
// X11/GTK-owned fields (event_windows, button_windows, surfaces, layout,
// name, icon*, force_quit_dialog, draw, prop_xid, button_fade_info).
// Field names/types are verbatim from emerald.h so ported code compiles.
struct DecorContext
{
    int client_width = 0, client_height = 0;
    int width = 0, height = 0; // full frame size (D7), LOGICAL units
    // Output scale factor (render_target_t.scale). The frame surface is
    // rendered at physical resolution (width*scale x height*scale) so the
    // decoration composites crisply on scaled outputs — the same contract
    // wayfire's native deco plugin follows (deco-subsurface.cpp scales its
    // title surface by data.target.scale). All engine drawing coordinates
    // stay logical; the engine applies cairo_scale(scale, scale) so the
    // physical surface is filled 1:1 by wayfire's logical->framebuffer dst box.
    // Default 1.0 keeps headless tests / harnesses byte-identical.
    double scale = 1.0;
    std::string title; // window title (drawn in the title bar)
    bool decorated = true;
    bool active    = false;
    // Finding 19: plugin options — when the window is maximized, the margins
    // (and the render crop) drop the border / shadow parts per side.
    // Default true keeps the full-extent behavior.
    bool maximized_borders = true;
    bool maximized_shadows = true;
    uint32_t state = 0; // EmeraldWindowState
    // The port's supported-action set: exactly the actions handle_button()
    // (emerald-node.cpp) implements — close/maximize/minimize plus
    // shade/sticky/fullscreen. The node never assigns ctx.actions, so this
    // default IS the production set: themes carrying 'S', 'Y', 'F' show
    // working buttons. EWA_ABOVE, EWA_MENU (and help's fake bit) are
    // deliberately absent — those buttons are hidden by the
    // emerald_button_actions mask; re-enabling a button is a one-bit fill-in
    // here plus its handle_button() case.
    uint32_t actions = EWA_CLOSE | EWA_MAXIMIZE | EWA_MINIMIZE |
        EWA_SHADE | EWA_STICK | EWA_FULLSCREEN;
    frame_settings *fs   = nullptr;         // active or inactive (set by bridge)
    int button_states[9] = {0};
    int tobj_pos[3]  = {0, 0, 0};
    int tobj_size[3] = {0, 0, 0};
    int tobj_item_pos[12]   = {0};
    int tobj_item_state[12] = {3}; // 3 = not present
    int tobj_item_width[12] = {0};
    button_region_t button_region[9] = {};
    button_region_t button_region_inact[9] = {};
    // Per-button fade crossfade alphas (finding 43; see
    // emerald-button-fade.hpp): the non-hovered surface (default 1.0) and
    // the hovered surface (default 0.0 = idle, no fade). The node
    // publishes them from its fade state; the engine blends the two
    // button surface columns.
    double button_fade_normal[9] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    double button_fade_hover[9]  = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    rectangle_t min_drawn_buttons_region = {0, 0, 0, 0};
    bool draw_only_buttons_region  = false;
    int button_last_drawn_state[9] = {0};
    bool only_change_active = false;
};

// Frame-size math (D7).
inline int frame_width(const window_settings & ws, int client_w)
{
    return ws.left_space + std::max(client_w, 1) + ws.right_space;
}

// Finding 19: maximized-window crop. A maximized window must hug the screen
// edges; each side's full margin is
// (shadow spread) + (border part),   shadow spread = space − ext − extra
// (see layout.cpp:393). The two parts are cropped by DIFFERENT options and
// on DIFFERENT edges (finding 19c):
// shadows off  → the shadow part is removed from the OUTER edge (the
// surface/render origin shifts inward);
// borders off  → the border part is removed from the INNER edge (the
// surface gets smaller).
// Both consumers (margins_for and the render translate) must derive these
// from the same decomposition.
struct decor_crop_parts_t
{
    int shadow = 0; // shadow spread
    int border = 0; // border part (margin − shadow)
};

inline decor_crop_parts_t maximized_crop_parts(int space, int win_ext,
    int extra)
{
    int shadow = space - win_ext - extra;
    if (shadow < 0)
    {
        shadow = 0;
    }

    const int margin = space + extra;
    return {shadow, margin - shadow};
}

struct decor_crop_t
{
    int left = 0, right = 0, top = 0, bottom = 0;
    bool any() const
    {
        return left || right || top || bottom;
    }
};

// Total crop per side for a flag combination (shadow part iff shadows off,
// border part iff borders off) — used by the margin math.
inline int maximized_crop_side(int space, int win_ext, int extra,
    bool borders, bool shadows)
{
    const auto parts = maximized_crop_parts(space, win_ext, extra);
    int c = 0;
    if (!shadows)
    {
        c += parts.shadow;
    }

    if (!borders)
    {
        c += parts.border;
    }

    return c;
}

inline decor_crop_t maximized_crop(const window_settings & ws,
    bool maximized_borders,
    bool maximized_shadows)
{
    decor_crop_t c;
    c.left = maximized_crop_side(ws.left_space, ws.win_extents.left,
        ws.extra_extents.left,
        maximized_borders, maximized_shadows);
    c.right = maximized_crop_side(ws.right_space, ws.win_extents.right,
        ws.extra_extents.right,
        maximized_borders, maximized_shadows);
    c.top = maximized_crop_side(ws.top_space, ws.win_extents.top,
        ws.extra_extents.top,
        maximized_borders, maximized_shadows);
    c.bottom = maximized_crop_side(ws.bottom_space, ws.win_extents.bottom,
        ws.extra_extents.bottom,
        maximized_borders, maximized_shadows);
    return c;
}

// Review F2 (-Wall -Wextra): `client_w` is part of the frozen D7 contract
// (specs/00-overview.md, callers pass it) but the height formula does not
// use it — unname it to keep the contract stable and the build clean.
inline int frame_height(const window_settings & ws, int /*client_w*/, int client_h)
{
    int h = ws.top_space + ws.bottom_space + ws.titlebar_height;
    if (ws.stretch_sides)
    {
        h += ws.normal_top_corner_space + ws.bottom_corner_space + 2;
    } else
    {
        h += std::max(client_h, 1);
    }

    return h;
}
}

#endif
