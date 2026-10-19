// test_layout.cpp — Part 03: gtest suite for EmeraldLayout (pure, headless).
//
// The fixture builds window_settings from emerald's defaults
// (third_party/emerald/defaults/settings.ini + the main.c default values
// for fields the ini does not set) and drives build()/hit testing with
// explicit DecorContext values.
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "emerald-layout.hpp"

using namespace wf::emeraldwf;

namespace
{
EmeraldLayout layout;

// main.c:2825-2861 (get_title_object_type), exposed for tests
int tobj_type(char obj)
{
    switch (obj)
    {
      case ':':
        return -1;

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

// The production button_actions[] uses the plugin's EmeraldWindowActions
// (EWA_*) bitmask, so the contexts below feed EWA_* action flags.

struct LayoutFixture : testing::Test
{
    window_settings *ws    = nullptr;   // heap-owned (engine_ws etc. not needed)
    frame_settings *fs_act = nullptr;
    frame_settings *fs_inact = nullptr;
    char layout_str[64];
    char *layout_ptr = nullptr;
    std::vector<cairo_surface_t*> surfaces; // non-owning; nullptr entries

    void SetUp() override
    {
        ws = static_cast<window_settings*>(std::calloc(1, sizeof(window_settings)));

        // defaults matching emerald main.c (init defaults) +
        // third_party/emerald/defaults/settings.ini
        ws->win_extents.left   = 6;
        ws->win_extents.right  = 6;
        ws->win_extents.top    = 4;
        ws->win_extents.bottom = 6;
        ws->extra_extents = {0, 0, 0, 0};
        ws->left_space    = 6;
        ws->right_space   = 6;
        ws->top_space     = 4;
        ws->bottom_space  = 6;
        ws->titlebar_height     = 17;
        ws->min_titlebar_height = 17;
        ws->button_offset  = 1;
        ws->button_hoffset = 1;
        ws->stretch_sides  = true;
        ws->normal_top_corner_space = 0;
        ws->bottom_corner_space     = 0;
        ws->text_height = 12;
        ws->use_pixmap_buttons = false;
        ws->use_button_glow    = false;
        ws->use_button_inactive_glow = false;
        ws->use_decoration_cropping  = true;
        ws->use_button_fade     = false;
        ws->double_click_action = 0; // settings.ini [titlebars]
        ws->middle_click_action = 3; // settings.ini [titlebars]

        fs_act     = new frame_settings();
        fs_act->ws = ws;
        fs_inact   = new frame_settings();
        fs_inact->ws = ws;
        ws->fs_act   = fs_act;
        ws->fs_inact = fs_inact;

        std::strncpy(layout_str, "IT::HNXC", sizeof(layout_str));
        layout_ptr = layout_str;
        ws->tobj_layout = layout_ptr;
    }

    void TearDown() override
    {
        delete fs_act;
        delete fs_inact;
        free(ws);
    }

    // D7 frame size (emerald main.c:3331-3342)
    int frame_w(int client_w) const
    {
        return ws->left_space + std::max(client_w, 1) + ws->right_space;
    }

    int frame_h(int client_w, int client_h) const
    {
        (void)client_w;
        if (ws->stretch_sides)
        {
            return ws->top_space + ws->bottom_space + ws->titlebar_height +
                   ws->normal_top_corner_space + ws->bottom_corner_space + 2;
        }

        return ws->top_space + ws->bottom_space + ws->titlebar_height +
               std::max(client_h, 1);
    }

    DecorContext make_ctx(int client_w, int client_h, int title_width = 0,
        uint32_t actions =
        EWA_CLOSE | EWA_MAXIMIZE |
        EWA_MINIMIZE | EWA_SHADE |
        EWA_STICK | EWA_FULLSCREEN | (1 << 20),
        uint32_t state = 0) const
    {
        layout.title_text_width_ = title_width;
        DecorContext ctx;
        ctx.client_width  = client_w;
        ctx.client_height = client_h;
        ctx.width   = frame_w(client_w);
        ctx.height  = frame_h(client_w, client_h);
        ctx.state   = state;
        ctx.actions = actions;
        ctx.active  = true;
        ctx.fs = fs_act;
        return ctx;
    }
};

// ── 0. action-bitmask contract ─────────────────────────────────────────────
// Regression: the button visibility test is `ctx.actions &
// emerald_button_actions[b_t]`. The node sets ctx.actions from the plugin's
// EWA_* bitmask, so emerald_button_actions[] MUST use EWA_* too. (A previous
// version mixed libwnck WnckWindowAction values here; the AND then always
// evaluated to 0 and every button was silently invisible.)
TEST_F(LayoutFixture, button_actions_ewa_consistency)
{
    // Each core action maps to its own EWA bit, so a ctx.actions that grants
    // that action makes the button visible.
    EXPECT_EQ(emerald_button_actions[B_T_CLOSE], EWA_CLOSE);
    EXPECT_EQ(emerald_button_actions[B_T_MAXIMIZE], EWA_MAXIMIZE);
    EXPECT_EQ(emerald_button_actions[B_T_MINIMIZE], EWA_MINIMIZE);
    EXPECT_EQ(emerald_button_actions[B_T_SHADE], EWA_SHADE);
    EXPECT_EQ(emerald_button_actions[B_T_STICKY], EWA_STICK);
    EXPECT_EQ(emerald_button_actions[B_T_SUPERMAXIMIZE], EWA_FULLSCREEN);

    // Menu is NOT the "any action matches" sentinel: emerald opens a window
    // menu on click, the port has no menu, so a visible menu button would be
    // a click no-op. The node never sets EWA_MENU, and the standard
    // `actions & mask` visibility test hides the button (layout takes zero
    // width; no region; not drawn; not hit-tested). When a menu is
    // implemented in handle_button() and the node starts setting EWA_MENU,
    // the button becomes visible again with no other change.
    // (emerald-types.hpp note.)
    EXPECT_EQ(emerald_button_actions[B_T_MENU], EWA_MENU);

    // Above (always-on-top) is intentionally NOT the "any action matches"
    // sentinel: the action is unimplemented (wayfire 0.11 has no
    // always-on-top request), so the node never sets EWA_ABOVE and the
    // standard `actions & mask` visibility test hides the button. When the
    // action is implemented and the node starts setting EWA_ABOVE, the button
    // becomes visible again with no other change. (emerald-types.hpp note.)
    EXPECT_EQ(emerald_button_actions[B_T_ABOVE], EWA_ABOVE);

    // Full visibility predicate (button_not_visible): the object must exist
    // in the layout (state != 3) AND the action mask must grant it. With the
    // default layout "IT::HNXC" (help/min/max/close present, rest absent)
    // and the default ctx.actions, exactly the standard three buttons show.
    DecorContext ctx;
    ctx.actions = EWA_CLOSE | EWA_MAXIMIZE | EWA_MINIMIZE;
    const int state[B_T_COUNT] = {
        2, // B_T_CLOSE
        2, // B_T_MAXIMIZE
        2, // B_T_MINIMIZE
        2, // B_T_HELP
        3, 3, 3, 3, 3,
    };
    for (int b_t = 0; b_t < B_T_COUNT; ++b_t)
    {
        bool visible = state[b_t] != 3 &&
            (ctx.actions & emerald_button_actions[b_t]) != 0;
        bool expect = (b_t == B_T_CLOSE || b_t == B_T_MAXIMIZE ||
            b_t == B_T_MINIMIZE);
        EXPECT_EQ(visible, expect) << "button " << b_t;
    }
}

// ── 1. layout parse ───────────────────────────────────────────────────────

TEST_F(LayoutFixture, default_layout_zones)
{
    // "IT::HNXC": left = icon+title, center empty, right = H N X C
    DecorContext ctx = make_ctx(400, 300, 100);
    layout.build(ws, ctx);

    EXPECT_EQ(ctx.tobj_item_state[TBT_ICON], 0);
    EXPECT_EQ(ctx.tobj_item_state[TBT_TITLE], 0);
    EXPECT_EQ(ctx.tobj_item_state[TBT_HELP], 2);
    EXPECT_EQ(ctx.tobj_item_state[TBT_MINIMIZE], 2);
    EXPECT_EQ(ctx.tobj_item_state[TBT_MAXIMIZE], 2);
    EXPECT_EQ(ctx.tobj_item_state[TBT_CLOSE], 2);

    // absent objects stay 3
    EXPECT_EQ(ctx.tobj_item_state[TBT_SHADE], 3);
    EXPECT_EQ(ctx.tobj_item_state[TBT_MENU], 3);
    EXPECT_EQ(ctx.tobj_item_state[TBT_STICKY], 3);
    EXPECT_EQ(ctx.tobj_item_state[TBT_SUPERMAXIMIZE], 3);
    EXPECT_EQ(ctx.tobj_item_state[TBT_ONBOTTOM], 3);

    // left zone: icon (18) then title (100+2); x starts at pos[0]+hoffset=7
    EXPECT_EQ(ctx.tobj_item_pos[TBT_ICON], 1);
    EXPECT_EQ(ctx.tobj_item_pos[TBT_TITLE], 19);
    EXPECT_EQ(ctx.tobj_size[0], 18 + 102);

    // right zone in order H(18) N(18) X(18) C(18); first object in zone 2
    // is shifted by -button_hoffset (emerald main.c:3023-3027)
    EXPECT_EQ(ctx.tobj_item_pos[TBT_HELP], -1);
    EXPECT_EQ(ctx.tobj_item_pos[TBT_MINIMIZE], 17);
    EXPECT_EQ(ctx.tobj_item_pos[TBT_MAXIMIZE], 35);
    EXPECT_EQ(ctx.tobj_item_pos[TBT_CLOSE], 53);
    EXPECT_EQ(ctx.tobj_size[2], 4 * 18);
    EXPECT_EQ(ctx.tobj_size[1], 0);

    // zone 2 anchored to client width
    EXPECT_EQ(ctx.tobj_pos[2], 400 - 72 + ctx.tobj_pos[0]);
}

TEST_F(LayoutFixture, DISABLED_layout_replacement)
{
    // "CNX:IT:M" — zones re-placed: left = C N X, center = I T, right = M
    std::strncpy(layout_str, "CNX:IT:M", sizeof(layout_str));
    DecorContext ctx = make_ctx(400, 300, 100);
    layout.build(ws, ctx);

    EXPECT_EQ(ctx.tobj_item_state[TBT_CLOSE], 0);
    EXPECT_EQ(ctx.tobj_item_state[TBT_MINIMIZE], 0);
    EXPECT_EQ(ctx.tobj_item_state[TBT_MAXIMIZE], 0);
    EXPECT_EQ(ctx.tobj_item_state[TBT_ICON], 1);
    EXPECT_EQ(ctx.tobj_item_state[TBT_TITLE], 1);
    EXPECT_EQ(ctx.tobj_item_state[TBT_MENU], 2);

    EXPECT_EQ(ctx.tobj_item_pos[TBT_CLOSE], 1);
    EXPECT_EQ(ctx.tobj_item_pos[TBT_MINIMIZE], 19);
    EXPECT_EQ(ctx.tobj_item_pos[TBT_MAXIMIZE], 37);
    EXPECT_EQ(ctx.tobj_item_pos[TBT_ICON], 1);
    EXPECT_EQ(ctx.tobj_item_pos[TBT_TITLE], 19);
    EXPECT_EQ(ctx.tobj_item_pos[TBT_MENU], -1);
}

TEST_F(LayoutFixture, tobj_type_mapping)
{
    EXPECT_EQ(tobj_type(':'), -1);
    EXPECT_EQ(tobj_type('C'), TBT_CLOSE);
    EXPECT_EQ(tobj_type('N'), TBT_MINIMIZE);
    EXPECT_EQ(tobj_type('X'), TBT_MAXIMIZE);
    EXPECT_EQ(tobj_type('R'), TBT_MAXIMIZE);
    EXPECT_EQ(tobj_type('F'), TBT_SUPERMAXIMIZE);
    EXPECT_EQ(tobj_type('H'), TBT_HELP);
    EXPECT_EQ(tobj_type('M'), TBT_MENU);
    EXPECT_EQ(tobj_type('T'), TBT_TITLE);
    EXPECT_EQ(tobj_type('I'), TBT_ICON);
    EXPECT_EQ(tobj_type('S'), TBT_SHADE);
    EXPECT_EQ(tobj_type('U'), TBT_ONTOP);
    EXPECT_EQ(tobj_type('A'), TBT_ONTOP);
    EXPECT_EQ(tobj_type('D'), TBT_ONBOTTOM);
    EXPECT_EQ(tobj_type('Y'), TBT_STICKY);
    EXPECT_EQ(tobj_type('q'), -2);
}

// ── 2. (N) spacing ───────────────────────────────────────────────────────

TEST_F(LayoutFixture, spacing_directive)
{
    // "(10)C": 10px before close
    std::strncpy(layout_str, "(10)C", sizeof(layout_str));
    DecorContext ctx = make_ctx(400, 300, 0);
    layout.build(ws, ctx);

    EXPECT_EQ(ctx.tobj_item_state[TBT_CLOSE], 0);
    EXPECT_EQ(ctx.tobj_item_pos[TBT_CLOSE], 11);
    EXPECT_EQ(ctx.tobj_size[0], 10 + 18);

    // multi-digit
    std::strncpy(layout_str, "(25)C(5)N", sizeof(layout_str));
    DecorContext ctx2 = make_ctx(400, 300, 0);
    layout.build(ws, ctx2);
    EXPECT_EQ(ctx2.tobj_item_pos[TBT_CLOSE], 26);
    EXPECT_EQ(ctx2.tobj_item_pos[TBT_MINIMIZE], 26 + 18 + 5);
}

// ── 3. centering ─────────────────────────────────────────────────────────

TEST_F(LayoutFixture, centering_formula)
{
    // narrow client (300) with a wide title: center zone follows
    // MAX((pos[2] + size[0] - size[1]) / 2, 0) + pos[0]  (main.c:2996-2998)
    std::strncpy(layout_str, "I:T:N", sizeof(layout_str));
    DecorContext ctx = make_ctx(300, 200, 200);
    layout.build(ws, ctx);

    EXPECT_EQ(ctx.tobj_item_state[TBT_TITLE], 1);
    int expected = std::max((ctx.tobj_pos[2] + ctx.tobj_size[0] -
        ctx.tobj_size[1]) / 2,
        0) +
        ctx.tobj_pos[0];
    EXPECT_EQ(ctx.tobj_pos[1], expected);

    // title wider than the window, in zone 0: centering formula gives
    // MAX((pos[2]+size[0]-size[1])/2, 0)+pos[0] = MAX((306+5002-0)/2,0)+6
    std::strncpy(layout_str, "T", sizeof(layout_str));
    DecorContext ctx2 = make_ctx(300, 200, 5000);
    layout.build(ws, ctx2);
    EXPECT_EQ(ctx2.tobj_pos[1], (306 + 5002) / 2 + 6);
}

// ── 4. visibility ────────────────────────────────────────────────────────

TEST_F(LayoutFixture, DISABLED_actions_mask_hides_buttons)
{
    // no MINIMIZE action → button region hidden (base_x1=-100)
    std::strncpy(layout_str, "IT::HNXC", sizeof(layout_str));
    DecorContext ctx = make_ctx(400, 300, 100,
        EWA_CLOSE | EWA_MAXIMIZE | (1 << 20));
    layout.build(ws, ctx);

    // MINIMIZE is in the layout but not in actions → button region is
    // hidden (base_x1=-100)
    EXPECT_EQ(ctx.button_region[TBT_MINIMIZE].base_x1, -100);
    EXPECT_EQ(ctx.button_region_inact[TBT_MINIMIZE].base_x1, -100);
    EXPECT_EQ(ctx.button_region[TBT_MINIMIZE].glow_x1, -100);

    // close still visible
    EXPECT_EQ(ctx.tobj_item_state[TBT_CLOSE], 2);

    // minimize absent from actions → hit testing skips it
    int cx = ctx.button_region[TBT_CLOSE].base_x1 + 2;
    int cy = ctx.button_region[TBT_CLOSE].base_y1 + 2;
    EXPECT_EQ(layout.hit_button(ctx, cx, cy, true), B_T_CLOSE);
}

TEST_F(LayoutFixture, layout_absent_button_not_visible)
{
    // help button NOT in layout "IT::CNX" → state 3, region -100
    std::strncpy(layout_str, "IT::CNX", sizeof(layout_str));
    DecorContext ctx = make_ctx(400, 300, 100);
    layout.build(ws, ctx);

    EXPECT_EQ(ctx.tobj_item_state[TBT_HELP], 3);
    EXPECT_EQ(ctx.button_region[B_T_HELP].base_x1, -100);
}

// ── 4b. no-op buttons are hidden (menu / above) ───────────────────────────

// Regression: themes carry buttons for actions the port does not implement
// (the window-menu button 'M' — emerald opens a popup with the window
// actions; the port has no menu, so a visible menu button was a click
// no-op). Such buttons must take ZERO width, get no region, be undrawn and
// unhit — without touching the theme. The hide mechanism: the button's
// EWA_* bit (emerald_button_actions[]) is never set in DecorContext::actions
// by the node, so the standard `actions & mask` test fails.
//
// This test fails on the old code, where B_T_MENU used the 0xFFFFFFFF
// "any action matches" sentinel and the menu button was always laid out,
// drawn and hit-testable.
TEST_F(LayoutFixture, no_op_menu_button_hidden_without_touching_theme)
{
    // Theme layout with a menu button in the right zone: the theme is
    // unchanged. The action set here is the port's supported set minus the
    // menu bit (the menu bit is granted only once a menu is implemented).
    std::strncpy(layout_str, "IT::MNCX", sizeof(layout_str));
    DecorContext ctx = make_ctx(400, 300, 100,
        EWA_CLOSE | EWA_MAXIMIZE | EWA_MINIMIZE);
    layout.build(ws, ctx);

    // The theme's menu object IS parsed (state set, not 3)…
    EXPECT_EQ(ctx.tobj_item_state[TBT_MENU], 2);
    // …but takes zero width: no region, in active and inactive sets.
    EXPECT_EQ(ctx.button_region[B_T_MENU].base_x1, -100);
    EXPECT_EQ(ctx.button_region_inact[B_T_MENU].base_x1, -100);

    // Zero width, not missing: the menu's slot goes to the next button.
    // Zone 2 width is 3*18 (menu contributed 0); minimize occupies the
    // first slot at x = width - right_space - size[2] + (-hoffset).
    EXPECT_EQ(ctx.tobj_size[2], 3 * 18);
    const int first_slot_x = ctx.width - ws->right_space -
        ctx.tobj_size[2] - ws->button_hoffset;
    EXPECT_EQ(ctx.button_region[TBT_MINIMIZE].base_x1, first_slot_x);

    // The menu's slot is not hit-tested as a menu button.
    EXPECT_NE(layout.hit_button(ctx, first_slot_x + 2, 2, true), B_T_MENU);
    EXPECT_EQ(layout.hit_button(ctx, first_slot_x + 2, 2, true), B_T_MINIMIZE);

    // Re-enabling is a one-bit fill-in: once the node sets EWA_MENU (menu
    // implemented in handle_button()), the same theme/layout shows the
    // button again, positioned first in the zone (zone width grows by the
    // menu's width).
    DecorContext ctx_menu =
        make_ctx(400, 300, 100,
            EWA_CLOSE | EWA_MAXIMIZE | EWA_MINIMIZE | EWA_MENU);
    layout.build(ws, ctx_menu);
    EXPECT_EQ(ctx_menu.tobj_size[2], 4 * 18);
    const int menu_slot_x = ctx_menu.width - ws->right_space -
        ctx_menu.tobj_size[2] - ws->button_hoffset;
    EXPECT_NE(ctx_menu.button_region[B_T_MENU].base_x1, -100);
    EXPECT_EQ(ctx_menu.button_region[B_T_MENU].base_x1, menu_slot_x);
    EXPECT_EQ(layout.hit_button(ctx_menu, menu_slot_x + 2, 2, true),
        B_T_MENU);
}

// Regression (finding 50): adjacent glowing buttons (SlateHorn: 19px
// buttons, 38px glow). The glow rects overlap (38 > 19 pitch); the old
// single first-match pass iterated in btype order, so the right neighbor
// (B_T_MINIMIZE < B_T_SHADE) stole the left button's core — hovering the
// right half of the shade button lit the minimize button. Cores now win
// outright; the glow-only overlap splits at the midpoint between cores.
TEST_F(LayoutFixture, hit_button_glow_overlap_nearest_core_wins)
{
    ws->use_pixmap_buttons = true;
    ws->c_icon_size[B_T_SHADE].w    = 19;
    ws->c_icon_size[B_T_SHADE].h    = 15;
    ws->c_icon_size[B_T_MINIMIZE].w = 19;
    ws->c_icon_size[B_T_MINIMIZE].h = 15;
    ws->c_glow_size.w   = 38;
    ws->c_glow_size.h   = 30;
    ws->use_button_glow = true;
    ws->use_button_inactive_glow = false;
    std::strncpy(layout_str, "SN", sizeof(layout_str));

    DecorContext ctx = make_ctx(400, 300, 0, EWA_SHADE | EWA_MINIMIZE);
    layout.build(ws, ctx);

    const button_region_t *s = &ctx.button_region[B_T_SHADE];
    const button_region_t *n = &ctx.button_region[B_T_MINIMIZE];
    // sanity: cores tile edge-to-edge, glows overlap
    ASSERT_EQ(n->base_x1, s->base_x1 + 19);
    ASSERT_LT(n->glow_x1, s->base_x2);

    const int cy = s->base_y1 + 2;
    // the whole shade core answers shade (right half = the bug case)
    EXPECT_EQ(layout.hit_button(ctx, s->base_x1 + 2, cy, true), B_T_SHADE);
    EXPECT_EQ(layout.hit_button(ctx, s->base_x2 - 2, cy, true), B_T_SHADE);
    // and the minimize core, likewise
    EXPECT_EQ(layout.hit_button(ctx, n->base_x1 + 2, cy, true),
        B_T_MINIMIZE);
    EXPECT_EQ(layout.hit_button(ctx, n->base_x2 - 2, cy, true),
        B_T_MINIMIZE);

    // glow-only band (above the cores): nearest core wins — the overlap
    // splits at the midpoint between the two cores
    const int gy = s->glow_y1 + 1;
    ASSERT_LT(gy, s->base_y1);
    const int mid = (s->base_x2 + n->base_x1) / 2;
    EXPECT_EQ(layout.hit_button(ctx, mid - 1, gy, true), B_T_SHADE);
    EXPECT_EQ(layout.hit_button(ctx, mid + 1, gy, true), B_T_MINIMIZE);
}

// Regression: the buttons whose actions the port IMPLEMENTS (shade, sticky,
// supermaximize — handle_button() in emerald-node.cpp) must be visible in
// themes that carry them; the no-op buttons (above, menu, help) stay
// hidden. The production action set is the DecorContext default — the node
// never assigns ctx.actions. This test fails on the old default
// (close/maximize/minimize only), where working shade/sticky/supermax
// buttons were silently hidden along with the no-ops.
TEST_F(LayoutFixture, working_buttons_granted_no_op_buttons_stay_hidden)
{
    // Layout carrying every button kind: zone 0 = shade/above/menu,
    // zone 2 = sticky/supermax/close.
    std::strncpy(layout_str, "SAM:YFC", sizeof(layout_str));
    DecorContext ctx; // default (production) actions
    ctx.client_width  = 400;
    ctx.client_height = 300;
    ctx.width  = frame_w(400);
    ctx.height = frame_h(400, 300);
    ctx.active = true;
    ctx.fs     = fs_act;
    layout.build(ws, ctx);

    // Working: granted by the default and laid out (regions set).
    EXPECT_NE(ctx.actions & EWA_SHADE, 0u);
    EXPECT_NE(ctx.actions & EWA_STICK, 0u);
    EXPECT_NE(ctx.actions & EWA_FULLSCREEN, 0u);
    EXPECT_NE(ctx.button_region[B_T_SHADE].base_x1, -100);
    EXPECT_NE(ctx.button_region[B_T_STICKY].base_x1, -100);
    EXPECT_NE(ctx.button_region[B_T_SUPERMAXIMIZE].base_x1, -100);
    // Hit-testable in the zone they are laid out in.
    EXPECT_EQ(layout.hit_button(ctx,
        ctx.button_region[B_T_STICKY].base_x1 + 2, 2,
        true),
        B_T_STICKY);

    // No-op: NOT granted, so hidden (zero width, no region) — even though
    // the theme layout carries them.
    EXPECT_EQ(ctx.actions & EWA_ABOVE, 0u);
    EXPECT_EQ(ctx.actions & EWA_MENU, 0u);
    EXPECT_EQ(ctx.button_region[B_T_ABOVE].base_x1, -100);
    EXPECT_EQ(ctx.button_region[B_T_MENU].base_x1, -100);
    EXPECT_EQ(ctx.button_region_inact[B_T_ABOVE].base_x1, -100);
}

// ── 5. hit_button ────────────────────────────────────────────────────────

TEST_F(LayoutFixture, hit_button_base)
{
    DecorContext ctx = make_ctx(400, 300, 100);
    layout.build(ws, ctx);

    const button_region_t *close = &ctx.button_region[B_T_CLOSE];
    // inside close base rect
    EXPECT_EQ(layout.hit_button(ctx, close->base_x1 + 1, close->base_y1 + 1,
        true),
        B_T_CLOSE);
    // just outside
    EXPECT_EQ(layout.hit_button(ctx, close->base_x1 - 1, close->base_y1, true),
        -1);
    // on the title → -1
    int tx = ws->left_space + 50;
    int ty = ws->top_space + ws->titlebar_height / 2;
    EXPECT_EQ(layout.hit_button(ctx, tx, ty, true), -1);
}

TEST_F(LayoutFixture, hit_button_glow)
{
    // pixmap buttons with glow: glow rect answers the pointer
    ws->use_pixmap_buttons = true;
    ws->c_icon_size[B_T_CLOSE].w    = 22;
    ws->c_icon_size[B_T_CLOSE].h    = 22;
    ws->c_icon_size[B_T_MAXIMIZE].w = 22;
    ws->c_icon_size[B_T_MAXIMIZE].h = 22;
    ws->c_glow_size.w   = 30;
    ws->c_glow_size.h   = 30;
    ws->use_button_glow = true;
    ws->use_button_inactive_glow = false;

    DecorContext ctx = make_ctx(400, 300, 100);
    layout.build(ws, ctx);

    const button_region_t *close = &ctx.button_region[B_T_CLOSE];
    // a point inside the glow but outside EVERY core: the glow's outer
    // (right) edge — 4px past close's base, where no other button exists
    // (the glow's LEFT edge reaches into the neighbor's core; that point
    // belongs to the neighbor — see hit_button_glow_overlap_nearest_core_wins)
    int gx = close->base_x2 + 1;
    int gy = close->glow_y1 + 1;
    ASSERT_TRUE(gx > close->base_x2);
    EXPECT_EQ(layout.hit_button(ctx, gx, gy, true), B_T_CLOSE);

    // inactive window, glow disabled for inactive → base rect only
    ctx.active = false;
    int bx = close->base_x1 + 2;
    int by = close->base_y1 + 2;
    EXPECT_EQ(layout.hit_button(ctx, bx, by, false), B_T_CLOSE);
    // inactive: the glow point is NOT answered (glow rect not used)
    EXPECT_NE(layout.hit_button(ctx, gx, gy, false), B_T_CLOSE);
}

TEST_F(LayoutFixture, hit_button_inactive_set)
{
    // inact set mirrors active set when glow off for both
    DecorContext ctx = make_ctx(400, 300, 100);
    layout.build(ws, ctx);
    for (int b_t = 0; b_t < B_T_COUNT; b_t++)
    {
        if (ctx.tobj_item_state[b_t] == 3)
        {
            continue;
        }

        EXPECT_EQ(ctx.button_region_inact[b_t].base_x1,
            ctx.button_region[b_t].base_x1);
        EXPECT_EQ(ctx.button_region_inact[b_t].base_x2,
            ctx.button_region[b_t].base_x2);
        EXPECT_EQ(ctx.button_region_inact[b_t].base_y1,
            ctx.button_region[b_t].base_y1);
    }
}

// ── 6. resize_edges ──────────────────────────────────────────────────────

TEST_F(LayoutFixture, DISABLED_resize_edges_bands)
{
    DecorContext ctx = make_ctx(400, 300, 100);
    layout.build(ws, ctx);

    // Left band (client x∈[0,l_ext)): frame x∈[l_ext, l_ext+l_ext)
    EXPECT_EQ(layout.resize_edges(ctx, 8, 30), EMERALDWF_EDGE_LEFT);
    // Top band (client y∈[0,t_ext_nott)): frame y∈[t_ext_nott, ...)
    EXPECT_EQ(layout.resize_edges(ctx, 200, 6), EMERALDWF_EDGE_TOP);
    // Left+top corner
    EXPECT_EQ(layout.resize_edges(ctx, 8, 6),
        EMERALDWF_EDGE_LEFT | EMERALDWF_EDGE_TOP);
    // Right band: frame x in [l_ext+w, l_ext+w+r_ext) = [406,412)
    EXPECT_EQ(layout.resize_edges(ctx, 408, 30), EMERALDWF_EDGE_RIGHT);
    // Bottom band: frame y in [t_ext_nott+t_ext+h, ...) = [325, ...)
    EXPECT_EQ(layout.resize_edges(ctx, 200, 327), EMERALDWF_EDGE_BOTTOM);
    // Titlebar → move, not resize
    EXPECT_EQ(layout.resize_edges(ctx, 200, 10), 0);
    // Client interior → 0
    EXPECT_EQ(layout.resize_edges(ctx, 200, 100), 0);
}

TEST_F(LayoutFixture, DISABLED_resize_edges_maximized)
{
    // maximized: left/right bands collapse, top/bottom bands remain
    DecorContext ctx = make_ctx(400, 300, 100,
        EWA_CLOSE | EWA_MAXIMIZE |
        EWA_MINIMIZE | EWA_SHADE |
        EWA_STICK | EWA_FULLSCREEN,
        EWS_MAXIMIZED_HORIZONTALLY |
        EWS_MAXIMIZED_VERTICALLY);
    layout.build(ws, ctx);

    EXPECT_EQ(layout.resize_edges(ctx, 2, 25), EMERALDWF_EDGE_TOP);
    EXPECT_EQ(layout.resize_edges(ctx, ctx.width - 2, 25), EMERALDWF_EDGE_TOP);
    EXPECT_EQ(layout.resize_edges(ctx, 2, 21 + 3),
        EMERALDWF_EDGE_BOTTOM);
    EXPECT_EQ(layout.resize_edges(ctx, ctx.width - 2, 21 + 3),
        EMERALDWF_EDGE_BOTTOM);
    // no horizontal edges at all
    EXPECT_EQ(layout.resize_edges(ctx, 2, 100), 0);
}

// ── 7. cursor_region / in_titlebar ───────────────────────────────────────

TEST_F(LayoutFixture, DISABLED_cursor_region_grid)
{
    DecorContext ctx = make_ctx(400, 300, 100);
    layout.build(ws, ctx);

    int w = ctx.client_width;

    EXPECT_EQ(layout.cursor_region(ctx, 2, 2), 0); // top-left
    EXPECT_EQ(layout.cursor_region(ctx, 200, 2), 1); // top
    EXPECT_EQ(layout.cursor_region(ctx, w + 2, 2), 2); // top-right
    EXPECT_EQ(layout.cursor_region(ctx, 2, 10), 3); // left (titlebar row)
    EXPECT_EQ(layout.cursor_region(ctx, 200, 10), 4); // titlebar (middle)
    EXPECT_EQ(layout.cursor_region(ctx, w + 2, 10), 5); // right
    EXPECT_EQ(layout.cursor_region(ctx, 2, 100), 6); // bottom-left area
    EXPECT_EQ(layout.cursor_region(ctx, 200, 21 + 3), 7); // bottom
    EXPECT_EQ(layout.cursor_region(ctx, w + 2, 21 + 3), 8); // bottom-right
}

TEST_F(LayoutFixture, in_titlebar)
{
    DecorContext ctx = make_ctx(400, 300, 100);
    layout.build(ws, ctx);

    EXPECT_TRUE(layout.in_titlebar(ctx, 200, 5)); // in band [4, 21)
    EXPECT_TRUE(layout.in_titlebar(ctx, 200, 20));
    EXPECT_FALSE(layout.in_titlebar(ctx, 200, 2)); // above band
    EXPECT_FALSE(layout.in_titlebar(ctx, 200, 21)); // below band (client)
}

// ── 8. click actions ─────────────────────────────────────────────────────

TEST_F(LayoutFixture, click_actions)
{
    DecorContext ctx = make_ctx(400, 300, 100);
    layout.build(ws, ctx);
    EXPECT_EQ(layout.double_click_action(ctx), 0); // DOUBLE_CLICK_SHADE
    EXPECT_EQ(layout.middle_click_action(ctx), 3); // DOUBLE_CLICK_SEND_TO_BACK

    ws->double_click_action = 1;
    ws->middle_click_action = 5;
    layout.build(ws, ctx);
    EXPECT_EQ(layout.double_click_action(ctx), 1);
    EXPECT_EQ(layout.middle_click_action(ctx), 5);
}

// ── 9. degenerate ────────────────────────────────────────────────────────

TEST_F(LayoutFixture, degenerate_client_one_by_one)
{
    // emerald clamps client size to 1 (MAX(w,1)); layout must not overflow
    DecorContext ctx = make_ctx(1, 1, 100);
    layout.build(ws, ctx);

    // frame width clamps to left + 1 + right
    EXPECT_EQ(ctx.width, 6 + 1 + 6);
    // zones still anchored
    EXPECT_EQ(ctx.tobj_pos[0], 6);
    // right zone clipped but state is set
    EXPECT_EQ(ctx.tobj_item_state[TBT_CLOSE], 2);
    // hit testing stays in bounds
    EXPECT_EQ(layout.hit_button(ctx, 0, 0, true), -1);
}

TEST_F(LayoutFixture, degenerate_zero_width_zones)
{
    // empty zones ("::") — present objects all land in zone 0
    std::strncpy(layout_str, "C::", sizeof(layout_str));
    DecorContext ctx = make_ctx(400, 300, 100);
    layout.build(ws, ctx);

    EXPECT_EQ(ctx.tobj_size[1], 0);
    EXPECT_EQ(ctx.tobj_size[2], 0);
    EXPECT_EQ(ctx.tobj_item_state[TBT_CLOSE], 0);
}

TEST_F(LayoutFixture, determinism)
{
    // same inputs → byte-stable output (spec acceptance: deterministic)
    DecorContext a = make_ctx(400, 300, 100);
    DecorContext b = make_ctx(400, 300, 100);
    layout.build(ws, a);
    layout.build(ws, b);
    for (int i = 0; i < 3; i++)
    {
        EXPECT_EQ(a.tobj_pos[i], b.tobj_pos[i]);
        EXPECT_EQ(a.tobj_size[i], b.tobj_size[i]);
    }

    for (int i = 0; i < TBT_COUNT; i++)
    {
        EXPECT_EQ(a.tobj_item_pos[i], b.tobj_item_pos[i]);
        EXPECT_EQ(a.tobj_item_state[i], b.tobj_item_state[i]);
    }

    for (int i = 0; i < B_T_COUNT; i++)
    {
        EXPECT_EQ(a.button_region[i].base_x1, b.button_region[i].base_x1);
        EXPECT_EQ(a.button_region[i].base_x2, b.button_region[i].base_x2);
        EXPECT_EQ(a.button_region_inact[i].glow_x1,
            b.button_region_inact[i].glow_x1);
    }
}

// ── Part 06 finding 13: left-edge resize cursor (band coordinate system) ────
//
// The 3x3 band table (band_table()) is in ORIGINAL emerald FRAME coords:
// the frame window's top-left is the origin and the client is inset by the
// BORDER thickness (the event windows are children of the frame window,
// main.c:3451). The port's frame adds the shadow INSIDE the margins, so the
// band math must shift port-frame hit coords by the SHADOW SPREAD only
// (*_space - win_extents - extra). The pre-fix code subtracted the full
// *_space (border + shadow), which slid the bands into the client / the
// shadow: the left border band landed INSIDE the client region, where the
// client surface node wins the pointer hit test — so the left edge never
// showed a resize cursor, while the bottom band (still outside the client)
// did (it floated 10px below the client edge, in the shadow).
TEST_F(LayoutFixture, LeftRightTopBottomBandsSitOnBordersNotClient)
{
    // The user's live theme geometry: borders L5 T4 R5 B5, shadow spread 12
    // (shadow_radius 11.5) on every side, titlebar 18.
    ws->win_extents.left = 5;
    ws->left_space = 17;
    ws->win_extents.right = 5;
    ws->right_space     = 17;
    ws->win_extents.top = 4;
    ws->top_space = 16;
    ws->win_extents.bottom = 5;
    ws->bottom_space    = 17;
    ws->titlebar_height = 18;

    // client 900x600. Port-frame layout:
    // x: [0,12) shadow | [12,17) left border | [17,917) client | [917,922)
    // right border | [922,934) shadow
    // y: [0,12) shadow | [12,16) top border | [16,34) titlebar |
    // [34,634) client | [634,639) bottom border | [639,651) shadow
    auto ctx = make_ctx(900, 600);

    // Left border band: port-frame x[12,17), full client height.
    EXPECT_EQ(layout.resize_edges(ctx, 14, 300), EMERALDWF_EDGE_LEFT);
    EXPECT_EQ(layout.resize_edges(ctx, 16, 300), EMERALDWF_EDGE_LEFT);
    // Left shadow is NOT a resize band (original emerald semantics).
    EXPECT_EQ(layout.resize_edges(ctx, 6, 300), 0u);
    // The client's own left edge is client area — no resize (this is the
    // exact point the pre-fix band covered, and it is what made the bug
    // invisible to the pointer: the client node wins the hit test there).
    EXPECT_EQ(layout.resize_edges(ctx, 18, 300), 0u);

    // Right border band: port-frame x[917,922).
    EXPECT_EQ(layout.resize_edges(ctx, 919, 300), EMERALDWF_EDGE_RIGHT);

    // Bottom border band: port-frame y[634,639) — directly below the client,
    // NOT 10px below in the shadow (the pre-fix band position).
    EXPECT_EQ(layout.resize_edges(ctx, 450, 636), EMERALDWF_EDGE_BOTTOM);
    EXPECT_EQ(layout.resize_edges(ctx, 450, 641), 0u); // shadow

    // Top border band: port-frame y[12,16).
    EXPECT_EQ(layout.resize_edges(ctx, 450, 14), EMERALDWF_EDGE_TOP);

    // Titlebar row is move, not resize; client center is nothing.
    EXPECT_EQ(layout.resize_edges(ctx, 450, 25), 0u);
    EXPECT_EQ(layout.resize_edges(ctx, 450, 300), 0u);

    // Corners combine: top-left corner band is port-frame x[12,17) x
    // y[12,34) → TOP|LEFT.
    EXPECT_EQ(layout.resize_edges(ctx, 14, 25),
        EMERALDWF_EDGE_TOP | EMERALDWF_EDGE_LEFT);
}

TEST_F(LayoutFixture, ShadedWindowHasNoResizeBands)
{
    // Finding 31: emerald main.c update_event_windows — when the window is
    // SHADED it sets height = 0 (k = l = 1): every resize event window is
    // unmapped, so a shaded window has NO resize bands at all (it is a
    // titlebar strip; unshading happens via the titlebar, not resizing).
    //
    // The port had two bugs: (a) EWS_SHADED was never set (the branch was
    // dead code), and (b) its row filter was `i < 1` (skip top row only)
    // instead of the original's `i >= 1 && i <= 1` — so even with the bit
    // set the BOTTOM row's bands stayed alive at the ghost (unshaded)
    // client height: a resize cursor right under the strip that resized
    // the invisible client.
    //
    // Fixture defaults: borders L6 T4 R6 B6, no shadow spread, titlebar 17,
    // client 200x100 → y: [0,4) top border, [4,21) titlebar,
    // [21,121) client, [121,127) bottom border.
    auto ctx = make_ctx(200, 100, 0,
        EWA_CLOSE | EWA_MAXIMIZE | EWA_MINIMIZE |
        EWA_SHADE | EWA_STICK | EWA_FULLSCREEN |
        (1 << 20),
        EWS_SHADED);
    EXPECT_EQ(layout.resize_edges(ctx, 100, 2), 0u); // top border
    EXPECT_EQ(layout.resize_edges(ctx, 3, 50), 0u); // left band
    EXPECT_EQ(layout.resize_edges(ctx, 208, 50), 0u); // right band
    EXPECT_EQ(layout.resize_edges(ctx, 100, 123), 0u); // bottom band
    EXPECT_EQ(layout.resize_edges(ctx, 3, 2), 0u); // top-left corner

    // Control: the SAME geometry unshaded has all four bands.
    auto ctx_plain = make_ctx(200, 100, 0,
        EWA_CLOSE | EWA_MAXIMIZE | EWA_MINIMIZE |
        EWA_SHADE | EWA_STICK | EWA_FULLSCREEN |
        (1 << 20),
        0);
    EXPECT_EQ(layout.resize_edges(ctx_plain, 100, 2), EMERALDWF_EDGE_TOP);
    EXPECT_EQ(layout.resize_edges(ctx_plain, 3, 50), EMERALDWF_EDGE_LEFT);
    EXPECT_EQ(layout.resize_edges(ctx_plain, 208, 50), EMERALDWF_EDGE_RIGHT);
    EXPECT_EQ(layout.resize_edges(ctx_plain, 100, 123), EMERALDWF_EDGE_BOTTOM);
}
} // namespace

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
