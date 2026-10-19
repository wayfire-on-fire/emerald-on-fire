// test_shade.cpp — headless gtest coverage of Part 05 (shade + shadows).
//
// Exercises:
// 1. margin steadiness: the SSD margins are UNCHANGED across the whole
// shade progression — the bottom margin stays pinned to the theme
// value (bottom_space + extra_bottom). The visual roll-up is done by
// the shade transformer; the emerald frame texture is drawn from the
// logical margins, so a moving bottom margin would desync the texture
// layout and put client rows into the shaded bottom strip.
// 2. left/right/top margins unchanged by shade (top stays 21).
// 3. `ShadeState::init_animation` direction bookkeeping: after shading,
// `get_direction()` true; toggle back → false (state machine tested
// without a GL context).
// 4. shadow custom data: `shadow_margins_for(client_h, thickness,
// titlebar, progress)` matches the formula (pure computation — a live
// view is not required for headless tests).
#include <gtest/gtest.h>

#include <cmath>

#include "emerald-decorator.hpp"
#include "emerald-shadow-margins.hpp"
#include "emerald-shade.hpp"

namespace
{
using wf::emeraldwf::MarginsInput;
using wf::emeraldwf::emerald_decorator_t;
using wf::emeraldwf::ShadeState;
using wf::emeraldwf::shadow_margins_for;

constexpr double EPS = 1e-9;

class ShadeTest : public ::testing::Test
{
  protected:
    // Baseline: emerald defaults (win_extents 6/6/4/6, extra 0), titlebar 17.
    MarginsInput base() const
    {
        MarginsInput in;
        in.left_space   = 6;
        in.right_space  = 6;
        in.top_space    = 4;
        in.bottom_space = 6;
        in.titlebar_height   = 17;
        in.maximized_borders = true;
        return in;
    }
};

// ── 1. Margin steadiness ───────────────────────────────────────────────────
// The bottom margin must stay EXACTLY the theme's unshaded value
// (bottom_space + extra_bottom) across the whole shade progression — the
// visual roll-up is the shade transformer's job, and the emerald frame
// texture is drawn from the LOGICAL margins, so a moving bottom margin
// desyncs the texture layout (client rows land in the shaded bottom strip —
// user report "contents showing under the titlebar"). margins_for() no
// longer takes any progression input; the bottom is structurally steady.
TEST_F(ShadeTest, BottomMarginSteadyUnderShade)
{
    // bottom = bottom_space + extra_bottom = 6 + 0, for every "progression"
    // the transformer may report (the margins no longer read it at all).
    for (double p : {0.0, 0.25, 0.5, 0.75, 1.0})
    {
        auto in = base();
        (void)p;
        auto m = emerald_decorator_t::margins_for(in);
        EXPECT_DOUBLE_EQ(m.bottom, 6.0) <<
            "bottom must stay the theme's unshaded value at progress " <<
            p << " (the collapse formula was removed)";
    }
}

// ── 2. left/right/top unchanged by shade ───────────────────────────────────
TEST_F(ShadeTest, LeftRightTopUnchangedByShade)
{
    auto in = base();
    auto m  = emerald_decorator_t::margins_for(in);
    EXPECT_DOUBLE_EQ(m.left, 6.0);
    EXPECT_DOUBLE_EQ(m.right, 6.0);
    EXPECT_DOUBLE_EQ(m.top, 21.0); // top_space 4 + titlebar 17
}

// ── 3. ShadeState direction bookkeeping ────────────────────────────────────
// The state machine is tested without a live view / GL context. The
// animation's internal clock is driven by wayfire core (not by our code),
// so we only verify the direction bookkeeping: after shading,
// `get_shade_direction()` is true; after toggling back, it's false.
TEST_F(ShadeTest, ShadeStateDirectionBookkeeping)
{
    ShadeState st;

    // Initial state: unshaded.
    EXPECT_FALSE(st.get_shade_direction());

    // Shade.
    st.init_animation(true);
    EXPECT_TRUE(st.get_shade_direction());

    // Toggle back (unshade).
    st.init_animation(false);
    EXPECT_FALSE(st.get_shade_direction());

    // Toggle again (shade).
    st.init_animation(true);
    EXPECT_TRUE(st.get_shade_direction());

    // Re-init in the same direction: no-op (direction stays the same).
    st.init_animation(true);
    EXPECT_TRUE(st.get_shade_direction());
}

// ── 4. Shadow custom data (pure computation) ──────────────────────────────
// `shadow_margins_for(client_h, thickness, titlebar, progress)` must match
// the formula: left = right = top = thickness; bottom = thickness +
// (client_h - thickness - titlebar) * progress.
TEST_F(ShadeTest, ShadowMarginsPureComputation)
{
    const int client_h  = 300;
    const int thickness = 6;
    const int titlebar  = 17;

    // p = 0 → all four = thickness.
    {
        auto m = shadow_margins_for(client_h, thickness, titlebar, 0.0);
        EXPECT_DOUBLE_EQ(m.left, 6.0);
        EXPECT_DOUBLE_EQ(m.right, 6.0);
        EXPECT_DOUBLE_EQ(m.top, 6.0);
        EXPECT_DOUBLE_EQ(m.bottom, 6.0);
    }

    // p = 1 → bottom = thickness + (client_h - thickness - titlebar).
    {
        auto m = shadow_margins_for(client_h, thickness, titlebar, 1.0);
        EXPECT_DOUBLE_EQ(m.left, 6.0);
        EXPECT_DOUBLE_EQ(m.right, 6.0);
        EXPECT_DOUBLE_EQ(m.top, 6.0);
        EXPECT_DOUBLE_EQ(m.bottom, 6.0 + (300 - 6 - 17));
    }

    // p = 0.5 → bottom = thickness + (client_h - thickness - titlebar) * 0.5.
    {
        auto m = shadow_margins_for(client_h, thickness, titlebar, 0.5);
        EXPECT_DOUBLE_EQ(m.bottom, 6.0 + (300 - 6 - 17) * 0.5);
    }

    // CSD: thickness = 0, titlebar = csd_titlebar_height (32).
    {
        auto m = shadow_margins_for(client_h, 0, 32, 0.0);
        EXPECT_DOUBLE_EQ(m.left, 0.0);
        EXPECT_DOUBLE_EQ(m.right, 0.0);
        EXPECT_DOUBLE_EQ(m.top, 0.0);
        EXPECT_DOUBLE_EQ(m.bottom, 0.0);
    }
}

// ── 5. Shadow margins for a maximized window ──────────────────────────────
TEST_F(ShadeTest, ShadowMarginsMaximized)
{
    const int client_h  = 300;
    const int thickness = 6;
    const int titlebar  = 17;

    auto m = shadow_margins_for(client_h, thickness, titlebar, 0.0);
    EXPECT_DOUBLE_EQ(m.left, 6.0);
    EXPECT_DOUBLE_EQ(m.bottom, 6.0);
}

// ── 6. part 06 finding 17: scroll direction + settling ────────────────────
//
// scroll_shade() maps the wheel DELTA to a FIXED shade direction
// (wheel up → shade, wheel down → unshade) and hands it to
// ShadeState::init_animation(). init_animation() is a no-op when the
// requested direction already equals last_direction, so a SLOW scroll
// (many same-direction ticks) settles: the window animates to the target
// and stays, instead of re-triggering / flipping on every tick (the old
// code did `direction = !tr->get_direction()` — a toggle — so every tick
// reversed the animation and the window shaded "nonstop").
//
// The mapping itself (delta → to_shade) is trivial, so we exercise the
// settling property through ShadeState the way the fixed scroll_shade
// drives it, and assert the old toggle behavior would have flipped it.
TEST_F(ShadeTest, ScrollShadeDirectionMapping)
{
    // Wheel up (negative delta) shades; wheel down (positive) unshades.
    // This tests the PRODUCTION mapping (emerald-decorator.hpp, shared by
    // the SSD decorator path and the plugin's CSD branch — finding 18),
    // not a local copy.
    using wf::emeraldwf::scroll_shades;
    EXPECT_TRUE(scroll_shades(-10.0));
    EXPECT_TRUE(scroll_shades(-0.01));
    EXPECT_FALSE(scroll_shades(+10.0));
    EXPECT_FALSE(scroll_shades(+0.01));
}

TEST_F(ShadeTest, SlowScrollUpSettlesAndDoesNotOscillate)
{
    ShadeState st;

    // A slow "scroll up" gesture = several same-direction ticks.
    // The fixed code drives init_animation() with the SAME direction each
    // tick; after the first tick the rest are no-ops → direction stays
    // shaded and the animation does not flip.
    for (int i = 0; i < 8; i++)
    {
        st.init_animation(wf::emeraldwf::scroll_shades(-5.0));
    }

    EXPECT_TRUE(st.get_shade_direction()) <<
        "repeated wheel-up ticks must keep the window shaded (settle), "
        "not flip it back";

    // Contrast: the pre-fix TOGGLE logic would flip on every tick.
    // Demonstrate that toggling the same 8 ticks lands UNshaded (even
    // count) — i.e. the old code did not settle. This documents WHY the
    // direction-based fix is required.
    ShadeState old;
    for (int i = 0; i < 8; i++)
    {
        old.init_animation(!old.get_shade_direction());
    }

    EXPECT_FALSE(old.get_shade_direction()) <<
        "the old toggle logic oscillates; even tick count ends unshaded";
}

// ── Finding 22: the shade_duration option must drive the animation ─────────
//
// Regression: emeraldwf/shade_duration was stored on the decorator but
// never connected to the ShadeState duration, so every shade/unshade
// completed instantly (user report: "it shades instantly"). A ShadeState
// constructed with a non-zero duration option must keep the transition in
// flight on the first frame.
TEST_F(ShadeTest, ShadeStateRespectsDurationOption)
{
    // A test-local smoothing lambda: the headless test binary interposes an
    // empty local `smoothing::linear` over libwf-config's, so the extern
    // wayfire smooth functions are not a reliable fixture here.
    auto linear = [] (double x) { return x; };
    auto dur = std::make_shared<wf::config::option_t<wf::animation_description_t>>(
        "emeraldwf/shade_duration",
        wf::animation_description_t{5000,
            wf::animation::smoothing::smooth_function(linear),
            "linear"});
    ShadeState st(dur);
    st.init_animation(true);
    EXPECT_TRUE(st.running()) <<
        "a 5000 ms animation must still be running after start";
    EXPECT_LT(st.get_progression(), 1.0) <<
        "progression must not jump to the end value on the first frame — "
        "with the option unwired (the bug) the transition completed "
        "instantly and progression was already 1.0";
}

// ── Finding 23: enable_shade reaches the decorator's input paths ────────────
//
// Regression: unchecking emeraldwf/enable_shade in WCM did nothing to the
// titlebar-scroll / click-action shade paths — the plugin checked the
// option only for the IPC toggle and the global wheel binding. The
// decorator now mirrors the option (published by the plugin at decoration
// time and on every option change), and the input entry points
// (emerald-decorator-shade.cpp toggle_shade()/scroll_shade()) consult it
// before touching the view. This pins the plumbing the gate depends on.
TEST_F(ShadeTest, EnableShadeFlagDefaultsOnAndToggles)
{
    emerald_decorator_t deco(nullptr, nullptr);
    EXPECT_TRUE(deco.enable_shade()) << "shade is enabled by default";
    deco.set_enable_shade(false);
    EXPECT_FALSE(deco.enable_shade());
    deco.set_enable_shade(true);
    EXPECT_TRUE(deco.enable_shade());
}

// ── Margin steadiness (the successor of finding 26) ────────────────────────
// Finding 26 wired the live shade progression into the margin math (the
// bottom-margin collapse). The collapse has since been REMOVED: the
// transformer does the visual fold, and a moving bottom margin desynced the
// frame-texture layout (client rows in the shaded bottom strip). The
// invariant that replaced it: the margins no longer consult the shade
// transformer at all — get_margins() is identical with and without a shade
// animation in flight. Headless (null-view) decorator: the SSD defaults
// (win_extents 6/6/4/6, titlebar 17) give left = right = bottom = 6,
// top = 4 + 17.
TEST_F(ShadeTest, MarginsIndependentOfShadeState)
{
    emerald_decorator_t deco(nullptr, nullptr);
    wf::toplevel_state_t state;
    state.fullscreen  = false;
    state.tiled_edges = 0;
    auto m = deco.get_margins(state);
    EXPECT_DOUBLE_EQ(m.left, 6.0);
    EXPECT_DOUBLE_EQ(m.right, 6.0);
    EXPECT_DOUBLE_EQ(m.bottom, 6.0) <<
        "without a shade transformer the bottom margin must be the "
        "unshaded theme value (steady — the margins no longer read the "
        "transformer's progression)";
    EXPECT_DOUBLE_EQ(m.top, 21.0);
}

// ── Finding 52 (successor): the margins never disturb the client ──────────
//
// The old Finding-52 regression was that the p = 1 bottom margin implied a
// NEGATIVE client (frame height fed as client_h → client = titlebar − top
// < 0), which wayfire clamped and re-derived the frame from — per-cycle
// window growth + off-screen drift. The current design removes the whole
// class of bug: the bottom margin is STEADY (theme value, no progression
// input), so the client that fits inside the (fixed) frame is invariant
// through the animation — the app is never asked to resize, and there is
// nothing to clamp or re-derive.
TEST_F(ShadeTest, MarginsLeaveTheClientInvariant)
{
    // Realistic window: frame 400 tall, top margin 21 (space 4 + titlebar
    // 17), unshaded bottom margin 6 → unshaded client 373.
    const int frame_h = 400;
    const int top = 21, bottom0 = 6;

    auto m = emerald_decorator_t::margins_for(base());
    const int client_after = frame_h - (int)m.top - (int)m.bottom;
    EXPECT_EQ(client_after, frame_h - top - bottom0) <<
        "the margins must leave exactly the unshaded client inside the "
        "frame (steady bottom margin — the client is never collapsed)";
    EXPECT_GE(client_after, 0);
}

// The null-view guard: a decorator without a view (the headless equivalent
// of a dead view) takes the SSD default path and reports the steady theme
// margins (no view → no client, but the margins are still well-defined).
TEST_F(ShadeTest, MakeMarginsInputNullViewHasSteadyMargins)
{
    emerald_decorator_t deco(nullptr, nullptr);
    wf::toplevel_state_t state;
    state.fullscreen  = false;
    state.tiled_edges = 0;
    auto m = deco.get_margins(state);
    EXPECT_DOUBLE_EQ(m.bottom, 6.0);
    EXPECT_DOUBLE_EQ(m.top, 21.0);
}

// ── Finding 53 (successor): the steady client never dips below the hint ──
//
// The old Finding-53 regression was that the p = 1 bottom margin implied a
// collapsed client below the WM_NORMAL_HINTS minimum, which X11 clamped and
// wayfire re-derived the frame from — per-cycle growth. The current design
// removes the collapse entirely: the bottom margin is STEADY, so the client
// inside the frame is invariant through the animation and can never dip
// below the min-size hint (it never shrinks at all).
TEST_F(ShadeTest, SteadyMarginsNeverDipBelowMinClientHeight)
{
    // Frame 400, top 21, unshaded bottom 6 → client 373; min hint 100
    // (a terminal that refuses to shrink below ~5 rows).
    const int frame_h = 400, top = 21, bottom0 = 6;
    const int client = frame_h - top - bottom0;
    const int min_h  = 100;

    auto m = emerald_decorator_t::margins_for(base());
    const int client_after = frame_h - (int)m.top - (int)m.bottom;
    EXPECT_EQ(client_after, client) <<
        "the steady margins must leave the full unshaded client inside "
        "the frame (no collapse)";
    EXPECT_GE(client_after, min_h) <<
        "the client must stay at or above the min-size hint (it never "
        "shrinks, so it cannot dip below it)";
}

TEST_F(ShadeTest, ShadowMarginsRespectMinClientHeight)
{
    const int client = 373, thickness = 6, titlebar = 17, min_h = 100;

    auto m = shadow_margins_for(client, thickness, titlebar, 1.0, min_h);
    EXPECT_DOUBLE_EQ(m.bottom, 6.0 + (client - min_h)) <<
        "the shadow bottom uses the same cap as the frame margins";
    EXPECT_EQ(client - (int)(m.bottom - 6.0), min_h);

    // Without the hint the old formula stands.
    auto m2 = shadow_margins_for(client, thickness, titlebar, 1.0);
    EXPECT_DOUBLE_EQ(m2.bottom, 6.0 + (client - 6 - titlebar));
}

// ── Finding 54 (successor): the shade/unshade feedback loop is stable ──────
//
// The margin math is only correct if it composes stably with the toplevel's
// COMMIT protocol, which no single-function test covers:
//
// 1. emeraldwf sets pending margins for the current progression p
// (margins_for — STEADY: identical for every p).
// 2. wayfire configures the app to client = frame − top − bottom
// (xdg-toplevel configure_surface_with_state: floor, min 1).
// 3. The app commits the size it actually chose — it may REFUSE to
// shrink below its own minimum (X11: the WM_NORMAL_HINTS min;
// Wayland: the app's internal min — invisible on XDG, where
// get_min_size() reports {0,0}).
// 4. wayfire ADOPTS the committed size: frame = actual + margins
// (xdg-toplevel handle_surface_commit →
// adjust_geometry_for_gravity — the frame is re-derived from what
// the app really has).
//
// The old design collapsed the bottom margin with p, so step 2 requested a
// shrinking client; when it dipped below the app's floor, step 4 grew the
// frame and the next cycle requested an even deeper collapse — unbounded
// per-cycle drift (the original "windows grow every shade" bug). With the
// STEADY margin, step 2 always requests the unshaded client (frame − top
// − bottom, the size the app already has), which the app trivially honors:
// frame = client + margins is invariant on every tick, for every app. The
// observed-floor tracking (update_observed_min / effective_min_client_h)
// remains in production for the CSD shadow export (shadow_margins_for
// still collapses) and the settle-restore; this simulation asserts the
// SSD frame is invariant through N full shade/unshade cycles.

struct FakeApp
{
    int min_h; // the app's real floor (hint-visible on X11, invisible on XDG)

    int commit(int requested) const
    {
        return std::max(requested, min_h); // apps never commit BELOW their floor
    }
};

// One protocol tick, mirroring on_shade_progress() + the wayfire commit
// path. `margins_prev` is the margin of the last APPLIED transaction
// (what wayfire's current() carries); the frame it pairs with is `frame`.
// `last_progress` is the previous tick's smoothed progress (direction
// guard, see on_shade_progress()).
void feedback_tick(double p, int & frame, int & app_h,
    int & live_min, int & last_requested, double & last_progress,
    wf::decoration_margins_t & margins_prev,
    const MarginsInput & base, const FakeApp & app)
{
    // (1) emeraldwf: observe the app's floor from what wayfire last
    // adopted (current()), then compute this tick's margins. Only
    // while heading toward full shade (progress rising).
    const int adopted = frame - (int)margins_prev.top - (int)margins_prev.bottom;
    if (p >= last_progress)
    {
        live_min = wf::emeraldwf::update_observed_min(live_min, adopted, last_requested);
    }

    last_progress = p;

    // margins_for() is steady (no progression input): identical margins on
    // every tick of the animation.
    const auto m = emerald_decorator_t::margins_for(base);

    // (2) wayfire: configure the app to the client inside the frame
    // (xdg: floor, min 1 — configure_surface_with_state).
    const int requested =
        std::max(1, (int)std::floor((double)frame - m.top - m.bottom));
    last_requested = requested;

    // (3) the app commits its actual size.
    app_h = app.commit(requested);

    // (4) wayfire adopts: frame = actual client + margins (apply /
    // handle_surface_commit → adjust_geometry_for_gravity).
    frame = app_h + (int)m.top + (int)m.bottom;
    margins_prev = m;
}

// A full shade→unshade pair. Progression samples mirror wayfire's
// duration_t: sigmoid-smoothed frames, settling EXACTLY on 1.0 / 0.0
// (duration_t::impl::progress() returns the exact end value when ready).
//
// Mirrors the production bookkeeping (toggle_shade / on_shade_progress):
// - the observed floor (live_min) PERSISTS across cycles;
// - at the start of the shade the frame is remembered as the reference;
// - when the unshade settles (p = 0.0, margins = exactly unshaded) the
// pending frame is restored to the reference — so the requested client
// is exactly the unshaded client, which the app honors.
void run_shade_unshade_cycle(int & frame, int & app_h, int & live_min,
    int & last_requested, double & last_progress,
    wf::decoration_margins_t & margins_prev,
    const MarginsInput & base, const FakeApp & app)
{
    const double sigmoid_max = 1.0 + std::exp(-6.0); // wayfire's sigmoid()
    const int frames = 12;
    const int top_m  = base.top_space + base.extra_top + base.titlebar_height;
    const int bot_m  = base.bottom_space + base.extra_bottom;

    // Shade start, as toggle_shade() does it.
    const int ref_frame = frame;
    last_requested = frame - top_m - bot_m;

    // Shade: p 0 → 1 (the final frame is the exact settle, per duration_t).
    for (int k = 1; k <= frames; k++)
    {
        const double x = 1.0 * k / frames;
        const double p = (k == frames) ? 1.0 :
            sigmoid_max / (1.0 + std::exp(-12.0 * x + 6.0));
        feedback_tick(p, frame, app_h, live_min, last_requested, last_progress, margins_prev, base, app);
    }

    // Unshade: p 1 → 0 (final frame = exact 0.0).
    for (int k = 1; k <= frames; k++)
    {
        const double x = 1.0 * k / frames;
        const double p = (k == frames) ? 0.0 :
            1.0 - sigmoid_max / (1.0 + std::exp(-12.0 * x + 6.0));
        if (p < wf::emeraldwf::SHADE_SETTLE_EPSILON)
        {
            // on_shade_progress: settled unshaded → restore the reference
            // frame BEFORE this tick's configure (the margins are exactly
            // the unshaded ones, so this requests exactly the unshaded
            // client). Mirrors production's epsilon settle gate (review N6).
            frame = ref_frame;
        }

        feedback_tick(p, frame, app_h, live_min, last_requested, last_progress, margins_prev, base, app);
    }
}

TEST_F(ShadeTest, FeedbackLoopStable_XdgAppWithInvisibleMin)
{
    // The user's case: a native Wayland terminal (xfce4-terminal/GTK) —
    // XDG toplevel, get_min_size() = {0,0} (no size hints in the protocol),
    // but the app refuses to shrink below its own minimum.
    const MarginsInput cfg = base();
    const FakeApp app{100}; // the app's real (invisible) floor

    const int top     = base().top_space + base().titlebar_height;  // 21
    const int bot0    = base().bottom_space;                       // 6
    const int client0 = 373;
    int frame = client0 + top + bot0; // 400
    int app_h = client0;
    int live_min = 0, last_requested = client0;
    double last_progress = 0.0;
    wf::decoration_margins_t mprev{};
    mprev.top    = top;
    mprev.bottom = bot0;

    for (int i = 0; i < 20; i++)
    {
        const int before = frame;
        run_shade_unshade_cycle(frame, app_h, live_min, last_requested, last_progress, mprev, cfg, app);
        EXPECT_EQ(frame, before) <<
            "cycle " << i << ": frame height drifted " << before << " -> " <<
            frame << " (the app's committed size was adopted into the "
                 "frame without the cap honoring the app's floor)";
    }
}

TEST_F(ShadeTest, FeedbackLoopStable_X11AppWithVisibleHint)
{
    // X11/XWayland: the min hint is visible AND enforced. With the steady
    // margin the requested client is always the unshaded one (≥ the hint),
    // so the app's floor never bites — must stay stable.
    const MarginsInput cfg = base();
    const FakeApp app{100};

    const int top     = base().top_space + base().titlebar_height;
    const int bot0    = base().bottom_space;
    const int client0 = 373;
    int frame = client0 + top + bot0;
    int app_h = client0;
    int live_min = 0, last_requested = client0;
    double last_progress = 0.0;
    wf::decoration_margins_t mprev{};
    mprev.top    = top;
    mprev.bottom = bot0;

    for (int i = 0; i < 20; i++)
    {
        const int before = frame;
        run_shade_unshade_cycle(frame, app_h, live_min, last_requested, last_progress, mprev, cfg, app);
        EXPECT_EQ(frame, before) <<
            "cycle " << i << ": frame height drifted " << before << " -> " <<
            frame;
    }
}

TEST_F(ShadeTest, FeedbackLoopStable_IdealApp)
{
    // An app that honors every requested size (no floor above 1 px):
    // the steady margin must keep the frame invariant — pins that the
    // observation machinery does not disturb the ideal case.
    const MarginsInput cfg = base();
    const FakeApp app{1};

    const int top     = base().top_space + base().titlebar_height;
    const int bot0    = base().bottom_space;
    const int client0 = 373;
    int frame = client0 + top + bot0;
    int app_h = client0;
    int live_min = 0, last_requested = client0;
    double last_progress = 0.0;
    wf::decoration_margins_t mprev{};
    mprev.top    = top;
    mprev.bottom = bot0;

    for (int i = 0; i < 20; i++)
    {
        const int before = frame;
        run_shade_unshade_cycle(frame, app_h, live_min, last_requested, last_progress, mprev, cfg, app);
        EXPECT_EQ(frame, before) <<
            "cycle " << i << ": frame height drifted " << before << " -> " <<
            frame;
    }

    // And at rest the app is back at its original size.
    EXPECT_EQ(app_h, client0);
}

// The pre-fix behavior is now IMPOSSIBLE by construction: with the steady
// margin, every tick requests the unshaded client (frame − top − bottom) —
// the exact size the app already has — so the app's floor is never
// relevant to the SSD frame and the frame is invariant even WITHOUT the
// observed-floor tracking. This pins that guarantee: drive the same
// commit protocol with live_min frozen at 0 (no observation) and assert
// no per-cycle drift.
TEST_F(ShadeTest, FeedbackLoopStable_EvenWithoutObservedFloor)
{
    const MarginsInput cfg = base();
    const FakeApp app{100}; // the app refuses to shrink below 100

    const int top     = base().top_space + base().titlebar_height;
    const int bot0    = base().bottom_space;
    const int client0 = 373;
    int frame = client0 + top + bot0;
    int app_h = client0;
    int last_requested   = client0;
    double last_progress = 0.0;
    wf::decoration_margins_t mprev{};
    mprev.top    = top;
    mprev.bottom = bot0;

    // Same loop as feedback_tick, but live_min is never updated (no
    // observation at all).
    int live_min     = 0;
    const int frames = 12;
    const double sigmoid_max = 1.0 + std::exp(-6.0);
    auto tick = [&] (double p)
    {
        if (p >= last_progress)
        {
            live_min = wf::emeraldwf::update_observed_min(0 /*frozen*/, 0, 0);
        }

        last_progress = p;
        const auto m = emerald_decorator_t::margins_for(cfg);
        const int requested = std::max(1, (int)std::floor((double)frame - m.top - m.bottom));
        last_requested = requested;
        app_h = app.commit(requested);
        frame = app_h + (int)m.top + (int)m.bottom;
        mprev = m;
    };

    const int start_frame = frame;
    for (int k = 1; k <= frames; k++)
    {
        const double x = 1.0 * k / frames;
        tick(k == frames ? 1.0 : sigmoid_max / (1.0 + std::exp(-12.0 * x + 6.0)));
    }

    for (int k = 1; k <= frames; k++)
    {
        const double x = 1.0 * k / frames;
        tick(k == frames ? 0.0 : 1.0 - sigmoid_max / (1.0 + std::exp(-12.0 * x + 6.0)));
    }

    EXPECT_EQ(frame, start_frame) <<
        "with the steady bottom margin the requested client is always the "
        "unshaded one, so the frame must be invariant even without the "
        "observed-floor tracking (the old collapse+drift path is gone)";
}

// The floor decision itself (production pure function):
TEST_F(ShadeTest, UpdateObservedMinTakesTheLargerFloor)
{
    using wf::emeraldwf::update_observed_min;
    using wf::emeraldwf::effective_min_client_h;

    // App refused to shrink below 100 (adopted 100 > requested 60) → floor 100.
    EXPECT_EQ(update_observed_min(0, 100, 60), 100);
    // Hint already higher than the observation → keep the hint floor.
    EXPECT_EQ(update_observed_min(120, 100, 60), 120);
    // App honored the request (adopted == requested) → no new floor.
    EXPECT_EQ(update_observed_min(0, 80, 80), 0);
    // App GREW past the request: the function cannot tell the animation
    // direction, and adopted > requested is always the floor signature
    // (the app refused the requested size) — so it reports 90. In
    // practice this case does not arise on the unshade path: the app
    // follows the growing requests exactly (adopted == requested), so no
    // spurious floor is ever recorded while unshading.
    EXPECT_EQ(update_observed_min(0, 90, 80), 90);
    // Effective = max(hint, observed).
    EXPECT_EQ(effective_min_client_h(0, 100), 100);
    EXPECT_EQ(effective_min_client_h(120, 100), 120);
    EXPECT_EQ(effective_min_client_h(0, 0), 0);
}
} // namespace
