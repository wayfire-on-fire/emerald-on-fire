// emerald-node-pure.cpp — Part 04: wayfire-free pure helpers for the scene
// node. Compiled into the headless test (tests/test_render_node.cpp) without
// any wayfire dependency.
#include "emerald-node-pure.hpp"

namespace wf
{
namespace emeraldwf
{
// The node re-renders the frame when any input that feeds
// `emeraldwf_render_frame()` changed since the last render.
bool needs_rebuild(const node_cache_t & cache, bool active,
    const std::string & title, int frame_w, int frame_h,
    double scale, int hover_button)
{
    if (cache.surface == nullptr)
    {
        return true;
    }

    if (cache.active != active)
    {
        return true;
    }

    if (cache.title != title)
    {
        return true;
    }

    if ((cache.width != frame_w) || (cache.height != frame_h))
    {
        return true;
    }

    if (cache.scale != scale)
    {
        return true;
    }

    if (cache.hover_button != hover_button)
    {
        return true;
    }

    return false;
}

// The frame surface is immutable once rendered, so its texture stays valid
// until the surface pointer is replaced. Cache hit (same surface, texture
// recorded) → reuse the texture, no upload; anything else → create.
texture_pick_t pick_frame_texture(const node_cache_t & cache,
    const cairo_surface_t *surface)
{
    texture_pick_t pick;
    if (surface == nullptr)
    {
        return pick; // nothing to draw
    }

    if ((cache.surface == surface) && (cache.texture != nullptr))
    {
        pick.texture = cache.texture; // cache hit — blit the existing texture
        return pick;
    }

    pick.must_create = true; // new surface (or missing texture) → new texture
    return pick;
}

// Frame bounding box (pure). See frame_box_for in the header for the
// rationale — this is the math that collapses when the client is 0×0.
frame_box_out_t frame_box_for(const frame_box_input_t & in)
{
    frame_box_out_t out;
    out.w = in.client_w + in.m_left + in.m_right;
    out.h = in.client_h + in.m_top + in.m_bottom;
    if (out.w < 0)
    {
        out.w = 0;
    }

    if (out.h < 0)
    {
        out.h = 0;
    }

    // The frame box surrounds the client wherever the client sits in the
    // surface-root coords: when the toplevel surface is frame-sized the
    // client is at (m_left, m_top) → box origin (0, 0); when the surface was
    // collapsed to the client size (wayfire's resize() path writes client
    // dims without margins) the client is at (0, 0) → box origin
    // (-m_left, -m_top). Both cases are correct for the frame.
    out.x = in.client_x - in.m_left;
    out.y = in.client_y - in.m_top;
    return out;
}

// Parent (surface-root) → frame-local. See frame_local_for in the header for
// the convention: local = parent - frame-origin (the origin is negative).
frame_local_out_t frame_local_for(int parent_x, int parent_y,
    const frame_box_out_t & box)
{
    return {parent_x - box.x, parent_y - box.y};
}

// Frame box from the pending geometry (see the header for the rationale — this
// is the parent-free derivation node_bounding_box() must use). The client is
// the frame shrunk by the margins and sits at (0,0) in surface-root coords.
frame_box_out_t frame_box_from_pending(const pending_box_input_t & in)
{
    int client_w = in.frame_w - in.m_left - in.m_right;
    int client_h = in.frame_h - in.m_top - in.m_bottom;
    if (client_w < 0)
    {
        client_w = 0;
    }

    if (client_h < 0)
    {
        client_h = 0;
    }

    return frame_box_for({
                0, 0,
                client_w, client_h,
                in.m_left, in.m_right, in.m_bottom, in.m_top});
}

// Titlebar double-click detection (finding 19). See the header for the
// convention: this replaces the server-side GDK_2BUTTON_PRESS that the
// original emerald relied on.
bool is_double_click(double_click_state_t & state, uint32_t now_ms,
    uint32_t timeout_ms)
{
    if (state.last_press_ms == 0)
    {
        state.last_press_ms = now_ms;
        return false;
    }

    if (now_ms < state.last_press_ms)
    {
        // Clock went backwards (timer wrap / event reordering) — treat as a
        // fresh single click rather than a bogus double-click.
        state.last_press_ms = now_ms;
        return false;
    }

    if (now_ms - state.last_press_ms <= timeout_ms)
    {
        state.last_press_ms = 0; // consume it; next press starts fresh
        return true;
    }

    state.last_press_ms = now_ms;
    return false;
}

// ── Titlebar press/move state machine (finding 20) ─────────────────────────
//
// See the header for the rationale: a titlebar left-press must not commit
// the WM move gesture up front, because move_request() grabs the pointer
// and would swallow the second press of a double-click. A press arms a
// pending move; motion past the commit threshold commits it; a second
// press in the window cancels it and reports DOUBLE_CLICK.
titlebar_press_result on_titlebar_press(titlebar_input_state_t & state,
    double global_x, double global_y, uint32_t now_ms, uint32_t timeout_ms)
{
    if (is_double_click(state.dbl, now_ms, timeout_ms))
    {
        // A double-click supersedes the pending move from the first press.
        state.pending_move = false;
        return titlebar_press_result::DOUBLE_CLICK;
    }

    state.pending_move = true;
    state.press_x = global_x;
    state.press_y = global_y;
    return titlebar_press_result::ARM_MOVE;
}

bool on_titlebar_motion(titlebar_input_state_t & state,
    double global_x, double global_y, double commit_px)
{
    if (!state.pending_move)
    {
        return false;
    }

    double dx = global_x - state.press_x;
    double dy = global_y - state.press_y;
    if (dx * dx + dy * dy > commit_px * commit_px)
    {
        // Commit once; the caller then grabs the pointer via move_request.
        state.pending_move = false;
        return true;
    }

    return false;
}

void on_button_release(titlebar_input_state_t & state)
{
    // A release drops a never-committed pending move. The double-click
    // timing in state.dbl is intentionally untouched.
    state.pending_move = false;
}

// ── Titlebar action → WM request mapping (finding 21) ─────────────────────
titlebar_action_request map_titlebar_action(int emerald_action)
{
    switch (emerald_action)
    {
      case 0: // DOUBLE_CLICK_SHADE
        return titlebar_action_request::TOGGLE_SHADE;

      case 1: // DOUBLE_CLICK_MAXIMIZE
        return titlebar_action_request::MAXIMIZE_TOGGLE;

      case 2: // DOUBLE_CLICK_MINIMIZE
        return titlebar_action_request::MINIMIZE;

      case 3: // DOUBLE_CLICK_SEND_TO_BACK
        return titlebar_action_request::SEND_TO_BACK;

      case 4: // DOUBLE_CLICK_CLOSE
        return titlebar_action_request::CLOSE;

      default: // DOUBLE_CLICK_NONE (5) and anything unknown
        return titlebar_action_request::NONE;
    }
}
} // namespace emeraldwf
} // namespace wf
