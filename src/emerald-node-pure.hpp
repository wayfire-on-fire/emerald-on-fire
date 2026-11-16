// emerald-node-pure.hpp — Part 04: wayfire-free pure helpers for the scene
// node, extracted so the headless test (tests/test_render_node.cpp) can link
// them without wayfire core symbols (libwayfire is not a linkable shared lib).
#pragma once

#include <cstdint>
#include <map>
#include <string>

#include <cairo.h>

namespace wf
{
namespace emeraldwf
{
// One rendered frame surface at a given output scale, plus the GL texture
// the node created from it (opaque handle: wf::texture_t * in the node;
// the node owns it in a parallel map). M10 (issue #1): the surface is
// sized physical = logical * scale, so each DISTINCT scale needs its own
// surface; a window visible on outputs of different scale (mixed-scale
// multi-output) must NOT re-render on every pass — it reuses the entry
// for the current scale.
struct node_scale_entry_t
{
    cairo_surface_t *surface = nullptr; // owned; destroyed on replace/dtor
    void *texture = nullptr;
};

// Cache state of the node (kept plain so the invalidation predicate can be
// tested headlessly — see `needs_rebuild()` below).
struct node_cache_t
{
    // The title the surfaces were rendered with ("" if none).
    std::string title = "";
    // Whether the surfaces were rendered with active/inactive frame settings.
    bool active = false;
    // Full frame size the surfaces were rendered for (D7).
    int width = 0, height = 0;
    // Last hover button (-1 if none) — drives button-fade + hover redraws.
    int hover_button = -1;
    // Per-scale rendered surfaces (M10): keyed on the output scale
    // (render_target_t.scale). Bounded in the node (see render_frame).
    std::map<double, node_scale_entry_t> surfaces;
};

// ── Frame-texture pick (per-frame upload regression) ───────────────────────
//
// The frame's GL texture is derived from the frame's cairo surface
// (wlr_texture_from_pixels). The surface is IMMUTABLE once rendered —
// re-rendering replaces the pointer, never mutating pixels in place — so
// the texture created from it stays valid until the surface is replaced.
//
// The frame node is a child of the view's surface root, and
// view_t::damage() damages the WHOLE view: on a page with continuous client
// damage (video, live feed) the frame node is therefore rendered at the
// client's framerate with an UNCHANGED cached surface. Re-wrapping the
// cached surface in a fresh GL texture on every such pass costs a full
// CPU→GPU copy of the whole frame buffer per frame (tens of MB/s on a
// large 2×-scaled window). The pick is pure so that contract — "a cache
// hit reuses the texture; only a new surface uploads" — is pinned
// headlessly in tests/test_render_node.cpp.
struct texture_pick_t
{
    // Texture to use for this pass (opaque, see node_cache_t::texture).
    // nullptr ⇒ nothing to draw (no surface) or the caller must create one
    // first (must_create).
    void *texture = nullptr;
    // True ⇒ the caller must create a NEW texture from the cached surface
    // and update node_cache_t::texture; false ⇒ `texture` is the cached one
    // and may be blitted directly.
    bool must_create = false;
};

texture_pick_t pick_frame_texture(const node_scale_entry_t & entry,
    const cairo_surface_t *surface);

// Pure invalidation predicate (spec 04 Tests): returns true if the node must
// re-render the frame next render pass. Extracted from the render instance so
// it can be tested directly without GL or wayfire.
//
// M10 (issue #1): the key is the SCALE-INVARIANT inputs (active, title,
// frame size, hover button). Scale is deliberately NOT part of the key —
// the per-scale surface cache (node_cache_t::surfaces) covers it: a scale
// without a cached entry is a one-time render, not a rebuild (see
// has_cached_scale()). Pre-M10, scale was in the key, so a window visible
// on a 1.0× and a 2.0× output re-rendered + re-uploaded on EVERY pass of
// both outputs (the per-frame-upload regression pick_frame_texture was
// built to eliminate, resurrected by mixed-scale outputs).
bool needs_rebuild(const node_cache_t & cache, bool active,
    const std::string & title, int frame_w, int frame_h,
    int hover_button);

// M10 (issue #1): true if a surface for `scale` is already cached. The
// caller must have established that the scale-invariant key matches
// (needs_rebuild() == false) — the entries are all rendered with the
// cached key.
bool has_cached_scale(const node_cache_t & cache, double scale);

// ── Frame-bounding-box math (extracted for the headless regression test) ────
//
// The emerald frame size is `client + margins` on each axis, and the node sits
// at offset {-margins.left, -margins.top} relative to the client (the emerald
// shadow lives INSIDE the frame extents). This is the pure core of
// emerald_node_t::node_bounding_box(), extracted so the "zero client on first
// map" bug (part 06) can be tested headlessly without a wayfire toplevel.
//
// A toplevel's wm geometry is 0×0 while it is mapping (wayfire populates it
// after the mapping transaction). If the frame is sized from that 0×0 client,
// the frame collapses to just the border thickness (a degenerate sliver) —
// this is the root of the "no decoration painted" bug. The fix is to recompute
// this once the real client size is known (the plugin's
// view_geometry_changed_signal handler).
struct frame_box_input_t
{
    int client_x = 0, client_y = 0; // client position in surface-root coords
    int client_w = 0, client_h = 0; // client size
    int m_left = 0, m_right = 0, m_bottom = 0, m_top = 0;
};

struct frame_box_out_t
{
    int x = 0, y = 0; // node offset (parent coords)
    int w = 0, h = 0; // full frame size (D7)
};

frame_box_out_t frame_box_for(const frame_box_input_t & in);

// ── Frame box from the pending geometry (part 06: live-map SEGV regression) ─
//
// node_bounding_box() must NOT derive the client from the surface-root node's
// get_bounding_box(): the emerald node is a child of that node, so the query
// recurses (node → parent's union-of-children → node → …) and overflows the
// stack on any window map. The authoritative, parent-free source is the
// toplevel's pending geometry (the FRAME, client+margins) and margins — the
// same pair wayfire uses to size the client surface. This is the pure core of
// that derivation, extracted so the "do not recurse through the parent"
// contract can be tested headlessly.
struct pending_box_input_t
{
    int frame_w = 0, frame_h = 0; // pending().geometry.width/height (frame)
    int m_left = 0, m_right = 0, m_bottom = 0, m_top = 0; // pending().margins
};

frame_box_out_t frame_box_from_pending(const pending_box_input_t & in);

// ── Render guard (finding 27) ────────────────────────────────────────────────
//
// The emerald frame must NOT be drawn on a fullscreen window: the margins
// are 0, so the node's bounding box equals the client box and compositing
// the frame would paint the emerald titlebar/borders OVER the fullscreen
// content (pixdecor hides its frame the same way). Zero-sized frames are
// likewise not drawn.
inline bool frame_should_render(bool fullscreen, int frame_w, int frame_h)
{
    return !fullscreen && frame_w > 0 && frame_h > 0;
}

// ── Parent→frame-local coordinate conversion (extracted for regression) ────
//
// Parent (surface-root) coords have the client's top-left at (0,0); the frame
// origin is at {box.x, box.y} = {-margins.left, -margins.top} (see
// frame_box_for). Frame-local coords have the frame's top-left at (0,0):
// local = parent - origin.
// This is the pure core of emerald_node_t::to_local(). The sign of the
// origin is the part that regressed in part 06 (a positive offset shifted
// every input coordinate above/left of the frame, so titlebar drags and
// border resizes never registered).
struct frame_local_out_t
{
    int x = 0, y = 0;
};

frame_local_out_t frame_local_for(int parent_x, int parent_y,
    const frame_box_out_t & box);

// ── Titlebar double-click detection (extracted for regression, finding 19) ──
//
// The original emerald relies on the input server's GDK_2BUTTON_PRESS event
// (timing from `gtk-double-click-time`). The wayfire port gets plain
// BTN_LEFT presses with no server-side double-click, so the node must
// synthesise it: a second press on the titlebar within DOUBLE_CLICK_TIMEOUT_MS
// of the previous one is a double-click. `last_press_ms == 0` means "no
// previous press". On a detected double-click the state is reset to 0 so the
// third press of a triple-click does not re-trigger it.
struct double_click_state_t
{
    uint32_t last_press_ms = 0;
};

bool is_double_click(double_click_state_t & state, uint32_t now_ms,
    uint32_t timeout_ms = 400);

// ── Titlebar press/move state machine (finding 20) ─────────────────────────
//
// A titlebar left-press must NOT commit the WM move gesture immediately:
// `move_request()` grabs the pointer, and the second press of a
// double-click would then never reach the node — which is exactly why the
// finding-19 synthesised double-click never fired in a live session.
//
// Instead a press only ARMS a pending move. The pending move commits (the
// caller must then issue move_request) only once the pointer has moved
// more than `commit_px` from the press position — a real drag. A second
// press within the double-click window cancels the pending move and
// produces DOUBLE_CLICK (the caller fires double_click_action). A single
// click (press + release, no motion, no second press) has no side effect.
//
// Coordinates are GLOBAL (the node's parent coord space) — plain doubles so
// the state machine stays wayfire-free.
struct titlebar_input_state_t
{
    double_click_state_t dbl; // finding-19 timing
    bool pending_move = false; // a press is armed, awaiting commit/cancel
    double press_x    = 0;        // global pointer position at the armed press
    double press_y    = 0;
};

enum class titlebar_press_result
{
    DOUBLE_CLICK, // caller fires double_click_action
    ARM_MOVE, // caller arms a pending move (NO immediate move_request)
};

titlebar_press_result on_titlebar_press(titlebar_input_state_t & state,
    double global_x, double global_y, uint32_t now_ms,
    uint32_t timeout_ms = 400);

// True exactly once when the armed pending move crosses the commit
// threshold; false while below it and when nothing is armed.
bool on_titlebar_motion(titlebar_input_state_t & state,
    double global_x, double global_y, double commit_px = 4.0);

// Left release: drops a never-committed pending move (a click, not a
// drag). Double-click timing deliberately survives the release —
// press-release-press within the window is exactly a double-click.
void on_button_release(titlebar_input_state_t & state);

// ── Touch-up button gate (M12, issue #1) ───────────────────────────────────
//
// A touch-up must only fire a button action when a MATCHING touch-down
// (same finger) started on the SAME button. Pre-M12 the node fired
// handle_button() on any lift-off over a button, remembering neither the
// down position nor the finger: a swipe ending on the close button closed
// the window.
//
// The state records the active touch-down (finger + frame-local position).
// The gate is pure: the node computes the button index under the down
// position and under the lift-off position (same ctx, so the comparison is
// consistent) and passes both in.
struct touch_state_t
{
    bool down_active = false;
    int down_finger = -1;
    int down_x = 0, down_y = 0; // frame-local position of the down
};

enum class touch_up_result
{
    NO_ACTION, // no matching down, or down/up not on the same button
    ACTION, // fire the button (up_button)
};

void on_touch_down(touch_state_t & state, int finger_id,
    int frame_local_x, int frame_local_y);

// `down_button`/`up_button`: hit_button() results (-1 when no button) at
// the stored down position and the lift-off position. The down is
// consumed on a matching-finger up regardless of the outcome.
touch_up_result on_touch_up(touch_state_t & state, int finger_id,
    int down_button, int up_button);

// ── Titlebar action → WM request mapping (finding 21) ─────────────────────
//
// Emerald's titlebar.h DOUBLE_CLICK_* enum (0 shade, 1 maximize, 2
// minimize, 3 send_to_back, 4 close, 5 none) → the request the node must
// issue. The mapping is pure so it is unit-tested: finding 21 was exactly
// that case 3 (SEND_TO_BACK) was missing from the node's dispatch and got
// silently swallowed.
enum class titlebar_action_request
{
    NONE,
    TOGGLE_SHADE,
    MAXIMIZE_TOGGLE,
    MINIMIZE,
    SEND_TO_BACK,
    CLOSE,
};

titlebar_action_request map_titlebar_action(int emerald_action);
} // namespace emeraldwf
} // namespace wf
