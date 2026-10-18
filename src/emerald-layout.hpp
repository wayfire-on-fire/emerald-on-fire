// emerald-layout.hpp — Part 03 (per overview §5.5, frozen).
//
// Pure port of emerald's titlebar layout parser + button/resize hit
// testing. No wayfire dependency: everything is plain emerald structs
// (window_settings, DecorContext) and plain uint32_t edge masks.
//
// Note: the frozen interface in overview §5.5 is `build(const
// ThemeSettings&, DecorContext&)`; the `ThemeSettings` shape there is
// Part 02's (ws()/fs_active()/fs_inactive() accessors). This class is
// implemented against `window_settings*` directly (part 02's
// ThemeSettings exposes it via ws()) so it builds before part 02 lands;
// the ThemeSettings overload below is the frozen convenience entry.
#pragma once

#include <cstdint>
#include <algorithm>
#include <emerald.h>
#include "emerald-types.hpp"

namespace wf
{
namespace emeraldwf
{

class ThemeSettings; // Part 02

// WLR_EDGE_* values (wayland protocol), defined plain so this part stays
// free of wayfire headers (spec 03 acceptance). Part 04 casts to wlr_edges.
#define EMERALDWF_EDGE_TOP    0x1
#define EMERALDWF_EDGE_BOTTOM 0x2
#define EMERALDWF_EDGE_LEFT   0x4
#define EMERALDWF_EDGE_RIGHT  0x8

// Pure port of emerald's layout_title_objects()/get_title_object_width()/
// update_button_regions() (main.c:2954, 2863, 1653) + the 3x3 cursor/resize
// band math (main.c:309-397). No wayfire dependency.
//
// build() is re-entrant: it re-derives all layout state from ws + ctx
// (client_width/height/width/height/active/state/actions). Part 04 keeps
// one instance per decorator and calls build() whenever the client size,
// title, or theme changes.
class EmeraldLayout
{
  public:
    // Fills ctx.tobj_* and ctx.button_region*/button_region_inact*.
    // Set title_text_width_ before build() (default 0).
    void build(const window_settings *ws, DecorContext &ctx);
    // Frozen contract (overview §5.5): build from the theme's settings.
    void build(const ThemeSettings &ts, DecorContext &ctx);

    // Frame-coord hit testing (x, y relative to frame top-left).
    int hit_button(const DecorContext &ctx, int x, int y, bool active) const; // btype or -1
    uint32_t resize_edges(const DecorContext &ctx, int x, int y) const;       // EMERALDWF_EDGE_* | 0
    bool in_titlebar(const DecorContext &ctx, int x, int y) const;
    int cursor_region(const DecorContext &ctx, int x, int y) const;           // 0..8 (3x3 grid)

    // From settings.ini [titlebars] (loaded by part 02 into ws).
    int double_click_action(const DecorContext &ctx) const;
    int middle_click_action(const DecorContext &ctx) const;

    // The Pango-measured width of the window title. Part 04 measures it
    // with Pango and sets it before calling build(); tests set it directly.
    // (Separate from ctx.tobj_item_width, which emerald uses as the zone
    // width accumulator, main.c:2991.)
    int title_text_width_ = 0;

  private:
    // Object placement in frame coordinates (port of layout_title_objects
    // + position_title_object, main.c:2954-2952).
    void place_objects(const window_settings *ws, DecorContext &ctx) const;
    // Button hit/draw rects for both active and inactive sets
    // (port of update_button_regions, main.c:1653-1766).
    void update_button_regions(const window_settings *ws, DecorContext &ctx) const;

    // Ports of emerald helpers.
    static int title_object_type(char obj);
    int title_object_width(const window_settings *ws, const DecorContext &ctx,
                           char obj) const;
    int real_pos(const window_settings *ws, const DecorContext &ctx,
                 int tobj) const;

    // 3x3 band table (port of update_window_extents, main.c:309).
    //
    // COORDINATE CONVENTION (finding 13 — read before editing): the table
    // is in ORIGINAL emerald FRAME coords — the frame window's top-left is
    // the origin and the client is inset by (l_ext, t_ext_nott), the BORDER
    // thickness (the event windows are children of the frame window,
    // main.c:3451). The port's frame adds the SHADOW inside the margins, so
    // resize_edges()/cursor_region() must shift by the shadow spread
    // (*_space - win_extents - extra) before applying the table.
    // Derived per call, never stored — keeps the class stateless.
    static pos_t band_table(const window_settings *ws, int i, int j)
    {
        int tt_h = ws->titlebar_height;
        int l_ext = ws->win_extents.left + ws->extra_extents.left;
        int r_ext = ws->win_extents.right + ws->extra_extents.right;
        int t_ext_nott = ws->win_extents.top + ws->extra_extents.top;
        int t_ext = t_ext_nott + tt_h;
        int b_ext = ws->win_extents.bottom + ws->extra_extents.bottom;

        pos_t t[3][3] = {
            {
                {0,      0,      l_ext, t_ext,      0, 0, 0, 0}, // 1 top-left corner
                {l_ext,  0,      0,     t_ext_nott, 0, 0, 1, 0}, // 2 top side
                {l_ext,  0,      r_ext, t_ext,      1, 0, 0, 0}, // 3 top-right corner
            },
            {
                {0,      t_ext, l_ext, 0,          0, 0, 0, 1}, // 4 left side
                {l_ext,  t_ext_nott, 0, tt_h,       0, 0, 1, 0}, // 5 titlebar
                {l_ext,  t_ext, r_ext, 0,          1, 0, 0, 1}, // 6 right side
            },
            {
                {0,      t_ext, l_ext, b_ext,      0, 1, 0, 0}, // 7 bottom-left corner
                {l_ext,  t_ext, 0,     b_ext,       0, 1, 1, 0}, // 8 bottom side
                {l_ext,  t_ext, r_ext, b_ext,       1, 1, 0, 0}, // 9 bottom-right corner
            },
        };
        return t[i][j];
    }
};

} // namespace emeraldwf
} // namespace wf
