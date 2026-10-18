// test_margins.cpp — headless gtest coverage of the margin math (Part 01).
//
// Exercises emerald_decorator_t::margins_for() (pure) and, through a
// constructed DecorContext-driven path, the D8 rules:
//   1. normal windowed        → {t, t, t, top_space+tb}
//   2. fullscreen             → zero
//   3. CSD                    → zero
//   4. maximized + borders on → {t, t, t, top_space+tb}
//   5. maximized + borders off→ {shadow, shadow, shadow, shadow_top+tb}
//      (finding 19: the shadow spread remains; both off → {0,0,0,tb})
//   6. extra_extents nonzero  → added per side
//
// The *_space values are the engine layout (border + shadow), so the
// margins must match them — the engines draw the title bar at
// top_space + titlebar_height below the frame top.
#include <gtest/gtest.h>

#include <cstring>

#include "emerald-decorator.hpp"

namespace
{
using wf::emeraldwf::MarginsInput;
using wf::emeraldwf::ThemeSettings;
using wf::emeraldwf::emerald_decorator_t;
using wf::emeraldwf::decor_crop_t;
using wf::emeraldwf::maximized_crop;
using wf::emeraldwf::maximized_crop_side;

constexpr uint32_t TILED_ALL = 0x1 | 0x2 | 0x4 | 0x8; // WLR_EDGE_* all four

class MarginsTest : public ::testing::Test
{
  protected:
    // Baseline theme geometry: emerald defaults (spaces 6/6/4/6, extra 0).
    MarginsInput base() const
    {
        MarginsInput in;
        in.left_space   = 6;
        in.right_space  = 6;
        in.top_space    = 4;
        in.bottom_space = 6;
        in.titlebar_height = 17;
        in.maximized_borders = true;
        return in;
    }
};

TEST_F(MarginsTest, NormalWindowed)
{
    auto m = emerald_decorator_t::margins_for(base());
    EXPECT_DOUBLE_EQ(m.left, 6.0);
    EXPECT_DOUBLE_EQ(m.right, 6.0);
    EXPECT_DOUBLE_EQ(m.bottom, 6.0);
    EXPECT_DOUBLE_EQ(m.top, 21.0);  // top_space 4 + titlebar 17
}

TEST_F(MarginsTest, Fullscreen)
{
    auto in = base();
    in.fullscreen = true;
    auto m = emerald_decorator_t::margins_for(in);
    EXPECT_DOUBLE_EQ(m.left, 0.0);
    EXPECT_DOUBLE_EQ(m.right, 0.0);
    EXPECT_DOUBLE_EQ(m.bottom, 0.0);
    EXPECT_DOUBLE_EQ(m.top, 0.0);
}

TEST_F(MarginsTest, Csd)
{
    auto in = base();
    in.csd = true;
    auto m = emerald_decorator_t::margins_for(in);
    EXPECT_DOUBLE_EQ(m.left, 0.0);
    EXPECT_DOUBLE_EQ(m.right, 0.0);
    EXPECT_DOUBLE_EQ(m.bottom, 0.0);
    EXPECT_DOUBLE_EQ(m.top, 0.0);
}

TEST_F(MarginsTest, MaximizedBordersOn)
{
    auto in = base();
    in.tiled_edges = TILED_ALL;
    in.maximized_borders = true;
    auto m = emerald_decorator_t::margins_for(in);
    EXPECT_DOUBLE_EQ(m.left, 6.0);
    EXPECT_DOUBLE_EQ(m.right, 6.0);
    EXPECT_DOUBLE_EQ(m.bottom, 6.0);
    EXPECT_DOUBLE_EQ(m.top, 21.0);  // top_space 4 + titlebar 17
}

TEST_F(MarginsTest, MaximizedBordersOff)
{
    // Finding 19: borders off drops border + extra, keeps the shadow spread.
    // base() has win_extents 0 → the whole side space is shadow spread.
    auto in = base();
    in.tiled_edges = TILED_ALL;
    in.maximized_borders = false;
    auto m = emerald_decorator_t::margins_for(in);
    EXPECT_DOUBLE_EQ(m.left, 6.0);   // shadow spread only
    EXPECT_DOUBLE_EQ(m.right, 6.0);
    EXPECT_DOUBLE_EQ(m.bottom, 6.0);
    EXPECT_DOUBLE_EQ(m.top, 21.0);   // top shadow spread 4 + titlebar 17
}

// Finding 19: maximized windows hug the screen edges. Each side's margin
// splits into shadow spread (space − ext − extra) and border (ext + extra);
// the two plugin options drop those parts independently.
TEST_F(MarginsTest, MaximizedCropMatrix)
{
    auto in = base();
    in.tiled_edges = TILED_ALL;
    // base(): left/right/bottom space 6, top 4, extra 0, titlebar 17.
    // Borders: 2 on the sides, 4 on top (= top_space → no shadow spread top).
    in.win_ext_left   = 2;
    in.win_ext_right  = 2;
    in.win_ext_bottom = 2;
    in.win_ext_top    = 4;
    // shadow spreads: L/R/B = 6 − 2 − 0 = 4; top = 4 − 4 − 0 = 0.

    in.maximized_borders = true;  in.maximized_shadows = true;
    auto m = emerald_decorator_t::margins_for(in);
    // Both on → full extents (default, regression).
    EXPECT_DOUBLE_EQ(m.left, 6.0);
    EXPECT_DOUBLE_EQ(m.right, 6.0);
    EXPECT_DOUBLE_EQ(m.bottom, 6.0);
    EXPECT_DOUBLE_EQ(m.top, 21.0);

    in.maximized_borders = false; in.maximized_shadows = true;
    m = emerald_decorator_t::margins_for(in);
    // Borders dropped → shadow spread only.
    EXPECT_DOUBLE_EQ(m.left, 4.0);
    EXPECT_DOUBLE_EQ(m.right, 4.0);
    EXPECT_DOUBLE_EQ(m.bottom, 4.0);
    EXPECT_DOUBLE_EQ(m.top, 17.0);  // 0 shadow + 17 titlebar

    in.maximized_borders = true;  in.maximized_shadows = false;
    m = emerald_decorator_t::margins_for(in);
    // Shadows dropped → border only; the window fills the screen below the
    // border + titlebar strip (no gaps at the edges).
    EXPECT_DOUBLE_EQ(m.left, 2.0);
    EXPECT_DOUBLE_EQ(m.right, 2.0);
    EXPECT_DOUBLE_EQ(m.bottom, 2.0);
    EXPECT_DOUBLE_EQ(m.top, 21.0);  // 4 border + 17 titlebar

    in.maximized_borders = false; in.maximized_shadows = false;
    m = emerald_decorator_t::margins_for(in);
    // Both dropped → titlebar strip only, zero gaps (use_decoration_cropping).
    EXPECT_DOUBLE_EQ(m.left, 0.0);
    EXPECT_DOUBLE_EQ(m.right, 0.0);
    EXPECT_DOUBLE_EQ(m.bottom, 0.0);
    EXPECT_DOUBLE_EQ(m.top, 17.0);
}

TEST_F(MarginsTest, ExtraExtentsAddedPerSide)
{
    auto in = base();
    in.extra_left   = 2;
    in.extra_right  = 3;
    in.extra_top    = 1;
    in.extra_bottom = 4;
    auto m = emerald_decorator_t::margins_for(in);
    // D8: thickness = *_space + extra_extents.X per side;
    // top = top_space + extra_top + titlebar.
    //   left   = 6 + 2 = 8
    //   right  = 6 + 3 = 9
    //   bottom = 6 + 4 = 10
    //   top    = 4 + 1 + 17 = 22
    EXPECT_DOUBLE_EQ(m.left, 8.0);
    EXPECT_DOUBLE_EQ(m.right, 9.0);
    EXPECT_DOUBLE_EQ(m.bottom, 10.0);
    EXPECT_DOUBLE_EQ(m.top, 22.0);
}

TEST_F(MarginsTest, MaximizedPartiallyTiledKeepsBorders)
{
    // Only two edges tiled → not "maximized" per D8 (tiled_edges != ALL).
    auto in = base();
    in.tiled_edges = 0x1 | 0x4; // TOP | LEFT
    auto m = emerald_decorator_t::margins_for(in);
    EXPECT_DOUBLE_EQ(m.left, 6.0);
    EXPECT_DOUBLE_EQ(m.top, 21.0);  // top_space 4 + titlebar 17
}

TEST_F(MarginsTest, FullscreenBeatsCsdAndMaximized)
{
    auto in = base();
    in.csd = true;
    in.fullscreen = true;
    in.tiled_edges = TILED_ALL;
    auto m = emerald_decorator_t::margins_for(in);
    EXPECT_DOUBLE_EQ(m.left, 0.0);
    EXPECT_DOUBLE_EQ(m.right, 0.0);
    EXPECT_DOUBLE_EQ(m.bottom, 0.0);
    EXPECT_DOUBLE_EQ(m.top, 0.0);
}

// ── part 06 finding 14: live reload must swap the decorator's theme ────────
//
// The engine is a process-wide global (e_draw) whose per-ws state is
// `ws->engine_ws`. If a live engine/theme reload only marked the node
// dirty while the decorator kept rendering its STALE window_settings,
// the new engine's draw code would read a foreign engine_ws struct —
// the root of the "crash after ~5 engine switches" report (and the
// reason theme-only edits never reached existing windows).
TEST_F(MarginsTest, SetThemeSwapsThemeRef)
{
    auto t1 = std::make_shared<ThemeSettings>();
    emerald_decorator_t deco(nullptr, t1);
    EXPECT_EQ(deco.theme_ref().get(), t1.get());

    auto t2 = std::make_shared<ThemeSettings>();
    deco.set_theme(t2);
    EXPECT_EQ(deco.theme_ref().get(), t2.get());
}

// ── part 07 finding 45: use_decoration_cropping forces the strip crop ──────
//
// emeraldwf's maximized crop is owned by the WCM maximized_borders /
// maximized_shadows options. Emerald's `use_decoration_cropping` (the
// theme manager's Emerald Settings tab; upstream main.c:660-666) must
// override both — a maximized window keeps only the titlebar strip —
// and while it is off the WCM options keep owning the crop.
TEST_F(MarginsTest, DecorationCroppingForcesTitlebarStrip)
{
    emerald_decorator_t deco(nullptr, std::make_shared<ThemeSettings>());

    deco.set_maximized_borders(true);
    deco.set_maximized_shadows(true);
    EXPECT_TRUE(deco.maximized_borders());
    EXPECT_TRUE(deco.maximized_shadows());

    auto t = std::make_shared<ThemeSettings>();
    t->ws()->use_decoration_cropping = true;
    deco.set_theme(t);
    EXPECT_FALSE(deco.maximized_borders());
    EXPECT_FALSE(deco.maximized_shadows());

    // The override is read-time: the WCM values are untouched, so turning
    // the setting back off restores them.
    t->ws()->use_decoration_cropping = false;
    EXPECT_TRUE(deco.maximized_borders());
    EXPECT_TRUE(deco.maximized_shadows());
}

// ── part 06 finding 16: safe detach when the view is dying ─────────────────
//
// When a toplevel dies, the order in which the view's scene nodes
// (surface root) and its data map (the decorator) are destroyed is not
// under our control. If the scene side dies first, detach_node() runs
// with node->parent() pointing at an already-destroyed surface root;
// remove_child() on a dead parent is a use-after-free. The predicate
// must only permit the removal when the parent is exactly the live
// surface root (kept alive by the decorator's sroot shared_ptr).
TEST_F(MarginsTest, DetachOnlyRemovesFromLiveSurfaceRoot)
{
    // The predicate is a pure pointer compare, so dummy bytes suffice.
    // Review F2 (-Wall): the second dummy sroot alias was unused — dropped.
    char a, b;
    auto pa = reinterpret_cast<wf::scene::node_t *>(&a);
    auto pb = reinterpret_cast<wf::scene::node_t *>(&b);
    auto sa = reinterpret_cast<wf::scene::floating_inner_node_t *>(&a);

    EXPECT_TRUE(emerald_decorator_t::detach_should_remove_child(pa, sa));
    EXPECT_FALSE(emerald_decorator_t::detach_should_remove_child(pb, sa))
        << "a dead/reparented parent must never be remove_child()ed";
    EXPECT_FALSE(emerald_decorator_t::detach_should_remove_child(pa, nullptr))
        << "without a captured sroot, removal must be refused";
}

// The liveness gate: a decorator whose view is gone (null here — the
// headless equivalent of a dead view) must report no view, so the
// emerald node's render/hit-test bail out instead of dereferencing a
// dangling observer_ptr (the segfault in emerald_node_t::render).
TEST_F(MarginsTest, ViewForOffsetsIsNullForDeadView)
{
    emerald_decorator_t deco(nullptr, nullptr);
    EXPECT_EQ(deco.view_for_offsets(), nullptr);
}

// ── Round 5: the top margin reserves the LIVE theme's titlebar height ──────
//
// The engine draws the SSD titlebar at ws->titlebar_height, so the top margin
// must reserve top_space + ws->titlebar_height. If the margin used a stale
// cached member (the default 17) while the theme draws 26, the drawn titlebar
// overflows the reserved space and overlaps the client — the "titlebar cut
// from the bottom" bug on scaled outputs. make_margins_input() must read the
// LIVE theme's titlebar_height, not the member (which is stale whenever the
// theme loads/reloads after the decorator is constructed).
TEST_F(MarginsTest, TopMarginUsesLiveThemeTitlebar)
{
    auto theme = std::make_shared<ThemeSettings>();
    theme->load("ignored.ini", "ignored");  // stub: valid_=true
    auto *ws = theme->ws();
    ASSERT_NE(ws, nullptr);
    ws->top_space       = 10;  // known, distinct values
    ws->titlebar_height = 30;  // ≠ the member default (17)

    // view=nullptr → is_csd() false (SSD). The constructor copies the member
    // from ws (30); then corrupt it to the stale default (17) to prove the
    // margin reads the live theme, not the member.
    emerald_decorator_t deco(nullptr, theme);
    deco.set_titlebar_height(17);

    wf::toplevel_state_t state{};  // normal windowed
    auto in = deco.make_margins_input(state);
    EXPECT_EQ(in.titlebar_height, 30)
        << "top margin must reserve the LIVE theme titlebar (30), not the "
           "stale member (17)";
    EXPECT_EQ(in.top_space, 10);

    // End-to-end: top margin = top_space + titlebar = 40.
    auto m = emerald_decorator_t::margins_for(in);
    EXPECT_DOUBLE_EQ(m.top, 40.0);
}

// ── Finding 19b: the shared crop decomposition ─────────────────────────────
//
// maximized_crop_side() / maximized_crop() (emerald-types.hpp) are the SINGLE
// source of truth for how much of each side is cropped when maximized. Both
// the margin math (margins_for) and the input/render offset
// (to_local_full_extent / render translate) must agree; a divergence between
// the two is exactly the "titlebar inert on maximized windows" bug.
namespace
{
struct crop_ws_t
{
    window_settings ws;
    crop_ws_t()
    {
        std::memset(&ws, 0, sizeof(ws));
        // A theme with non-zero shadow spread on every side so all four
        // flag combinations are distinguishable.
        //   left   : space 17 = ext 4 + shadow 11 + extra 2
        //   right  : space 16 = ext 5 + shadow 10 + extra 1
        //   top    : space 14 = ext 4 + shadow 10 + extra 0
        //   bottom : space 15 = ext 5 + shadow 7 + extra 3
        ws.win_extents.left = 4;   ws.extra_extents.left = 2;   ws.left_space = 17;
        ws.win_extents.right = 5;  ws.extra_extents.right = 1;  ws.right_space = 16;
        ws.win_extents.top = 4;    ws.extra_extents.top = 0;    ws.top_space = 14;
        ws.win_extents.bottom = 5; ws.extra_extents.bottom = 3; ws.bottom_space = 15;
    }
};
} // namespace

TEST(MarginsCrop, DecompositionPerSide)
{
    // crop = (shadows off ? shadow spread : 0) + (borders off ? border part : 0)
    // left (17,4,2): margin 19 = shadow 11 + border 8
    EXPECT_EQ(maximized_crop_side(17, 4, 2, true, true), 0);
    EXPECT_EQ(maximized_crop_side(17, 4, 2, false, true), 8);  // borders off
    EXPECT_EQ(maximized_crop_side(17, 4, 2, true, false), 11); // shadows off
    EXPECT_EQ(maximized_crop_side(17, 4, 2, false, false), 19);// both off
}

TEST(MarginsCrop, BothOffCropsTheWholeSide)
{
    // With both options off, crop == space + extra (the full side extent),
    // so the margin on that side is 0 — the window hugs the screen edge.
    crop_ws_t c;
    auto crop = maximized_crop(c.ws, false, false);
    EXPECT_EQ(crop.left, c.ws.left_space + c.ws.extra_extents.left);
    EXPECT_EQ(crop.right, c.ws.right_space + c.ws.extra_extents.right);
    EXPECT_EQ(crop.top, c.ws.top_space + c.ws.extra_extents.top);
    EXPECT_EQ(crop.bottom, c.ws.bottom_space + c.ws.extra_extents.bottom);
}

TEST(MarginsCrop, MarginPlusCropIsTheFullExtent)
{
    // margin = (space + extra) - crop, for every flag combination — so the
    // margin and the render/input offset are guaranteed to sum back to the
    // original full-extent side (no drift between the two consumers).
    crop_ws_t c;
    const bool combos[4][2] = {{true, true}, {false, true},
                               {true, false}, {false, false}};
    for (auto &cb : combos)
    {
        bool borders = cb[0], shadows = cb[1];
        auto crop = maximized_crop(c.ws, borders, shadows);

        MarginsInput in;
        in.left_space = c.ws.left_space;
        in.right_space = c.ws.right_space;
        in.top_space = c.ws.top_space;
        in.bottom_space = c.ws.bottom_space;
        in.extra_left = c.ws.extra_extents.left;
        in.extra_right = c.ws.extra_extents.right;
        in.extra_top = c.ws.extra_extents.top;
        in.extra_bottom = c.ws.extra_extents.bottom;
        in.win_ext_left = c.ws.win_extents.left;
        in.win_ext_right = c.ws.win_extents.right;
        in.win_ext_top = c.ws.win_extents.top;
        in.win_ext_bottom = c.ws.win_extents.bottom;
        in.titlebar_height = 17;
        in.tiled_edges = TILED_ALL;
        in.maximized_borders = borders;
        in.maximized_shadows = shadows;

        auto m = emerald_decorator_t::margins_for(in);
        EXPECT_DOUBLE_EQ(m.left,
            c.ws.left_space + c.ws.extra_extents.left - crop.left);
        EXPECT_DOUBLE_EQ(m.right,
            c.ws.right_space + c.ws.extra_extents.right - crop.right);
        EXPECT_DOUBLE_EQ(m.bottom,
            c.ws.bottom_space + c.ws.extra_extents.bottom - crop.bottom);
        EXPECT_DOUBLE_EQ(m.top,
            c.ws.top_space + c.ws.extra_extents.top - crop.top + 17.0);
    }
}

} // namespace
