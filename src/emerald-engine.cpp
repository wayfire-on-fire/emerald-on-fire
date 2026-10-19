/*
 * emerald-engine.cpp — engine dlopen bridge + pure frame render (part 02).
 *
 * The dlopen loader is a port of emerald src/engine_loader.c:32-84 (same search order, same exported symbols,
 * same call conventions). The render path is a port of emerald src/main.c:1768-2050
 * (draw_window_decoration_real + draw_shadow_background) with X pixmaps replaced by a cairo ARGB32 image
 * surface.
 *
 * NOTE on D4: the bit values pinned in the overview for EmeraldWindowState/EmeraldWindowActions do not match
 * the installed libwnck 3.0 enums (see compat/README). We therefore map explicitly:
 *
 *   state   EWS_MAXIMIZED_HORIZONTALLY -> WNCK_WINDOW_STATE_MAXIMIZED_HORIZONTALLY EWS_MAXIMIZED_VERTICALLY
 *   -> WNCK_WINDOW_STATE_MAXIMIZED_VERTICALLY EWS_SHADED                 -> WNCK_WINDOW_STATE_SHADED
 * EWS_ABOVE                  -> WNCK_WINDOW_STATE_ABOVE EWS_STICKY                 ->
 * WNCK_WINDOW_STATE_STICKY state   — mapped EWS_* -> WNCK_WINDOW_STATE_* (the engines select bistate icons
 * via the libwnck state values, e.g. maximize vs. restore).
 *   actions — NOT mapped. d.actions carries the plugin's EWA_* bits verbatim; the button visibility test
 * (button_not_visible) ANDs it against emerald_button_actions, which is EWA-based. Converting to WNCK bits
 * here made every test evaluate to 0 and hid all buttons (part 06 regression).
 */

// titlebar.h defines the button bistate/state-flag arrays under these
// guards; the emerald main.c build enables them (main.c:25-28). They must be
// defined before emerald.h reaches its #include <titlebar.h>, so they come
// before any emerald header. (NEED_BUTTON_ACTIONS is NOT enabled — the
// action bitmask table is emerald_button_actions in emerald-types.hpp,
// mapped to the plugin's EWA_* values.)
#define NEED_BUTTON_BISTATES
#define NEED_BUTTON_STATE_FLAGS

#include "emerald-engine.hpp"
#include "emerald-settings.hpp"

#include <cairo.h>
#include <dlfcn.h>
#include <glib.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace
{
// ---- engine loader state (port of src/engine_loader.c) ----
void *engine_handle = nullptr;

typedef void (*init_engine_proc)(window_settings*);
typedef void (*fini_engine_proc)(window_settings*);
typedef void (*load_settings_proc)(GKeyFile*, window_settings*);
typedef void (*draw_frame_proc)(decor_t*, cairo_t*);

init_engine_proc e_init   = nullptr;
fini_engine_proc e_fini   = nullptr;
load_settings_proc e_load = nullptr;
draw_frame_proc e_draw    = nullptr;
// last ws passed to init_engine, so e_fini(ws) can free engine-private data
window_settings *e_ws = nullptr;
// name of the currently-loaded engine (for the pixmap pixmap guard)
std::string active_engine_name;

extern "C" void call_load_engine_settings(GKeyFile *f, window_settings *ws)
{
    if (e_load && engine_handle)
    {
        e_load(f, ws);
    }
}

extern "C" void call_engine_draw_frame(decor_t *d, cairo_t *cr)
{
    if (e_draw && engine_handle)
    {
        e_draw(d, cr);
    }
}

using wf::emeraldwf::EWS_MAXIMIZED_HORIZONTALLY;
using wf::emeraldwf::EWS_MAXIMIZED_VERTICALLY;
using wf::emeraldwf::EWS_SHADED;
using wf::emeraldwf::EWS_ABOVE;
using wf::emeraldwf::EWS_STICKY;

// ---- D4 mapping (see file comment) ----
WnckWindowState to_wnck_state(uint32_t ews)
{
    // WnckWindowState is an enum (libwnck 3.0), so `s |= ENUM` is a compile
    // error for an enum variable; accumulate in int and cast once.
    int s = 0;
    if (ews & EWS_MAXIMIZED_HORIZONTALLY)
    {
        s |= WNCK_WINDOW_STATE_MAXIMIZED_HORIZONTALLY;
    }

    if (ews & EWS_MAXIMIZED_VERTICALLY)
    {
        s |= WNCK_WINDOW_STATE_MAXIMIZED_VERTICALLY;
    }

    if (ews & EWS_SHADED)
    {
        s |= WNCK_WINDOW_STATE_SHADED;
    }

    if (ews & EWS_ABOVE)
    {
        s |= WNCK_WINDOW_STATE_ABOVE;
    }

    if (ews & EWS_STICKY)
    {
        s |= WNCK_WINDOW_STATE_STICKY;
    }

    return static_cast<WnckWindowState>(s);
}

// NB: there is deliberately NO EWA→WNCK action mapping. d.actions carries the
// plugin's EWA_* bits verbatim: button_not_visible() ANDs them against
// emerald_button_actions (emerald-types.hpp), which is EWA-based. A WNCK
// conversion here made every `actions & mask` test evaluate to 0, so every
// button was invisible even though the layout positioned them (part 06).

// ---- draw helpers ported from src/main.c ----
// port of main.c:1098-1106 (get_b_state)
static int get_b_state(const decor_t *d, int button)
{
    int ret = d->active ? 0 : 3;

    if (d->button_states[button] & IN_EVENT_WINDOW)
    {
        ret++;
        if (d->button_states[button] & PRESSED_EVENT_WINDOW)
        {
            ret++;
        }
    }

    return ret;
}

// port of main.c:291 (get_real_pos)
static gint get_real_pos(const window_settings *ws, gint tobj, const decor_t *d)
{
    switch (d->tobj_item_state[tobj])
    {
      case 1:
        return ((d->width + ws->left_space - ws->right_space +
            d->tobj_size[0] - d->tobj_size[1] - d->tobj_size[2]) / 2 +
            d->tobj_item_pos[tobj]);

      case 2:
        return (d->width - ws->right_space - d->tobj_size[2] +
            d->tobj_item_pos[tobj]);

      case 3:
        return -1;

      default:
        return (ws->left_space + d->tobj_item_pos[tobj]);
    }
}

// port of main.c:73 (BUTTON_NOT_VISIBLE) — uses emerald_button_actions
// (emerald-types.hpp), the EWA_*-mapped action table shared with the layout.
static bool button_not_visible(const decor_t *d, int b_t)
{
    return d->tobj_item_state[b_t] == 3 ||
           !(d->actions & wf::emeraldwf::emerald_button_actions[b_t]);
}

// port of main.c:1251 (button_state_paint)
static void button_state_paint(cairo_t *cr, const alpha_color *color,
    const alpha_color *color_2, guint state)
{
    double alpha;

    if (state & IN_EVENT_WINDOW)
    {
        alpha = 1.0;
    } else
    {
        alpha = color->alpha;
    }

    if ((state & (PRESSED_EVENT_WINDOW | IN_EVENT_WINDOW)) ==
        (PRESSED_EVENT_WINDOW | IN_EVENT_WINDOW))
    {
        cairo_set_source_rgba(cr, color->color.r, color->color.g,
            color->color.b, alpha);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, color_2->color.r, color_2->color.g,
            color_2->color.b, color_2->alpha);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        cairo_set_line_width(cr, 2.0);
    } else
    {
        cairo_set_source_rgba(cr, color_2->color.r, color_2->color.g,
            color_2->color.b, color_2->alpha);
        cairo_stroke_preserve(cr);
        cairo_set_source_rgba(cr, color->color.r, color->color.g,
            color->color.b, alpha);
        cairo_fill(cr);
    }
}

// port of main.c:1000-1095 (vector button glyphs)
static void draw_help_button(cairo_t *cr, double s)
{
    (void)s;
    cairo_rel_move_to(cr, 0.0, 6.0);
    cairo_rel_line_to(cr, 0.0, 3.0);
    cairo_rel_line_to(cr, 4.5, 0.0);
    cairo_rel_line_to(cr, 0.0, 4.5);
    cairo_rel_line_to(cr, 3.0, 0.0);
    cairo_rel_line_to(cr, 0.0, -4.5);

    cairo_rel_line_to(cr, 4.5, 0.0);

    cairo_rel_line_to(cr, 0.0, -3.0);
    cairo_rel_line_to(cr, -4.5, 0.0);
    cairo_rel_line_to(cr, 0.0, -4.5);
    cairo_rel_line_to(cr, -3.0, 0.0);
    cairo_rel_line_to(cr, 0.0, 4.5);

    cairo_close_path(cr);
}

static void draw_close_button(cairo_t *cr, double s)
{
    cairo_rel_move_to(cr, 0.0, s);

    cairo_rel_line_to(cr, s, -s);
    cairo_rel_line_to(cr, s, s);
    cairo_rel_line_to(cr, s, -s);
    cairo_rel_line_to(cr, s, s);

    cairo_rel_line_to(cr, -s, s);
    cairo_rel_line_to(cr, s, s);
    cairo_rel_line_to(cr, -s, s);
    cairo_rel_line_to(cr, -s, -s);

    cairo_rel_line_to(cr, -s, s);
    cairo_rel_line_to(cr, -s, -s);
    cairo_rel_line_to(cr, s, -s);

    cairo_close_path(cr);
}

static void draw_max_button(cairo_t *cr, double s)
{
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);

    cairo_rel_line_to(cr, 12.0, 0.0);
    cairo_rel_line_to(cr, 0.0, 12.0);
    cairo_rel_line_to(cr, -12.0, 0.0);

    cairo_close_path(cr);

    cairo_rel_move_to(cr, 2.0, s);

    cairo_rel_line_to(cr, 8.0, 0.0);
    cairo_rel_line_to(cr, 0.0, 10.0 - s);
    cairo_rel_line_to(cr, -8.0, 0.0);

    cairo_close_path(cr);
}

static void draw_unmax_button(cairo_t *cr, double s)
{
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);

    cairo_rel_move_to(cr, 1.0, 1.0);

    cairo_rel_line_to(cr, 10.0, 0.0);
    cairo_rel_line_to(cr, 0.0, 10.0);
    cairo_rel_line_to(cr, -10.0, 0.0);

    cairo_close_path(cr);

    cairo_rel_move_to(cr, 2.0, s);

    cairo_rel_line_to(cr, 6.0, 0.0);
    cairo_rel_line_to(cr, 0.0, 8.0 - s);
    cairo_rel_line_to(cr, -6.0, 0.0);

    cairo_close_path(cr);
}

static void draw_min_button(cairo_t *cr, double s)
{
    (void)s;
    cairo_rel_move_to(cr, 0.0, 8.0);

    cairo_rel_line_to(cr, 12.0, 0.0);
    cairo_rel_line_to(cr, 0.0, 4.0);
    cairo_rel_line_to(cr, -12.0, 0.0);

    cairo_close_path(cr);
}

// port of main.c:1150-1244 (draw_button_with_glow_alpha_bstate)
// y1 unused by the wayfire port (buttons are positioned via button_region).
static void draw_button_with_glow_alpha_bstate(gint b_t, const decor_t *d,
    cairo_t *cr, gint y1,
    gdouble button_alpha,
    gdouble glow_alpha, int b_state)
{
    (void)y1;
    // B_T_* → B_* surface index (main.c:250-262 get_b_t_offset). The surface
    // arrays are B_*-indexed with bistate pairs interleaved; a bare b = b_t
    // made MINIMIZE draw the RESTORE surface (part 06 regression).
    gint b = wf::emeraldwf::get_b_t_offset(b_t);
    gdouble x, y;
    window_settings *ws = d->fs->ws;

    if (b_state < 0)
    {
        b_state = get_b_state(d, b_t);
    }

    // bi-state: emerald reuses the same surface slot pair for e.g.
    // maximize/restore; the state flag selects which of the two
    // (main.c:250-262, get_b_t_offset + the b++ in the draw function).
    if (btbistate[b_t])
    {
        if (d->state & btstateflag[b_t])
        {
            b++;
        }
    }

    if (button_not_visible(d, b_t))
    {
        return;
    }

    const button_region_t *button_region =
        (d->active ? &d->button_region[b_t] : &d->button_region_inact[b_t]);
    x = button_region->base_x1;
    y = button_region->base_y1;

    if (ws->use_pixmap_buttons)
    {
        const gdouble w = button_region->base_x2 - x;
        const gdouble h = button_region->base_y2 - y;
        if (ws->button_surface[b_state + b * S_COUNT] != NULL)
        {
            cairo_save(cr);
            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
            cairo_translate(cr, x, y);
            cairo_scale(cr, w / (gdouble)cairo_image_surface_get_width(
                ws->button_surface[b_state + b * S_COUNT]),
                h / (gdouble)cairo_image_surface_get_height(
                    ws->button_surface[b_state + b * S_COUNT]));
            cairo_set_source_surface(cr, ws->button_surface[b_state + b * S_COUNT],
                0, 0);
            cairo_paint_with_alpha(cr, button_alpha);
            cairo_restore(cr);
        }

        if (glow_alpha > 1e-5) /* i.e. glow is on */
        {
            const gdouble glow_x  = button_region->glow_x1;
            const gdouble glow_y  = button_region->glow_y1;
            const gdouble glow_w  = button_region->glow_x2 - button_region->glow_x1;
            const gdouble glow_h  = button_region->glow_y2 - button_region->glow_y1;
            cairo_surface_t *glow = nullptr;
            if (d->active && ws->use_button_glow)
            {
                glow = ws->button_glow_surface[b];
            } else if (!d->active && ws->use_button_inactive_glow)
            {
                glow = ws->button_inactive_glow_surface[b];
            }

            if (glow != NULL)
            {
                cairo_save(cr);
                cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
                cairo_translate(cr, glow_x, glow_y);
                cairo_scale(cr, glow_w / (gdouble)cairo_image_surface_get_width(glow),
                    glow_h / (gdouble)cairo_image_surface_get_height(glow));
                cairo_set_source_surface(cr, glow, 0, 0);
                cairo_paint_with_alpha(cr, glow_alpha);
                cairo_restore(cr);
            }
        }
    } else
    {
        y += 3;
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, x, y);
        switch (b)
        {
          case B_CLOSE:
            draw_close_button(cr, 3.1);
            break;

          case B_MAXIMIZE:
            draw_max_button(cr, 4.0);
            break;

          case B_RESTORE:
            draw_unmax_button(cr, 4.0);
            break;

          case B_SUPERMAXIMIZE:
            draw_max_button(cr, 4.0);
            break;

          case B_MINIMIZE:
            draw_min_button(cr, 4.0);
            break;

          case B_HELP:
            cairo_move_to(cr, x, y);
            draw_help_button(cr, 3.1);
            break;

          default:
            /* FIXME: do something here (same as emerald) */
            break;
        }

        button_state_paint(cr, &d->fs->button, &d->fs->button_halo,
            d->button_states[b_t]);
    }
}

// The pixmap engine's engine_draw_frame dereferences pfs->pixmaps[i].surface,
// which is only populated when the theme ships pixmaps.*.png files. Without
// them (e.g. the default theme) the surfaces are NULL and cairo crashes.
// Detect that case and skip the engine frame (draw a plain background), so
// the bridge stays robust for any emerald theme.
static bool pixmap_theme_has_pixmaps(const wf::emeraldwf::ThemeSettings & ts)
{
    if (active_engine_name != "pixmap")
    {
        return true;
    }

    const std::string dir = ts.theme_dir();
    const char *names[]   = {"top", "left", "right", "bottom", "title"};
    return std::any_of(std::begin(names), std::end(names),
        [&dir] (const char *n)
    {
        std::error_code ec;
        return std::filesystem::exists(
            dir + "/pixmaps." + n + ".png", ec);
    });
}

// port of main.c:879-984 (draw_shadow_background)
static void draw_shadow_background(const decor_t *d, cairo_t *cr)
{
    cairo_matrix_t matrix;
    double w, h, x2, y2;
    int width, height;
    gint left, right, top, bottom;
    window_settings *ws = d->fs->ws;

    if (ws->large_shadow_surface == NULL)
    {
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
        cairo_paint(cr);
        return;
    }

    width  = cairo_image_surface_get_width(ws->large_shadow_surface);
    height = cairo_image_surface_get_height(ws->large_shadow_surface);

    left   = ws->left_space + ws->left_corner_space;
    right  = ws->right_space + ws->right_corner_space;
    top    = ws->top_space + ws->top_corner_space;
    bottom = ws->bottom_space + ws->bottom_corner_space;

    if (d->width - left - right < 0)
    {
        left  = d->width / 2;
        right = d->width - left;
    }

    if (d->height - top - bottom < 0)
    {
        top    = d->height / 2;
        bottom = d->height - top;
    }

    w = d->width - left - right;
    h = d->height - top - bottom;

    x2 = d->width - right;
    y2 = d->height - bottom;

    // The middle bands (top/bottom/left/right) are drawn from ONE constant
    // texture column / row — the tiling trick that keeps the band uniform
    // and seam-free with the 1:1 corner regions (the band's column/row is
    // the corner regions' split line). That column/row must be DEEP in the
    // blurred, +offset-shifted silhouette's dense core: the sliver occupies
    // [left_space-ext.left, d_width-right) x [top_space-ext.top,
    // d_height-bottom) in the texture (the window's position in the frame;
    // see create_large_shadow_surface), so after the +offset shift the
    // full-coverage core spans
    // [offset_x + size, d_width-right+offset_x-size] x
    // [offset_y + size, d_height-bottom+offset_y-size]
    // in texture coords. Clamp the sampled column/row into that core; for
    // normal radii `left`/`top` already sit inside it.
    const int shadow_size = (int)(ws->shadow_radius * 2 + 2) / 2;
    int band_col = std::max(left, ws->shadow_offset_x + shadow_size);
    band_col = std::min(band_col,
        (int)width - right + ws->shadow_offset_x - shadow_size);
    int band_row = std::max(top, ws->shadow_offset_y + shadow_size);
    band_row = std::min(band_row,
        (int)height - bottom + ws->shadow_offset_y -
        shadow_size);

    /* top left */
    cairo_matrix_init_identity(&matrix);
    cairo_pattern_set_matrix(ws->shadow_pattern, &matrix);
    cairo_set_source(cr, ws->shadow_pattern);
    cairo_rectangle(cr, 0.0, 0.0, left, top);
    cairo_fill(cr);

    /* top */
    if (w > 0)
    {
        cairo_matrix_init_translate(&matrix, band_col, 0.0);
        cairo_matrix_scale(&matrix, 1.0 / w, 1.0);
        cairo_matrix_translate(&matrix, -left, 0.0);
        cairo_pattern_set_matrix(ws->shadow_pattern, &matrix);
        cairo_set_source(cr, ws->shadow_pattern);
        cairo_rectangle(cr, left, 0.0, w, top);
        cairo_fill(cr);
    }

    /* top right */
    cairo_matrix_init_translate(&matrix, width - right - x2, 0.0);
    cairo_pattern_set_matrix(ws->shadow_pattern, &matrix);
    cairo_set_source(cr, ws->shadow_pattern);
    cairo_rectangle(cr, x2, 0.0, right, top);
    cairo_fill(cr);

    /* left */
    if (h > 0)
    {
        cairo_matrix_init_translate(&matrix, 0.0, band_row);
        cairo_matrix_scale(&matrix, 1.0, 1.0 / h);
        cairo_matrix_translate(&matrix, 0.0, -top);
        cairo_pattern_set_matrix(ws->shadow_pattern, &matrix);
        cairo_set_source(cr, ws->shadow_pattern);
        cairo_rectangle(cr, 0.0, top, left, h);
        cairo_fill(cr);
    }

    /* right */
    // cppcheck-suppress duplicateCondition
    // The left and right middle bands are independent fills that both require
    // h > 0; they cannot be merged (the right band's translate-x
    // `width-right-x2` differs from its rect-x `x2`), so the guard repeats.
    if (h > 0)
    {
        cairo_matrix_init_translate(&matrix, width - right - x2, band_row);
        cairo_matrix_scale(&matrix, 1.0, 1.0 / h);
        cairo_matrix_translate(&matrix, 0.0, -top);
        cairo_pattern_set_matrix(ws->shadow_pattern, &matrix);
        cairo_set_source(cr, ws->shadow_pattern);
        cairo_rectangle(cr, x2, top, right, h);
        cairo_fill(cr);
    }

    /* bottom left */
    cairo_matrix_init_translate(&matrix, 0.0, height - bottom - y2);
    cairo_pattern_set_matrix(ws->shadow_pattern, &matrix);
    cairo_set_source(cr, ws->shadow_pattern);
    cairo_rectangle(cr, 0.0, y2, left, bottom);
    cairo_fill(cr);

    /* bottom */
    if (w > 0)
    {
        cairo_matrix_init_translate(&matrix, band_col, height - bottom - y2);
        cairo_matrix_scale(&matrix, 1.0 / w, 1.0);
        cairo_matrix_translate(&matrix, -left, 0.0);
        cairo_pattern_set_matrix(ws->shadow_pattern, &matrix);
        cairo_set_source(cr, ws->shadow_pattern);
        cairo_rectangle(cr, left, y2, w, bottom);
        cairo_fill(cr);
    }

    /* bottom right */
    cairo_matrix_init_translate(&matrix, width - right - x2,
        height - bottom - y2);
    cairo_pattern_set_matrix(ws->shadow_pattern, &matrix);
    cairo_set_source(cr, ws->shadow_pattern);
    cairo_rectangle(cr, x2, y2, right, bottom);
    cairo_fill(cr);
}
}

extern "C"
{
// port of main.c:1608-1652 (draw_buttons_without_fade) plus the fade path
// (main.c:1420-1486 draw_buttons_timer_func): `normal_alpha` / `hover_alpha`
// are the per-button crossfade alphas (the node's button-fade state; both
// nullptr = always the legacy single-surface path). While mid-fade the
// button is drawn in two layers: the non-hovered surface at
// pow(1 - hover, 0.4) underneath and the hovered surface at `hover` on top
// (main.c:1460-1485) — the button itself never disappears.
    static void draw_buttons_without_fade(decor_t *d, cairo_t *cr, double y1,
        const double *normal_alpha = nullptr,
        const double *hover_alpha  = nullptr)
    {
        (void)y1;
        const window_settings *ws = d->fs->ws;

        int b_t;

        for (b_t = 0; b_t < B_T_COUNT; b_t++)
        {
            if (button_not_visible(d, b_t))
            {
                continue;
            }

            double na = normal_alpha ? normal_alpha[b_t] : 1.0;
            double ha = hover_alpha ? hover_alpha[b_t] : 0.0;

            if ((ha <= 1e-4) && (na >= 1.0 - 1e-4))
            {
                // Idle / no-fade / pressed: single surface at full alpha.
                int b_state = get_b_state(d, b_t);
                bool glow   = (ws->use_pixmap_buttons &&
                    ((ws->use_button_glow && b_state == S_ACTIVE_HOVER) ||
                        (ws->use_button_inactive_glow &&
                            b_state == S_INACTIVE_HOVER)));
                draw_button_with_glow_alpha_bstate(b_t, d, cr, (gint)y1,
                    1.0, glow ? 1.0 : 0.0, b_state);
                continue;
            }

            // Mid-fade: crossfade between the non-hovered and hovered surface
            // columns (the counter encodes hover-ness, not opacity).
            if (na > 1e-4)
            {
                draw_button_with_glow_alpha_bstate(b_t, d, cr, (gint)y1,
                    na, 0.0, d->active ? 0 : 3);
            }

            if (ha > 1e-4)
            {
                double glow = 0.0;
                if ((ws->use_button_glow && d->active) ||
                    (ws->use_button_inactive_glow && !d->active))
                {
                    glow = ha;
                }

                draw_button_with_glow_alpha_bstate(b_t, d, cr, (gint)y1,
                    ha, glow, d->active ? 1 : 4);
            }
        }
    }
}

namespace wf::emeraldwf
{
bool emerald_engine_load(const std::string & name, const std::string & system_dir)
{
    // Review F3: reject unsanitized names before they reach the dlopen()
    // path — see is_valid_engine_name() for the threat model.
    if (!is_valid_engine_name(name))
    {
        // The cstyleCast below is inside glib's g_warning() macro expansion
        // (a --force configuration), not in this call.
        // cppcheck-suppress cstyleCast
        g_warning("emerald_engine_load: refusing invalid engine name '%s' "
                  "(only [A-Za-z0-9_-] is allowed)", name.c_str());
        return false;
    }

    std::string engine_ldname = "lib" + name + ".so";
    if (engine_handle)
    {
        if (e_fini && (e_ws != nullptr))
        {
            e_fini(e_ws);
        }

        e_ws = nullptr;
        // Same engine (theme-only reload): keep the .so loaded — a
        // dlopen/dlclose round-trip is pure churn and re-runs the engine's
        // static initializers. The new ws is re-inited by
        // emerald_engine_init_engine() after load().
        if (active_engine_name == name)
        {
            return true;
        }

        dlclose(engine_handle);
        engine_handle = nullptr;
        e_init = nullptr;
        e_fini = nullptr;
        e_load = nullptr;
        e_draw = nullptr;
    }

    active_engine_name = name;

    dlerror(); /* clear errors */

    // search order: ~/.emerald/engines/lib<name>.so then <system_dir>/lib<name>.so
    std::string home_dir = g_get_home_dir();
    std::string path     = home_dir + "/.emerald/engines/" + engine_ldname;

    void *newengine = dlopen(path.c_str(), RTLD_NOW);

    if (!newengine)
    {
        path = system_dir.empty() ? engine_ldname :
            (system_dir + "/" + engine_ldname);
        newengine = dlopen(path.c_str(), RTLD_NOW);

        if (!newengine)
        {
            // The cstyleCast below is inside glib's g_warning() macro
            // expansion (a --force configuration), not in this call.
            // cppcheck-suppress cstyleCast
            g_warning("emerald_engine_load: %s", dlerror());
            return false;
        }
    }

    engine_handle = newengine;

    // dlsym() returns void*; reinterpret_cast is the C++-correct conversion
    // to a function pointer (a C-style cast here is flagged by cppcheck).
    e_init = reinterpret_cast<init_engine_proc>(dlsym(engine_handle, "init_engine"));
    e_fini = reinterpret_cast<fini_engine_proc>(dlsym(engine_handle, "fini_engine"));
    e_load = reinterpret_cast<load_settings_proc>(dlsym(engine_handle, "load_engine_settings"));
    e_draw = reinterpret_cast<draw_frame_proc>(dlsym(engine_handle, "engine_draw_frame"));

    return e_draw != nullptr;
}

void emerald_engine_init_engine(window_settings *ws)
{
    if (e_init && engine_handle)
    {
        e_ws = ws;

        e_init(ws);
    }
}

void emerald_engine_fini_engine(const window_settings *ws)
{
    if (e_fini && engine_handle && (e_ws == ws))
    {
        e_fini(e_ws);
        e_ws = nullptr;
    }
}

void emerald_engine_unload()
{
    if (engine_handle)
    {
        // Free the engine's private state before the code disappears
        // (finding 14) — fini_engine frees engine_ws (and any cached
        // surfaces, per engine).
        if (e_fini && e_ws)
        {
            e_fini(e_ws);
            e_ws = nullptr;
        }

        dlclose(engine_handle);
        engine_handle = nullptr;
    }

    e_init = nullptr;
    e_fini = nullptr;
    e_load = nullptr;
    e_draw = nullptr;
}

cairo_surface_t *emeraldwf_render_frame(const ThemeSettings & ts,
    const DecorContext & ctx)
{
    window_settings *ws = ts.ws();
    if (ws == nullptr)
    {
        return nullptr;
    }

    const int w = ctx.width;
    const int h = ctx.height;
    if ((w <= 0) || (h <= 0))
    {
        return nullptr;
    }

    // Scale factor (render_target_t.scale). The surfaces are created at
    // physical resolution (logical * scale) and the draw context is scaled by
    // `scale`, so every logical-coordinate draw lands at physical pixels and
    // the surface is filled 1:1 by wayfire's logical->framebuffer dst box.
    // scale == 1.0 (headless tests, harnesses) is byte-identical to before.
    const double s = ctx.scale;

    // Finding 19/19c: ctx.width/height are the FULL-EXTENT frame (the layout
    // positions the title objects against them). When the window is
    // maximized the margins exclude cropped parts — but the parts are
    // removed from DIFFERENT edges:
    // shadows off → the shadow spread is cropped from the OUTER edge
    // (the frame origin shifts in);
    // borders off → the border part is cropped from the INNER edge
    // (the frame shrinks; the titlebar/client content
    // reflow — see the piecewise blit below).
    // (Using one combined crop for both — the finding-19 iteration —
    // shifted the surface by the border part when borders were off,
    // landing the engine's border INSIDE the shadow margin: "checking
    // draw shadows draws borders too".)
    const bool maximized =
        (ctx.state & (EWS_MAXIMIZED_HORIZONTALLY | EWS_MAXIMIZED_VERTICALLY)) ==
        (EWS_MAXIMIZED_HORIZONTALLY | EWS_MAXIMIZED_VERTICALLY);
    auto side = [maximized] (int space, int ext, int extra, bool borders,
                             bool shadows)
    {
        int outer = 0, inner = 0;
        if (maximized)
        {
            const auto parts = maximized_crop_parts(space, ext, extra);
            if (!shadows)
            {
                outer = parts.shadow;
            }

            if (!borders)
            {
                inner = parts.border;
            }
        }

        return std::pair<int, int>{outer, inner};
    };
    const auto cl = side(ws->left_space, ws->win_extents.left,
        ws->extra_extents.left, ctx.maximized_borders,
        ctx.maximized_shadows);
    const auto crp = side(ws->right_space, ws->win_extents.right,
        ws->extra_extents.right, ctx.maximized_borders,
        ctx.maximized_shadows);
    const auto ct = side(ws->top_space, ws->win_extents.top,
        ws->extra_extents.top, ctx.maximized_borders,
        ctx.maximized_shadows);
    const auto cb = side(ws->bottom_space, ws->win_extents.bottom,
        ws->extra_extents.bottom, ctx.maximized_borders,
        ctx.maximized_shadows);
    const int cw = w - cl.first - cl.second - crp.first - crp.second;
    const int ch = h - ct.first - ct.second - cb.first - cb.second;
    if ((cw <= 0) || (ch <= 0))
    {
        return nullptr;
    }

    // The engine always draws into the FULL-EXTENT surface (its
    // coordinates assume the frame origin is the shadow corner); the crop
    // is applied afterwards. The outer crop (shadow) is a simple size
    // reduction; the INNER crop (border) removes a band from the middle of
    // the side — the content below/right of it (titlebar, client) must
    // reflow, which the piecewise blit below performs.
    cairo_surface_t *full = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
        (int)(w * s),
        (int)(h * s));
    if (full == nullptr)
    {
        return nullptr;
    }

    // Build the emerald decor_t from the DecorContext. Value-initialization
    // ({} ) zeroes every member (floats to 0.0) — the portable C++ form of the
    // old `memset(&d, 0, sizeof(d))` (cppcheck: memsetClassFloat).
    decor_t d{};
    d.width  = ctx.width;
    d.height = ctx.height;
    d.client_width  = ctx.client_width;
    d.client_height = ctx.client_height;
    d.decorated     = ctx.decorated;
    d.active  = ctx.active;
    d.state   = to_wnck_state(ctx.state);
    d.actions = (WnckWindowActions)ctx.actions; // EWA_* bits verbatim (note above)
    d.fs = ctx.fs;
    std::memcpy(d.button_states, ctx.button_states, sizeof(d.button_states));
    std::memcpy(d.tobj_pos, ctx.tobj_pos, sizeof(d.tobj_pos));
    std::memcpy(d.tobj_size, ctx.tobj_size, sizeof(d.tobj_size));
    std::memcpy(d.tobj_item_pos, ctx.tobj_item_pos, sizeof(d.tobj_item_pos));
    std::memcpy(d.tobj_item_state, ctx.tobj_item_state, sizeof(d.tobj_item_state));
    std::memcpy(d.tobj_item_width, ctx.tobj_item_width, sizeof(d.tobj_item_width));
    std::memcpy(d.button_region, ctx.button_region, sizeof(d.button_region));
    std::memcpy(d.button_region_inact, ctx.button_region_inact,
        sizeof(d.button_region_inact));
    d.min_drawn_buttons_region = ctx.min_drawn_buttons_region;
    d.draw_only_buttons_region = ctx.draw_only_buttons_region;
    std::memcpy(d.button_last_drawn_state, ctx.button_last_drawn_state,
        sizeof(d.button_last_drawn_state));
    d.only_change_active = ctx.only_change_active;
    d.surface = full;
    d.buffer_surface = nullptr;

    cairo_t *cr = cairo_create(full);
    // Render at physical resolution: scale the logical draw coordinates into
    // the physical (w*s x h*s) surface. All engine formulas (shadow, bar,
    // title, buttons) stay in logical units; this single scale makes them land
    // at physical pixels. No-op when scale == 1.0.
    cairo_scale(cr, s, s);

    // The engine draws the frame at the FULL-EXTENT origin (its border/
    // titlebar/shadow formulas all assume the shadow corner is (0,0));
    // d.width/height are the full extents so the engine computes the
    // right/bottom edges correctly. The maximized crop is applied to the
    // finished surface below, not to the draw.
    d.width  = w;
    d.height = h;

    double y1 = ws->top_space - ws->win_extents.top;

    if (!d.draw_only_buttons_region)
    {
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_line_width(cr, 1.0);
        cairo_save(cr);
        draw_shadow_background(&d, cr);
        if (pixmap_theme_has_pixmaps(ts))
        {
            call_engine_draw_frame(&d, cr);
        } else
        {
            // No theme pixmaps: paint a plain rounded-rect frame so the
            // pixmap engine does not dereference NULL pixmap surfaces.
            double x1  = ws->left_space - ws->win_extents.left;
            double y1f = ws->top_space - ws->win_extents.top;
            double x2  = d.width - ws->right_space + ws->win_extents.right;
            double y2  = d.height - ws->bottom_space + ws->win_extents.bottom;
            cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 0.8);
            cairo_rectangle(cr, x1, y1f, x2 - x1, y2 - y1f);
            cairo_fill(cr);
        }

        cairo_restore(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        cairo_set_line_width(cr, 2.0);

        // Title text with halo (port of main.c:1807-1828).
        if (d.tobj_item_state[TBT_TITLE] != 3)
        {
            const frame_settings *fs = d.fs;
            PangoLayout *layout =
                pango_cairo_create_layout(cr);
            if (ws->font_desc != nullptr)
            {
                pango_layout_set_font_description(layout, ws->font_desc);
            }

            std::string title = ctx.title; // window title (set by the node)
            pango_layout_set_text(layout, title.c_str(), (int)title.size());

            pango_layout_set_alignment(layout,
                (PangoAlignment)ws->title_text_align);

            int pos   = get_real_pos(ws, TBT_TITLE, &d);
            double ty = y1 + 2.0 +
                (ws->titlebar_height - ws->text_height) / 2.0;

            // halo stroke
            cairo_set_source_rgba(cr, fs->text_halo.color.r,
                fs->text_halo.color.g,
                fs->text_halo.color.b,
                fs->text_halo.alpha);
            cairo_move_to(cr, pos, ty);
            pango_cairo_update_layout(cr, layout);
            pango_cairo_layout_path(cr, layout);
            cairo_stroke(cr);

            // text fill
            cairo_set_source_rgba(cr, fs->text.color.r, fs->text.color.g,
                fs->text.color.b, fs->text.alpha);
            cairo_move_to(cr, pos, ty);
            pango_cairo_show_layout(cr, layout);

            g_object_unref(layout);
        }

        // Icon (port of main.c:1830-1843): no icon in the headless bridge.
    }

    // Buttons (port of main.c:1998-2010; finding 43 adds the fade path —
    // ctx.button_fade_normal/hover carry the node's per-button crossfade
    // alphas).
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    draw_buttons_without_fade(&d, cr, y1, ctx.button_fade_normal,
        ctx.button_fade_hover);

    cairo_destroy(cr);

    // No crop (windowed, or both maximized options on): the engine's
    // full-extent surface IS the frame — done.
    if ((cl.first == 0) && (cl.second == 0) && (crp.first == 0) && (crp.second == 0) &&
        (ct.first == 0) && (ct.second == 0) && (cb.first == 0) && (cb.second == 0))
    {
        return full;
    }

    // Maximized crop (finding 19c). The final surface (cw × ch) is the
    // full-extent frame minus the OUTER bands (shadow spread, when shadows
    // are off) and the INNER bands (border part, when borders are off).
    // The engine drew with fixed full-extent coordinates, so the crop is a
    // piecewise copy of the 9 zones: left/client × (shadow/titlebar/client/
    // shadow). Each zone maps 1:1 from a full-extent region with a constant
    // offset; removing the inner band shifts the content right/below of it
    // (titlebar, client) back into the freed space — a reflow no single
    // translate can express (one translate left the border INSIDE the
    // shadow margin, or clipped the titlebar).
    cairo_surface_t *result = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
        (int)(cw * s),
        (int)(ch * s));
    if (result == nullptr)
    {
        cairo_surface_destroy(full);
        return nullptr;
    }

    cairo_t *crs = cairo_create(result);
    cairo_pattern_t *pat = cairo_pattern_create_for_surface(full);

    // Note on the pattern matrix: cairo samples the surface pattern at
    // M·user_point (see draw_shadow_background above), so copying the
    // full-extent region with origin (sx, sy) onto the destination origin
    // uses the POSITIVE translate (sx, sy).
    //
    // Scale: `full` and `result` are physical (logical * s). The zone
    // boundaries are computed in logical units below, so each coordinate is
    // multiplied by `s` here to sample/plot at physical pixels (1:1 copy).
    auto blit = [&] (double dx, double dy, double dw, double dh,
                     double sx, double sy)
    {
        if ((dw <= 0.0) || (dh <= 0.0))
        {
            return;
        }

        cairo_matrix_t m;
        cairo_matrix_init_translate(&m, sx * s, sy * s);
        cairo_save(crs);
        cairo_pattern_set_matrix(pat, &m);
        cairo_set_source(crs, pat);
        cairo_rectangle(crs, dx * s, dy * s, dw * s, dh * s);
        cairo_fill(crs);
        cairo_restore(crs);
    };

    // Shadow spread per side (maximized_crop_parts: shadow = space − ext
    // − extra) — the zone boundaries sit at the band edges in surface
    // coordinates (surface x + offset = full-extent x).
    const double sl = (double)maximized_crop_parts(ws->left_space,
        ws->win_extents.left, ws->extra_extents.left).shadow;
    const double sr = (double)maximized_crop_parts(ws->right_space,
        ws->win_extents.right, ws->extra_extents.right).shadow;
    const double st = (double)maximized_crop_parts(ws->top_space,
        ws->win_extents.top, ws->extra_extents.top).shadow;
    const double sb = (double)maximized_crop_parts(ws->bottom_space,
        ws->win_extents.bottom, ws->extra_extents.bottom).shadow;

    const double xl = sl - cl.first; // left band width
    const double xr = (double)cw - (sr - crp.first); // right band start
    const double yt = st - ct.first; // top band width
    const double yb = (double)ch - (sb - cb.first); // bottom band start
    const double oxl = cl.first; // left zone origin
    const double oxm = cl.first + cl.second; // client zone origin
    const double oxr = (double)w - (double)cw - crp.first;
    const double oyt = ct.first;
    const double oym = ct.first + ct.second;
    const double oyb = (double)h - (double)ch - cb.first;

    blit(0, 0, xl, yt, oxl, oyt);
    blit(xl, 0, xr - xl, yt, oxm, oyt);
    blit(xr, 0, (double)cw - xr, yt, oxr, oyt);
    blit(0, yt, xl, yb - yt, oxl, oym);
    blit(xl, yt, xr - xl, yb - yt, oxm, oym);
    blit(xr, yt, (double)cw - xr, yb - yt, oxr, oym);
    blit(0, yb, xl, (double)ch - yb, oxl, oyb);
    blit(xl, yb, xr - xl, (double)ch - yb, oxm, oyb);
    blit(xr, yb, (double)cw - xr, (double)ch - yb, oxr, oyb);

    cairo_pattern_destroy(pat);
    cairo_destroy(crs);
    cairo_surface_destroy(full);
    return result;
}
} // namespace wf::emeraldwf
