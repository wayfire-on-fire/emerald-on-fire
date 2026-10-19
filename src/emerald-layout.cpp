// emerald-layout.cpp — Part 03: pure port of emerald's titlebar layout
// parser (layout_title_objects, main.c:2954), per-object widths
// (get_title_object_width, main.c:2863), button regions
// (update_button_regions, main.c:1653) and the 3x3 cursor/resize band math
// (update_window_extents, main.c:309-397; event-window logic main.c:3033+).
//
// No wayfire dependency: only emerald.h types and plain ints.
#include "emerald-layout.hpp"
#include "emerald-settings.hpp"

#include <cctype>
#include <cstring>
#include <cstdlib>
#include <limits>

namespace wf
{
namespace emeraldwf
{
// Part 02's emerald-settings.cpp ships the real ThemeSettings::ws()
// (it is under development and is not linked into the part-03 test).
// Provide the definition here so test_layout is self-contained; part 02's
// translation unit takes over once it lands.
//
// Weaken it so that when emerald-layout.cpp is linked into the plugin
// alongside emerald-settings.cpp (which defines the strong ws()), the real
// definition wins and there is no "multiple definition" link error. test_layout
// (which links only this file) still resolves to the weak copy.
__attribute__((weak)) window_settings*ThemeSettings::ws() const
{
    return ws_;
}

// emerald_button_actions[] comes from emerald-types.hpp (emerald_button_actions),
// mapped to the EWA_* bitmask that ctx.actions carries.

int EmeraldLayout::title_object_type(char obj)
{
    // port of get_title_object_type (main.c:2825)
    switch (obj)
    {
      case ':':
        return -1; // state separator

      case 'C':
        return TBT_CLOSE;

      case 'N':
        return TBT_MINIMIZE;

      case 'X':
      case 'R':
        return TBT_MAXIMIZE;

      case 'F':
        return TBT_SUPERMAXIMIZE;

      case 'H':
        return TBT_HELP;

      case 'M':
        return TBT_MENU;

      case 'T':
        return TBT_TITLE;

      case 'I':
        return TBT_ICON;

      case 'S':
        return TBT_SHADE;

      case 'U':
      case 'A':
        return TBT_ONTOP;

      case 'D':
        return TBT_ONBOTTOM;

      case 'Y':
        return TBT_STICKY;

      default:
        return -2;
    }
}

int EmeraldLayout::title_object_width(const window_settings *ws,
    const DecorContext & ctx, char obj) const
{
    // port of get_title_object_width (main.c:2863)
    int i = title_object_type(obj);
    switch (i)
    {
      case -1:
        return -1;

      case TBT_TITLE:
        // emerald: pango_layout_get_pixel_size + 2; we take the
        // Pango-measured width from title_text_width_ (set by part 04
        /// tests before build(); separate from ctx.tobj_item_width,
        // main.c:2991).
        return title_text_width_ + 2;

      case TBT_ICON:
        return 18;

      default:
        if ((i >= 0) && (i < B_T_COUNT))
        {
            return (ctx.actions & emerald_button_actions[i]) ?
                   (ws->use_pixmap_buttons ? ws->c_icon_size[i].w : 18) :
                   0;
        } else
        {
            return 0;
        }
    }
}

int EmeraldLayout::real_pos(const window_settings *ws,
    const DecorContext & ctx, int tobj) const
{
    // port of get_real_pos (main.c:291)
    switch (ctx.tobj_item_state[tobj])
    {
      case 1:
        return ((ctx.width + ws->left_space - ws->right_space +
            ctx.tobj_size[0] - ctx.tobj_size[1] - ctx.tobj_size[2]) / 2 +
            ctx.tobj_item_pos[tobj]);

      case 2:
        return (ctx.width - ws->right_space - ctx.tobj_size[2] +
            ctx.tobj_item_pos[tobj]);

      case 3:
        return -1;

      default:
        return (ws->left_space + ctx.tobj_item_pos[tobj]);
    }
}

void EmeraldLayout::place_objects(const window_settings *ws,
    DecorContext & ctx) const
{
    // port of layout_title_objects (main.c:2954) — width accumulation pass
    // and object positioning pass. The X11 bits of position_title_object
    // (XMoveResizeWindow / cursors) are dropped; the state/x bookkeeping
    // is kept verbatim.
    const char *layout = ws->tobj_layout ? ws->tobj_layout : "";

    // ── pass 1: zone widths ─────────────────────────────────────────────
    ctx.tobj_size[0] = 0;
    ctx.tobj_size[1] = 0;
    ctx.tobj_size[2] = 0;
    int zone_width[3] = {0}; // (main.c:2991) per-zone last-object width
    int state = 0;
    for (size_t i = 0; i < strlen(layout); i++)
    {
        if (layout[i] == '(')
        {
            i++;
            ctx.tobj_size[state] += strtol(&layout[i], nullptr, 0);
            while (layout[i] && isdigit((unsigned char)layout[i]))
            {
                i++;
            }

            continue;
        }

        int owidth = title_object_width(ws, ctx, layout[i]);
        if (owidth == -1)
        {
            state++;
            if (state > 2)
            {
                break;
            }
        } else
        {
            ctx.tobj_size[state] += owidth;
            zone_width[state]     = owidth;  // (main.c:2991)
        }
    }

    // ── zone positions ──────────────────────────────────────────────────
    // (main.c:2994-2998) — note: the original emerald code uses
    // wnck client window height here; emeraldwf keeps a plain client size.
    ctx.tobj_pos[0] = ws->left_space + ws->extra_extents.left;
    ctx.tobj_pos[2] = ctx.client_width - ctx.tobj_size[2] + ctx.tobj_pos[0];
    ctx.tobj_pos[1] = std::max((ctx.tobj_pos[2] + ctx.tobj_size[0] -
        ctx.tobj_size[1]) / 2,
        0) + ctx.tobj_pos[0];

    // ── pass 2: object placement ────────────────────────────────────────
    // (main.c:3000-3031) — note: emerald's original code uses wnck client
    // window height as the `x` cursor here; emeraldwf keeps plain client
    // size, which is the same value for the windowed case.
    state = 0;
    int x = ctx.tobj_pos[0] + ws->button_hoffset;
    for (int i = 0; i < TBT_COUNT; i++)
    {
        ctx.tobj_item_state[i] = 3;
    }

    bool removed = false;
    for (size_t i = 0; i < strlen(layout); i++)
    {
        if (layout[i] == '(')
        {
            i++;
            x += strtol(&layout[i], nullptr, 0);
            while (layout[i] && isdigit((unsigned char)layout[i]))
            {
                i++;
            }

            continue;
        }

        if (title_object_type(layout[i]) == -1)
        {
            state++;
            if (state > 2)
            {
                break;
            } else
            {
                x = ctx.tobj_pos[state];
            }
        } else
        {
            if ((state == 2) && !removed)
            {
                x -= ws->button_hoffset;
                removed = true;
            }

            int w = title_object_width(ws, ctx, layout[i]);
            // (main.c:2991) per-zone last-object width — use the value
            // computed in pass 1, which is authoritative.
            ctx.tobj_item_width[state] = zone_width[state];
            ctx.tobj_item_pos[title_object_type(layout[i])] =
                x - ctx.tobj_pos[state];
            ctx.tobj_item_state[title_object_type(layout[i])] = state;
            x += w;
        }
    }
}

void EmeraldLayout::update_button_regions(const window_settings *ws,
    DecorContext & ctx) const
{
    // port of update_button_regions (main.c:1653). bg_surface / fade parts
    // are part 02/04 concerns and are left at their defaults.
    int y1 = ws->top_space - ws->win_extents.top;

    auto not_visible = [&] (int b_t) -> bool
    {
        return ctx.tobj_item_state[b_t] == 3 ||
               !(ctx.actions & emerald_button_actions[b_t]);
    };

    // reset all regions (emerald reset_buttons_bg_and_fade uses -100). The
    // inactive set is reset here too (not only on the visible-button copy
    // below): a hidden button's inact region must stay -100 like its active
    // one, not a stale/fresh value.
    for (int b_t = 0; b_t < B_T_COUNT; ++b_t)
    {
        for (button_region_t *r : {&ctx.button_region[b_t],
             &ctx.button_region_inact[b_t]})
        {
            r->base_x1 = r->base_y1 = r->base_x2 = r->base_y2 = -100;
            r->glow_x1 = r->glow_y1 = r->glow_x2 = r->glow_y2 = -100;
            for (int b_t2 = 0; b_t2 < B_T_COUNT; ++b_t2)
            {
                r->overlap_buttons[b_t2] = false;
            }

            r->bg_surface = nullptr;
        }
    }

    for (int b_t = 0; b_t < B_T_COUNT; ++b_t)
    {
        button_region_t *button_region = &ctx.button_region[b_t];
        double x = 0.0, y = 0.0;

        if (not_visible(b_t))
        {
            continue;
        }

        // port of get_button_pos (main.c:1093)
        y = (double)y1 + ws->button_offset;
        x = (double)real_pos(ws, ctx, b_t);

        button_region->base_x1 = (gint)x;
        button_region->base_y1 = (gint)y;

        if (ws->use_pixmap_buttons)
        {
            button_region->base_x2 = (gint)(x + ws->c_icon_size[b_t].w);
            button_region->base_y2 =
                std::min((gint)(y + ws->c_icon_size[b_t].h),
                    (gint)(ws->top_space + ws->titlebar_height));

            if ((ctx.active && ws->use_button_glow) ||
                (!ctx.active && ws->use_button_inactive_glow))
            {
                int glow_x =
                    (gint)(x - (ws->c_glow_size.w - ws->c_icon_size[b_t].w) / 2);
                int glow_y =
                    (gint)(y - (ws->c_glow_size.h - ws->c_icon_size[b_t].h) / 2);
                button_region->glow_x1 = glow_x;
                button_region->glow_y1 = glow_y;
                button_region->glow_x2 = glow_x + ws->c_glow_size.w;
                button_region->glow_y2 =
                    std::min(glow_y + ws->c_glow_size.h,
                        (gint)(ws->top_space + ws->titlebar_height));
            } else
            {
                button_region->glow_x1 = button_region->base_x1;
                button_region->glow_y1 = button_region->base_y1;
                button_region->glow_x2 = button_region->base_x2;
                button_region->glow_y2 = button_region->base_y2;
            }
        } else
        {
            button_region->base_x2 = (gint)(x + 16);
            button_region->base_y2 = (gint)(y + 16);
        }
    }

    // glow overlap flags (port of the second loop, main.c:1716-1748)
    for (int b_t = 0; b_t < B_T_COUNT; ++b_t)
    {
        button_region_t *button_region = &ctx.button_region[b_t];
        if (not_visible(b_t))
        {
            continue;
        }

        if (ws->use_pixmap_buttons &&
            ((ctx.active && ws->use_button_glow) ||
             (!ctx.active && ws->use_button_inactive_glow)))
        {
            for (int b_t2 = 0; b_t2 < B_T_COUNT; ++b_t2)
            {
                if (not_visible(b_t2))
                {
                    continue;
                }

                if (((button_region->base_x1 > ctx.button_region[b_t2].base_x1) &&
                     (button_region->glow_x1 <= ctx.button_region[b_t2].base_x2)) ||
                    ((button_region->base_x1 < ctx.button_region[b_t2].base_x1) &&
                     (button_region->glow_x2 >= ctx.button_region[b_t2].base_x1)))
                {
                    button_region->overlap_buttons[b_t2] = true;
                } else
                {
                    button_region->overlap_buttons[b_t2] = false;
                }
            }
        }
    }

    // inactive set: copy of the active set (port of the third loop;
    // bg_surface belongs to the renderer, so it is excluded here)
    for (int b_t = 0; b_t < B_T_COUNT; ++b_t)
    {
        if (not_visible(b_t))
        {
            continue;
        }

        ctx.button_region_inact[b_t] = ctx.button_region[b_t];
        ctx.button_region_inact[b_t].bg_surface = nullptr;
    }
}

void EmeraldLayout::build(const window_settings *ws, DecorContext & ctx)
{
    if (!ws)
    {
        return;
    }

    place_objects(ws, ctx);
    update_button_regions(ws, ctx);
}

void EmeraldLayout::build(const ThemeSettings & ts, DecorContext & ctx)
{
    build(ts.ws(), ctx);
}

int EmeraldLayout::hit_button(const DecorContext & ctx, int x, int y,
    bool active) const
{
    // port of the hit-test used by emerald's button event windows and
    // draw pass (main.c:1861-1881 / 1629-1650): the *glow* rect answers
    // the pointer when the glow is drawn for this state, else the base
    // rect. Buttons of state 3 (not visible) never hit.
    //
    // Two passes (finding 50): the button cores tile without overlap, so
    // a point inside a core belongs to that button outright. Only points
    // outside every core fall through to the glow rects — which DO
    // overlap (the glow is wider than the button, e.g. SlateHorn: 19px
    // buttons, 38px glow), where the nearest button core wins. A single
    // first-match pass in btype order let a neighbor steal the other
    // button's core (B_T_MINIMIZE < B_T_SHADE: hovering the right half
    // of the shade button lit the minimize button).
    const button_region_t *regions =
        active ? ctx.button_region : ctx.button_region_inact;
    const window_settings *ws = ctx.fs ? ctx.fs->ws : nullptr;
    if (!ws)
    {
        return -1;
    }

    int glow_winner = -1;
    int glow_winner_dist2 = std::numeric_limits<int>::max();

    for (int b_t = 0; b_t < B_T_COUNT; b_t++)
    {
        if ((ctx.tobj_item_state[b_t] == 3) ||
            !(ctx.actions & emerald_button_actions[b_t]))
        {
            continue;
        }

        const button_region_t *br = &regions[b_t];
        int bw = br->base_x2 - br->base_x1;
        int bh = br->base_y2 - br->base_y1;

        // pass 1: the button core (no overlap between cores)
        if ((br->base_x1 != -100) &&
            (x >= br->base_x1) && (x < br->base_x1 + bw) &&
            (y >= br->base_y1) && (y < br->base_y1 + bh))
        {
            return b_t;
        }

        // pass 2 candidate: the glow rect answers the pointer when the
        // glow is drawn for this state (mirror of main.c:1861-1881)
        if (ws->use_pixmap_buttons &&
            ((ws->use_button_glow && active) ||
             (ws->use_button_inactive_glow && !active)) &&
            (br->glow_x1 != -100))
        {
            int gw = br->glow_x2 - br->glow_x1;
            int gh = br->glow_y2 - br->glow_y1;
            if ((x >= br->glow_x1) && (x < br->glow_x1 + gw) &&
                (y >= br->glow_y1) && (y < br->glow_y1 + gh))
            {
                // distance to this button's core center (x2 = 2*dx to
                // avoid the division)
                int cdx = 2 * x - (br->base_x1 + br->base_x2);
                int cdy = 2 * y - (br->base_y1 + br->base_y2);
                int d2  = cdx * cdx + cdy * cdy;
                if (d2 < glow_winner_dist2)
                {
                    glow_winner_dist2 = d2;
                    glow_winner = b_t;
                }
            }
        }
    }

    return glow_winner;
}

uint32_t EmeraldLayout::resize_edges(const DecorContext & ctx, int x,
    int y) const
{
    // port of the event-window band math (update_event_windows,
    // main.c:3033-3107). A point resizes the window on the bands
    // surrounding the client; the titlebar row is move, not resize
    // (t_ext_nott boundary, main.c:324).
    //
    // NOTE (finding 13): the band table is in ORIGINAL emerald FRAME
    // coords (frame window origin; the client is inset by the BORDER
    // thickness l_ext/t_ext_nott — the event windows are children of the
    // frame window, main.c:3451). Hit-test coordinates here are PORT frame
    // coords, where the shadow sits between the frame edge and the border.
    // Convert by subtracting the SHADOW SPREAD only (*_space - border):
    // shadow_left = left_space - (win_extents.left + extra_left)
    // Subtracting the full *_space (the old code) shifted the bands into
    // the client / the shadow — the left border band landed inside the
    // client (invisible to the pointer: the client surface node wins the
    // hit test there), which is why the left edge never showed a resize
    // cursor while the bottom (still outside the client) did.
    const window_settings *ws = ctx.fs ? ctx.fs->ws : nullptr;
    if (!ws)
    {
        return 0;
    }

    int w = ctx.client_width;
    int h = ctx.client_height;
    if (ctx.state & EWS_SHADED)
    {
        h = 0;
    }

    const int shadow_left =
        ws->left_space - (ws->win_extents.left + ws->extra_extents.left);
    const int shadow_top =
        ws->top_space - (ws->win_extents.top + ws->extra_extents.top);

    const int cx = x - shadow_left; // original-frame coords
    const int cy = y - shadow_top;

    uint32_t edges = 0;

    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            const pos_t p = band_table(ws, i, j);
            // region 5 (i==1, j==1) is the titlebar: move, not resize
            if ((i == 1) && (j == 1))
            {
                continue;
            }

            // Finding 31: emerald main.c update_event_windows — shaded:
            // height = 0; k = l = 1, i.e. ONLY the middle row's side bands
            // (left/right edges at the strip) are live. The original port
            // skipped only the top row (i < 1), leaving the bottom row's
            // bands alive on the ghost (unshaded) client height.
            if (ctx.state & EWS_SHADED)
            {
                if (i != 1)
                {
                    continue;
                }
            }

            int bx = p.x + p.xw * w;
            int by = p.y + p.yh * h;
            int bw = p.w + p.ww * w;
            int bh = p.h + p.hh * h;

            // maximized: horizontal bands on the left/right edges
            // collapse to zero width (main.c:3086-3096)
            if (ctx.state & EWS_MAXIMIZED_HORIZONTALLY)
            {
                if ((j == 0) || (j == 2))
                {
                    bw = 0;
                }
            }

            if (ctx.state & EWS_MAXIMIZED_VERTICALLY)
            {
                if ((i == 0) || (i == 2))
                {
                    bh = 0;
                }
            }

            if ((bw == 0) || (bh == 0))
            {
                continue;
            }

            if ((cx < bx) || (cx >= bx + bw) || (cy < by) || (cy >= by + bh))
            {
                continue;
            }

            if (i == 0)
            {
                edges |= EMERALDWF_EDGE_TOP;
            }

            if (i == 2)
            {
                edges |= EMERALDWF_EDGE_BOTTOM;
            }

            if (j == 0)
            {
                edges |= EMERALDWF_EDGE_LEFT;
            }

            if (j == 2)
            {
                edges |= EMERALDWF_EDGE_RIGHT;
            }
        }
    }

    return edges;
}

// Review F2 (-Wall): `x` is unused — the titlebar band spans the full
// frame width (only the y range matters); unnamed in the definition,
// named in the header.
bool EmeraldLayout::in_titlebar(const DecorContext & ctx, int /*x*/, int y) const
{
    // The titlebar band: from the top extent down to its full height
    // (t_ext_nott .. t_ext_nott + titlebar_height), spanning the frame
    // width. (emerald's region 5, main.c:387.)
    const window_settings *ws = ctx.fs ? ctx.fs->ws : nullptr;
    if (!ws)
    {
        return false;
    }

    int t_ext_nott = ws->top_space + ws->extra_extents.top;
    int t_ext = t_ext_nott + ws->titlebar_height;

    return y >= t_ext_nott && y < t_ext;
}

int EmeraldLayout::cursor_region(const DecorContext & ctx, int x, int y) const
{
    // 3x3 grid index 0..8 (port of the ws->pos[i][j] lookup used by
    // update_event_windows, main.c:3033+). Part 04 maps the index to a
    // cursor shape; the middle cell is move.
    //
    // NOTE (finding 13): the band table is in ORIGINAL emerald FRAME coords
    // (see band_table). Hit-test coordinates are PORT frame coords, so
    // subtract the SHADOW SPREAD only (not the full *_space):
    // client_x = frame_x - shadow_left, client_y = frame_y - shadow_top.
    const window_settings *ws = ctx.fs ? ctx.fs->ws : nullptr;
    if (!ws)
    {
        return 4;
    }

    int w = ctx.client_width;
    int h = ctx.client_height;
    if (ctx.state & EWS_SHADED)
    {
        h = 0;
    }

    const int shadow_left =
        ws->left_space - (ws->win_extents.left + ws->extra_extents.left);
    const int shadow_top =
        ws->top_space - (ws->win_extents.top + ws->extra_extents.top);

    const int cx = x - shadow_left;
    const int cy = y - shadow_top;

    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            const pos_t p = band_table(ws, i, j);
            int bx = p.x + p.xw * w;
            int by = p.y + p.yh * h;
            int bw = p.w + p.ww * w;
            int bh = p.h + p.hh * h;
            if ((bw == 0) || (bh == 0))
            {
                continue;
            }

            if ((cx >= bx) && (cx < bx + bw) && (cy >= by) && (cy < by + bh))
            {
                return i * 3 + j;
            }
        }
    }

    // no band hit (e.g. the client area): clamp into the 3x3 grid so the
    // index stays 0..8 — part 04 maps the middle cell to the move cursor.
    // (cx/cy are table coords: the client spans x[l_ext, l_ext + w).)
    const int l_ext = ws->win_extents.left + ws->extra_extents.left;
    const int t_ext_nott = ws->win_extents.top + ws->extra_extents.top;
    int t_ext = t_ext_nott + ws->titlebar_height;

    int row = (cy < 0) ? 0 : (cy >= t_ext ? 2 : 1);
    int col = (cx < 0) ? 0 : (cx >= l_ext + w ? 2 : 1);
    return row * 3 + col;
}

int EmeraldLayout::double_click_action(const DecorContext & ctx) const
{
    const window_settings *ws = ctx.fs ? ctx.fs->ws : nullptr;
    return ws ? ws->double_click_action : 0;
}

int EmeraldLayout::middle_click_action(const DecorContext & ctx) const
{
    const window_settings *ws = ctx.fs ? ctx.fs->ws : nullptr;
    return ws ? ws->middle_click_action : 0;
}
} // namespace emeraldwf
} // namespace wf
