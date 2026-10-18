// test_render_node.cpp — Part 04: gtest suite for the scene node's pure
// invalidation predicate (headless, no wayfire instance, no GL).
//
// Per spec 04 "Tests": extract the dirty-flag invalidation predicate into a
// free function (`needs_rebuild`) and test it directly. `needs_rebuild` and
// its `node_cache_t` live in emerald-node-pure.hpp/.cpp (wayfire-free) so this
// test links without wayfire core symbols.
#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include <titlebar.h>

#include "emerald-node-pure.hpp"

using namespace wf::emeraldwf;

namespace
{

// Build a cache matching the given inputs.
node_cache_t make_cache(bool active, const std::string &title,
                        int w, int h, int hover)
{
    node_cache_t c;
    c.surface = reinterpret_cast<cairo_surface_t *>(0x1); // sentinel (never destroyed)
    c.active  = active;
    c.title   = title;
    c.width   = w;
    c.height  = h;
    c.hover_button = hover;
    return c;
}

} // namespace

// ── needs_rebuild (invalidation predicate) ──────────────────────────────────
TEST(RenderNodeTest, NoSurface_RequiresRebuild)
{
    node_cache_t cache; // surface == nullptr
    EXPECT_TRUE(needs_rebuild(cache, true, "title", 200, 150, 1.0, -1));
}

TEST(RenderNodeTest, Unchanged_NoRebuild)
{
    auto cache = make_cache(true, "title", 200, 150, -1);
    EXPECT_FALSE(needs_rebuild(cache, true, "title", 200, 150, 1.0, -1));
}

TEST(RenderNodeTest, ChangedActive_RequiresRebuild)
{
    auto cache = make_cache(true, "title", 200, 150, -1);
    EXPECT_TRUE(needs_rebuild(cache, false, "title", 200, 150, 1.0, -1));
}

TEST(RenderNodeTest, ChangedTitle_RequiresRebuild)
{
    auto cache = make_cache(true, "old title", 200, 150, -1);
    EXPECT_TRUE(needs_rebuild(cache, true, "new title", 200, 150, 1.0, -1));
}

TEST(RenderNodeTest, ChangedSize_RequiresRebuild)
{
    auto cache = make_cache(true, "title", 200, 150, -1);
    EXPECT_TRUE(needs_rebuild(cache, true, "title", 210, 160, 1.0, -1));
}

TEST(RenderNodeTest, ChangedHover_RequiresRebuild)
{
    auto cache = make_cache(true, "title", 200, 150, -1);
    EXPECT_TRUE(needs_rebuild(cache, true, "title", 200, 150, 1.0, 2));
}

TEST(RenderNodeTest, EmptyTitleChange_RequiresRebuild)
{
    auto cache = make_cache(false, "title", 200, 150, -1);
    EXPECT_TRUE(needs_rebuild(cache, false, "", 200, 150, 1.0, -1));
}

// The surface is rendered at physical resolution (logical * scale), so moving
// a window to a differently-scaled output (or changing the output scale) must
// force a re-render — the cached surface is the wrong physical size otherwise.
TEST(RenderNodeTest, ChangedScale_RequiresRebuild)
{
    auto cache = make_cache(true, "title", 200, 150, -1); // cached at scale 1.0
    cache.scale = 1.0;
    EXPECT_FALSE(needs_rebuild(cache, true, "title", 200, 150, 1.0, -1));
    EXPECT_TRUE(needs_rebuild(cache, true, "title", 200, 150, 2.0, -1));

    cache.scale = 2.0;
    EXPECT_TRUE(needs_rebuild(cache, true, "title", 200, 150, 1.0, -1));
    EXPECT_FALSE(needs_rebuild(cache, true, "title", 200, 150, 2.0, -1));
}

// ── frame_box_for (part 06 regression: "no decoration painted on first map") ──
//
// A toplevel's wm geometry is 0×0 while it is mapping (wayfire populates it
// after the mapping transaction). If the frame is sized from that 0×0 client,
// it collapses to just the border thickness — a degenerate sliver that looks
// like "nothing is painted". The fix is to recompute the frame once the real
// client size is known (the plugin's view_geometry_changed_signal handler).
// These tests pin the exact collapse and the recovery.

namespace
{
// Theme-derived margins for a normal (non-CSD, windowed) window.
// left/right = win_extents.x + extra.x; top = thickness + titlebar.
const frame_box_input_t MARGINS{
    /*client_x*/ 0, /*client_y*/ 0,
    /*client_w*/ 0, /*client_h*/ 0,
    /*m_left*/ 6, /*m_right*/ 6, /*m_bottom*/ 6, /*m_top*/ 21};
}

TEST(FrameBoxTest, ZeroClient_CollapsesToBorderSliver)
{
    // Mapping-time state: client is 0×0. The frame is only the borders.
    auto in = MARGINS;
    auto box = frame_box_for(in);
    // Width = 0 + 6 + 6 = 12, height = 0 + 21 + 6 = 27. This is the
    // degenerate sliver the bug produces — visible as "no decoration".
    EXPECT_EQ(box.w, 12);
    EXPECT_EQ(box.h, 27);
    EXPECT_EQ(box.x, -6);
    EXPECT_EQ(box.y, -21);
    // The frame has no client area (w == m_left + m_right exactly), which is
    // the signature of the bug: a real windowed frame is always bigger than
    // its borders.
    EXPECT_EQ(box.w, in.m_left + in.m_right);
    EXPECT_EQ(box.h, in.m_top + in.m_bottom);
}

TEST(FrameBoxTest, PopulatedClient_ProducesFullFrame)
{
    // After wayfire populates the geometry (e.g. 800×600 client), the same
    // margins yield a proper frame: client + margins on each axis.
    auto in = MARGINS;
    in.client_w = 800;
    in.client_h = 600;
    auto box = frame_box_for(in);
    EXPECT_EQ(box.w, 800 + 6 + 6);   // 812
    EXPECT_EQ(box.h, 600 + 21 + 6);  // 627
    EXPECT_EQ(box.x, -6);
    EXPECT_EQ(box.y, -21);
    // Sanity: the frame strictly contains the client (not just borders).
    EXPECT_GT(box.w, in.m_left + in.m_right);
    EXPECT_GT(box.h, in.m_top + in.m_bottom);
}

TEST(FrameBoxTest, FrameSizedSurface_ClientAtMarginOffset)
{
    // When the toplevel surface IS frame-sized (wayfire expanded the
    // configure geometry), the client sits at (m_left, m_top) inside it —
    // the frame box must then start at (0, 0), not (-m_left, -m_top).
    // This is the twin of PopulatedClient_ProducesFullFrame (client at
    // (0, 0), the collapsed-surface case after wayfire's resize() writes
    // client dims into pending.geometry without margins).
    auto in = MARGINS;
    in.client_x = in.m_left;
    in.client_y = in.m_top;
    in.client_w = 800;
    in.client_h = 600;
    auto box = frame_box_for(in);
    EXPECT_EQ(box.x, 0);
    EXPECT_EQ(box.y, 0);
    EXPECT_EQ(box.w, 800 + 6 + 6);
    EXPECT_EQ(box.h, 600 + 21 + 6);
}

TEST(FrameBoxTest, ReconfigureMatchesRealToplevelTransition)
{
    // Simulate the exact lifecycle: map (0×0) → geometry populated (800×600).
    // The bounding box MUST change; if it didn't, the node would keep
    // rendering the degenerate sliver. This is what the
    // view_geometry_changed_signal handler relies on.
    auto mapped = frame_box_for(MARGINS);                 // 0×0 client
    auto in = MARGINS;
    in.client_w = 800; in.client_h = 600;
    auto populated = frame_box_for(in);
    EXPECT_NE(mapped.w, populated.w);
    EXPECT_NE(mapped.h, populated.h);
    // Offset is margins-derived, so it is stable across the transition —
    // only the size changes.
    EXPECT_EQ(mapped.x, populated.x);
    EXPECT_EQ(mapped.y, populated.y);
}

// ── frame_box_from_pending (part 06 regression: SEGV on any window map) ─────
//
// node_bounding_box() must derive the frame from the toplevel's pending
// geometry/margins (the pair wayfire uses to size the surface) and must NOT
// query the surface-root node's bounding box: the emerald node is a child of
// that node, so the query recurses through it and overflows the stack on any
// window map. These tests pin the parent-free derivation.

TEST(FrameFromPendingTest, NormalFrame_MatchesClientPlusMargins)
{
    // Frame 812×627, margins left/right=6, top=21, bottom=6 → client 800×600.
    auto in = pending_box_input_t{812, 627, 6, 6, 6, 21};
    auto box = frame_box_from_pending(in);
    EXPECT_EQ(box.w, 812);
    EXPECT_EQ(box.h, 627);
    // Client at (0,0) in surface-root coords → frame origin is negative.
    EXPECT_EQ(box.x, -6);
    EXPECT_EQ(box.y, -21);
}

TEST(FrameFromPendingTest, ZeroFrame_CollapsesToBorderSliver)
{
    // Mapping-time state: pending geometry is 0×0 (the transaction hasn't
    // populated it). The client clamps to 0×0 → the frame is only borders.
    // This is the degenerate sliver; the geometry-changed handler recomputes
    // once the real size is known. It must NOT be negative or unbounded.
    auto in = pending_box_input_t{0, 0, 6, 6, 6, 21};
    auto box = frame_box_from_pending(in);
    EXPECT_EQ(box.w, 12); // 6 + 6
    EXPECT_EQ(box.h, 27); // 21 + 6
    EXPECT_EQ(box.x, -6);
    EXPECT_EQ(box.y, -21);
}

TEST(FrameFromPendingTest, FrameEqualMargins_ClonseToBordersOnly)
{
    // A frame exactly the size of its margins ⇒ client 0×0 (signature of a
    // collapsed client). The box must equal the margins, no more.
    auto in = pending_box_input_t{12, 27, 6, 6, 6, 21};
    auto box = frame_box_from_pending(in);
    EXPECT_EQ(box.w, in.m_left + in.m_right);
    EXPECT_EQ(box.h, in.m_top + in.m_bottom);
}

TEST(FrameFromPendingTest, ConsistentWithFrameBoxFor)
{
    // The pending derivation must agree with frame_box_for given the derived
    // client — i.e. it is exactly "shrink the frame to the client, then
    // frame_box_for with the client at (0,0)".
    int fw = 1234, fh = 567;
    int l = 9, r = 8, b = 7, t = 30;
    auto via_pending = frame_box_from_pending({fw, fh, l, r, b, t});
    int cw = fw - l - r, ch = fh - t - b;
    auto via_framebox = frame_box_for({0, 0, cw, ch, l, r, b, t});
    EXPECT_EQ(via_pending.x, via_framebox.x);
    EXPECT_EQ(via_pending.y, via_framebox.y);
    EXPECT_EQ(via_pending.w, via_framebox.w);
    EXPECT_EQ(via_pending.h, via_framebox.h);
}

// ── frame_local_for (part 06 regression: "no titlebar drag / no resize") ───
//
// Parent (surface-root) coords: client top-left at (0,0); the frame extends
// to NEGATIVE coords on top/left. Frame-local coords: frame top-left at
// (0,0). The conversion is local = parent - frame-origin, and the frame
// origin from frame_box_for is NEGATIVE {-m_left, -m_top}. A positive
// offset (the bug) shifted every input coordinate above/left of the frame:
// a press on the title bar landed above the frame, so in_titlebar() and
// resize_edges() both reported "nothing" and move/resize never started.
TEST(FrameLocalTest, ClientOrigin_IsFrameOriginPlusMargins)
{
    auto in = MARGINS;
    in.client_w = 800; in.client_h = 600;
    auto box = frame_box_for(in);
    // Parent (0,0) is the client's top-left → frame-local (m_left, m_top).
    auto local = frame_local_for(0, 0, box);
    EXPECT_EQ(local.x, 6);
    EXPECT_EQ(local.y, 21);
}

TEST(FrameLocalTest, FrameTopLeft_IsZero)
{
    auto in = MARGINS;
    in.client_w = 800; in.client_h = 600;
    auto box = frame_box_for(in);
    // The frame's own top-left in parent coords is the box origin (negative)
    // → frame-local (0,0).
    auto local = frame_local_for(box.x, box.y, box);
    EXPECT_EQ(local.x, 0);
    EXPECT_EQ(local.y, 0);
}

TEST(FrameLocalTest, TitlebarPress_LandsInsideFrame)
{
    auto in = MARGINS;
    in.client_w = 800; in.client_h = 600;
    auto box = frame_box_for(in);
    // A parent point in the title bar band: client_x 200, 10px below the
    // top border (title bar spans local y in [m_top - thickness, m_top)).
    // With m_top=21 (thickness 4 + titlebar 17) that is parent y = -3.
    auto local = frame_local_for(200, -3, box);
    EXPECT_EQ(local.x, 206);
    EXPECT_EQ(local.y, 18);
    // Must fall inside the frame (0..w, 0..h) — with the buggy positive
    // offset it came out at local y = -24, above the frame.
    EXPECT_GE(local.y, 0);
    EXPECT_LT(local.y, box.h);
}

// ── Titlebar double-click detection (finding 19) ──────────────────────────
//
// Regression: double-clicking the title bar did nothing — the port never
// synthesised the server-side double-click event. `is_double_click()` is the
// pure timing predicate the node uses to decide whether a BTN_LEFT titlebar
// press fires the configured double_click_action.
TEST(DoubleClickTest, FirstPressIsNotADoubleClick)
{
    double_click_state_t st;
    EXPECT_FALSE(is_double_click(st, 1000));
    // The first press is recorded for the next comparison.
    EXPECT_EQ(st.last_press_ms, 1000u);
}

TEST(DoubleClickTest, SecondPressWithinWindowIsADoubleClick)
{
    double_click_state_t st;
    is_double_click(st, 1000);
    EXPECT_TRUE(is_double_click(st, 1200)); // 200ms < 400ms default
}

TEST(DoubleClickTest, SecondPressAtExactWindowIsADoubleClick)
{
    double_click_state_t st;
    is_double_click(st, 1000);
    EXPECT_TRUE(is_double_click(st, 1400)); // exactly 400ms (<= boundary)
}

TEST(DoubleClickTest, SecondPressAfterWindowIsNotADoubleClick)
{
    double_click_state_t st;
    is_double_click(st, 1000);
    EXPECT_FALSE(is_double_click(st, 1401)); // 401ms > 400ms
    // The late press becomes the new baseline.
    EXPECT_EQ(st.last_press_ms, 1401u);
}

TEST(DoubleClickTest, TripleClickConsumesThePair)
{
    double_click_state_t st;
    is_double_click(st, 1000);
    EXPECT_TRUE(is_double_click(st, 1200));
    // After a detected double-click the state is reset, so the third press
    // must NOT re-trigger (it starts a fresh sequence).
    EXPECT_FALSE(is_double_click(st, 1400));
    EXPECT_EQ(st.last_press_ms, 1400u);
}

TEST(DoubleClickTest, ClockGoingBackwardsIsNotADoubleClick)
{
    double_click_state_t st;
    is_double_click(st, 1000);
    EXPECT_FALSE(is_double_click(st, 900));
    EXPECT_EQ(st.last_press_ms, 900u);
}

TEST(DoubleClickTest, CustomTimeoutRespected)
{
    double_click_state_t st;
    is_double_click(st, 1000, 100);
    EXPECT_TRUE(is_double_click(st, 1099, 100));
}

// ── Titlebar press/move state machine (finding 20) ─────────────────────────
//
// Regression: double-clicking the titlebar did nothing in a live session —
// the first press immediately issued move_request(), which grabs the
// pointer, so the double-click's second press never reached the node. The
// state machine defers the move: a press only ARMS it, motion commits it,
// a second press in the window cancels it and fires the action.
TEST(TitlebarInputTest, DoubleClickCancelsThePendingMove)
{
    titlebar_input_state_t st;
    EXPECT_EQ(on_titlebar_press(st, 100, 50, 1000),
              titlebar_press_result::ARM_MOVE);
    ASSERT_TRUE(st.pending_move);
    // The second press within the window fires the double-click and drops
    // the pending move — the node must NOT have committed a move gesture.
    EXPECT_EQ(on_titlebar_press(st, 100, 50, 1300),
              titlebar_press_result::DOUBLE_CLICK);
    EXPECT_FALSE(st.pending_move);
}

TEST(TitlebarInputTest, MotionCommitsTheMoveExactlyOnce)
{
    titlebar_input_state_t st;
    on_titlebar_press(st, 100, 50, 1000);
    EXPECT_FALSE(on_titlebar_motion(st, 102, 50, 4.0)); // 2px — below 4
    ASSERT_TRUE(st.pending_move);
    EXPECT_TRUE(on_titlebar_motion(st, 105, 50, 4.0));  // 5px — commit
    EXPECT_FALSE(st.pending_move);
    // Already committed — further motion must not re-commit.
    EXPECT_FALSE(on_titlebar_motion(st, 200, 50, 4.0));
}

TEST(TitlebarInputTest, DiagonalMotionCommits)
{
    titlebar_input_state_t st;
    on_titlebar_press(st, 100, 50, 1000);
    // 3px in x and 3px in y is ~4.24px > the 4px threshold.
    EXPECT_TRUE(on_titlebar_motion(st, 103, 53, 4.0));
}

TEST(TitlebarInputTest, NoMotionThenReleaseIsAClick)
{
    titlebar_input_state_t st;
    on_titlebar_press(st, 100, 50, 1000);
    EXPECT_FALSE(on_titlebar_motion(st, 101, 50, 4.0));
    on_button_release(st);
    EXPECT_FALSE(st.pending_move);
}

TEST(TitlebarInputTest, ReleaseDoesNotBreakTheDoubleClickPair)
{
    // Real double-clicks are press-release-press; the release in between
    // must not reset the timing.
    titlebar_input_state_t st;
    on_titlebar_press(st, 100, 50, 1000);
    on_button_release(st);
    EXPECT_EQ(on_titlebar_press(st, 100, 50, 1200),
              titlebar_press_result::DOUBLE_CLICK);
}

TEST(TitlebarInputTest, PressAfterWindowArmsAFreshMove)
{
    titlebar_input_state_t st;
    on_titlebar_press(st, 100, 50, 1000);
    EXPECT_EQ(on_titlebar_press(st, 100, 50, 1500), // 500ms > 400ms
              titlebar_press_result::ARM_MOVE);
}

TEST(TitlebarInputTest, PressAfterCommitStartsFresh)
{
    titlebar_input_state_t st;
    on_titlebar_press(st, 100, 50, 1000);
    on_titlebar_motion(st, 110, 50, 4.0); // commit the drag
    // A later press is a fresh sequence, not a double-click of the old one.
    EXPECT_EQ(on_titlebar_press(st, 110, 50, 2000),
              titlebar_press_result::ARM_MOVE);
}

TEST(TitlebarInputTest, ReleaseWithoutPressIsANoop)
{
    titlebar_input_state_t st;
    on_button_release(st);
    EXPECT_FALSE(st.pending_move);
    EXPECT_EQ(st.dbl.last_press_ms, 0u);
}

// ── Titlebar action → WM request mapping (finding 21) ─────────────────────
//
// Regression: the old raw switch in handle_click_action() had no `case 3`,
// so the configured SEND_TO_BACK action was silently swallowed (user
// report: "send to back doesn't work"). Every emerald DOUBLE_CLICK_* value
// must now map to a concrete request — this table is the single source of
// truth the node dispatches on.
TEST(TitlebarActionMapTest, AllEmeraldActionsMapToConcreteRequests)
{
    EXPECT_EQ(map_titlebar_action(DOUBLE_CLICK_SHADE),
              titlebar_action_request::TOGGLE_SHADE);
    EXPECT_EQ(map_titlebar_action(DOUBLE_CLICK_MAXIMIZE),
              titlebar_action_request::MAXIMIZE_TOGGLE);
    EXPECT_EQ(map_titlebar_action(DOUBLE_CLICK_MINIMIZE),
              titlebar_action_request::MINIMIZE);
    // The regression case: send-to-back must map to SEND_TO_BACK, not NONE.
    EXPECT_EQ(map_titlebar_action(DOUBLE_CLICK_SEND_TO_BACK),
              titlebar_action_request::SEND_TO_BACK);
    EXPECT_EQ(map_titlebar_action(DOUBLE_CLICK_CLOSE),
              titlebar_action_request::CLOSE);
}

TEST(TitlebarActionMapTest, NoneAndUnknownMapToNoRequest)
{
    EXPECT_EQ(map_titlebar_action(DOUBLE_CLICK_NONE),
              titlebar_action_request::NONE);
    EXPECT_EQ(map_titlebar_action(-1), titlebar_action_request::NONE);
    EXPECT_EQ(map_titlebar_action(99), titlebar_action_request::NONE);
}

// ── Finding 27: fullscreen render guard ─────────────────────────────────────
//
// Regression: render_frame() had no fullscreen guard — entering fullscreen
// sets margins to 0, so the node's bounding box equals the client box and
// the (re-rendered) emerald frame was composited OVER the fullscreen
// client, painting the emerald titlebar on top of the content (pixdecor
// hides its frame the same way). The guard is pure so it is pinned here.
TEST(FrameRenderGuardTest, FullscreenAndZeroSizeAreNotRendered)
{
    EXPECT_TRUE(frame_should_render(false, 400, 300));
    EXPECT_FALSE(frame_should_render(true, 400, 300))
        << "fullscreen windows must not draw the frame (it would land on "
           "the client)";
    EXPECT_FALSE(frame_should_render(false, 0, 300))
        << "zero-width frames are degenerate (no decoration to draw)";
    EXPECT_FALSE(frame_should_render(false, 400, 0));
    EXPECT_FALSE(frame_should_render(false, 0, 0));
}

// ── Frame texture pick (per-frame upload regression) ────────────────────────
//
// Regression: on a page with continuous client damage (video), the frame
// node is rendered at the client's framerate with an UNCHANGED cached
// surface. The old render path wrapped that cached cairo surface in a fresh
// wf::owned_texture_t on every pass — wlr_texture_from_pixels() re-creates
// the GL texture and re-copies the entire ARGB32 frame buffer to the GPU on
// EVERY frame (~tens of MB/s on a large 2×-scaled window): the "wayfire
// burns 25–45% of a core while a video page is open" bug.
//
// Contract: the texture is created ONCE per surface and reused for every
// subsequent render of that surface; only a NEW surface (rebuild) may
// trigger a (re)upload.
//
// Note: the GL side of this (wayfire's owned_texture_t / wlr_texture_from_
// pixels) is not testable headlessly; the pick is the pure decision point
// the node acts on, so it is pinned here exactly like needs_rebuild.

namespace
{
// A "rendered" cache entry: sentinel surface (never destroyed, as in
// make_cache) + the texture the node created from it on rebuild.
node_cache_t make_texture_cache()
{
    node_cache_t c;
    c.surface = reinterpret_cast<cairo_surface_t *>(0x1);
    c.texture = reinterpret_cast<void *>(0xABCDEF); // the cached GL texture (sentinel)
    c.active  = true;
    c.title   = "title";
    c.width   = 200;
    c.height  = 150;
    return c;
}
} // namespace

TEST(FrameTexturePickTest, CacheHit_ReusesTextureWithoutUpload)
{
    // THE regression: same surface as last render (nothing changed — the
    // cache-hit path on a continuously-damaged video page). The texture must
    // be reused as-is; must_create would mean wlr_texture_from_pixels() ran
    // again — a full CPU→GPU copy of the frame on a frame that needed
    // nothing.
    auto cache = make_texture_cache();
    auto pick = pick_frame_texture(cache, cache.surface);
    EXPECT_FALSE(pick.must_create)
        << "cache hit must not re-create the texture (per-frame full-buffer "
           "upload — the video-page CPU regression)";
    EXPECT_EQ(pick.texture, cache.texture)
        << "cache hit must hand back the EXISTING texture, not a new one";
}

TEST(FrameTexturePickTest, NewSurface_RequiresNewTexture)
{
    // Rebuild: the surface pointer changed (re-rendered frame). The old
    // texture derives from the old surface — it must be replaced.
    auto cache = make_texture_cache();
    // Opaque sentinel (not a real surface) — int->ptr is a reinterpret_cast.
    const cairo_surface_t *new_surface = reinterpret_cast<cairo_surface_t *>(0x2);
    auto pick = pick_frame_texture(cache, new_surface);
    EXPECT_TRUE(pick.must_create)
        << "a new surface has no texture yet — one must be created";
}

TEST(FrameTexturePickTest, NoSurface_NoTexture)
{
    // Nothing rendered (or destroyed) — nothing to blit, nothing to create.
    node_cache_t cache; // all null
    auto pick = pick_frame_texture(cache, nullptr);
    EXPECT_FALSE(pick.must_create);
    EXPECT_EQ(pick.texture, nullptr);
}

TEST(FrameTexturePickTest, SurfaceWithoutTexture_RequiresUpload)
{
    // Defensive: a surface with no texture recorded (e.g. texture creation
    // failed last time) must be re-created, not blitted as a dangling null.
    auto cache = make_texture_cache();
    cache.texture = nullptr;
    auto pick = pick_frame_texture(cache, cache.surface);
    EXPECT_TRUE(pick.must_create);
}

TEST(FrameTexturePickTest, StaleTextureAfterReplace_RequiresNewTexture)
{
    // After a rebuild the node updates BOTH surface and texture together;
    // this pins that the pick keys off the SURFACE pair, not the texture
    // alone — a texture pointer that happens to match an old cache entry is
    // no excuse to skip the upload.
    auto cache = make_texture_cache();
    // Opaque sentinel (not a real surface) — int->ptr is a reinterpret_cast.
    // (non-const: it is stored into cache.surface, a non-const member)
    cairo_surface_t *new_surface = reinterpret_cast<cairo_surface_t *>(0x2);
    // Caller-created texture for the new surface — a DIFFERENT one.
    void *new_texture = reinterpret_cast<void *>(0x123456);
    cache.surface = new_surface;
    cache.texture = new_texture;
    auto pick = pick_frame_texture(cache, new_surface);
    EXPECT_FALSE(pick.must_create);
    EXPECT_EQ(pick.texture, new_texture);
}
