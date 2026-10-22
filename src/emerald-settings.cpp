/*
 * emerald-settings.cpp — ThemeSettings (part 02).
 *
 * Port of emerald's config loading:
 *  - defaults from third_party/emerald/defaults (main.c:5920-5990)
 *  - settings.ini core sections (main.c:5717-5746, load_settings)
 *  - theme.ini: LFACSS + engine load + load_engine_settings (main.c:5748-5770)
 *  - button PNGs via GdkPixbuf (main.c:5520-5717, load_buttons_image/glow)
 *  - copy_from_defaults_if_needed ported from libengine/emerald.c:23-64
 */

// titlebar.h button arrays (see emerald-engine.cpp for the rationale).
#define NEED_BUTTON_BISTATES
#define NEED_BUTTON_FILE_NAMES

#include "emerald-settings.hpp"
#include "emerald-engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <vector>

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

namespace fs = std::filesystem;

extern "C" {
#include <libengine.h> /* load_*_setting helpers used by the engines */
}

extern "C" void call_load_engine_settings(GKeyFile *f, window_settings *ws);

namespace wf::emeraldwf
{
bool emerald_engine_load(const std::string & name,
    const std::string & system_dir);
void emerald_engine_init_engine(window_settings *ws);

// port of main.c:250-262 (get_b_t_offset). Maps the B_T_* titlebar-object
// space into the B_* button/surface space. The surface arrays
// (ws->button_surface, ws->button_glow_surface) are indexed by B_* — where
// bistate pairs are interleaved (MAXIMIZE=1/RESTORE=2, SHADE=6/UNSHADE=7,
// ABOVE=8/UNABOVE=9, STICK=10/UNSTICK=11) — while B_T_* is a separate enum.
// Using the B_T index directly here made the MINIMIZE button draw the
// RESTORE surface (part 06: "minimize button icon is actually a restore").
int get_b_t_offset(gint b_t)
{
    switch (b_t)
    {
      case B_T_CLOSE:
        return B_CLOSE;

      case B_T_MAXIMIZE:
        return B_MAXIMIZE;

      case B_T_MINIMIZE:
        return B_MINIMIZE;

      case B_T_HELP:
        return B_HELP;

      case B_T_MENU:
        return B_MENU;

      case B_T_SHADE:
        return B_SHADE;

      case B_T_ABOVE:
        return B_ABOVE;

      case B_T_STICKY:
        return B_STICK;

      case B_T_SUPERMAXIMIZE:
        return B_SUPERMAXIMIZE;

      default:
        return B_COUNT;
    }
}
} // namespace wf::emeraldwf

namespace
{
// emerald's defaults (src/main.c:5920-5960)
void apply_emerald_defaults(window_settings *ws)
{
    ws->win_extents.left    = 6;
    ws->win_extents.top     = 4;
    ws->win_extents.right   = 6;
    ws->win_extents.bottom  = 6;
    ws->extra_extents.left  = 0;
    ws->extra_extents.top   = 0;
    ws->extra_extents.right = 0;
    ws->extra_extents.bottom = 0;
    ws->shadow_radius  = 11.5;
    ws->shadow_opacity = .8;
    ws->min_titlebar_height = 18;
    ws->double_click_action = DOUBLE_CLICK_SHADE;
    ws->middle_click_action = DOUBLE_CLICK_SEND_TO_BACK;
    ws->button_hover_cursor = 1;
    ws->button_offset  = 1;
    ws->button_hoffset = 1;
    ws->button_fade_step_duration = 50;
    ws->button_fade_num_steps     = 5;
    ws->blur_type = BLUR_TYPE_NONE;

    if (ws->tobj_layout != nullptr)
    {
        g_free(ws->tobj_layout);
    }

    ws->tobj_layout = g_strdup("I(30):T:N(2)X(2)C:");

    // frame settings: active
    ws->fs_act->text.color.r = 1.0;
    ws->fs_act->text.color.g = 1.0;
    ws->fs_act->text.color.b = 1.0;
    ws->fs_act->text.alpha   = 1.0;
    ws->fs_act->text_halo.color.r = 0.0;
    ws->fs_act->text_halo.color.g = 0.0;
    ws->fs_act->text_halo.color.b = 0.0;
    ws->fs_act->text_halo.alpha   = 0.2;
    ws->fs_act->button.color.r    = 1.0;
    ws->fs_act->button.color.g    = 1.0;
    ws->fs_act->button.color.b    = 1.0;
    ws->fs_act->button.alpha = 0.8;
    ws->fs_act->button_halo.color.r = 0.0;
    ws->fs_act->button_halo.color.g = 0.0;
    ws->fs_act->button_halo.color.b = 0.0;
    ws->fs_act->button_halo.alpha   = 0.2;

    // frame settings: inactive
    ws->fs_inact->text.color.r = 0.8;
    ws->fs_inact->text.color.g = 0.8;
    ws->fs_inact->text.color.b = 0.8;
    ws->fs_inact->text.alpha   = 0.8;
    ws->fs_inact->text_halo.color.r = 0.0;
    ws->fs_inact->text_halo.color.g = 0.0;
    ws->fs_inact->text_halo.color.b = 0.0;
    ws->fs_inact->text_halo.alpha   = 0.2;
    ws->fs_inact->button.color.r    = 0.8;
    ws->fs_inact->button.color.g    = 0.8;
    ws->fs_inact->button.color.b    = 0.8;
    ws->fs_inact->button.alpha = 0.8;
    ws->fs_inact->button_halo.color.r = 0.0;
    ws->fs_inact->button_halo.color.g = 0.0;
    ws->fs_inact->button_halo.color.b = 0.0;
    ws->fs_inact->button_halo.alpha   = 0.2;

    ws->round_top_left     = TRUE;
    ws->round_top_right    = TRUE;
    ws->round_bottom_left  = TRUE;
    ws->round_bottom_right = TRUE;
}

// port of libengine/emerald.c:23-64 (copy_from_defaults_if_needed),
// parameterized on the settings/theme paths and the defaults dirs.
//
// Review R8: `installed_theme_dir` is the installed default theme (install.sh
// ships it at <so-dir>/emeraldwf-defaults/theme, with the XDG user-dir
// fallback) — in the installed layout the build-tree-only `defaults_dir`
// resolves to nothing (the plugin runs dlopen'd into /usr/bin/wayfire), so
// the first-run theme seed was a silent no-op there. The theme is copied
// from the installed dir when it exists, the build-tree vendored defaults
// otherwise; settings.ini exists only in the latter (install.sh does not
// ship it — ~/.emerald/settings.ini is seeded by install.sh itself).
void copy_from_defaults_if_needed(const std::string & settings_path,
    const std::string & theme_dir,
    const std::string & defaults_dir,
    const std::string & installed_theme_dir)
{
    if (fs::exists(settings_path))
    {
        return;
    }

    const std::string default_settings =
        defaults_dir + "/settings.ini";
    if (fs::exists(default_settings))
    {
        std::ifstream in(default_settings, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());
        fs::create_directories(
            fs::path(settings_path).parent_path());
        std::ofstream out(settings_path, std::ios::binary);
        out << content;
    }

    if (fs::exists(theme_dir + "/theme.ini"))
    {
        return;
    }

    std::error_code ec;
    const bool have_installed = fs::is_directory(installed_theme_dir, ec);
    const std::string default_theme =
        have_installed ? installed_theme_dir : defaults_dir + "/theme";
    if (have_installed || fs::is_directory(default_theme, ec))
    {
        fs::create_directories(theme_dir);
        for (const auto & entry : fs::directory_iterator(default_theme))
        {
            if (!fs::is_regular_file(entry))
            {
                continue;
            }

            std::ifstream in(entry.path(), std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>());
            std::ofstream out(theme_dir + "/" +
                entry.path().filename().string(),
                std::ios::binary);
            out << content;
        }
    }
}

// Non-throwing __FILE__-relative root; falls back to the raw path on failure.
static fs::path file_relative_root()
{
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::current_path() / __FILE__, ec);
    if (ec)
    {
        p = fs::path(__FILE__);
    }

    return p;
}

// port of main.c:5490-5512 (titlebar_font_changed)
void titlebar_font_changed(window_settings *ws, const std::string & font_name)
{
    if (ws->font_desc != nullptr)
    {
        pango_font_description_free(ws->font_desc);
    }

    ws->font_desc = pango_font_description_from_string(
        font_name.c_str());

    PangoFontMap *font_map = pango_cairo_font_map_get_default();
    PangoContext *context  = pango_font_map_create_context(font_map);
    pango_context_set_font_description(context, ws->font_desc);
    PangoLanguage *lang = pango_context_get_language(context);
    PangoFontMetrics *metrics = pango_context_get_metrics(
        context, ws->font_desc, lang);

    ws->text_height =
        PANGO_PIXELS(pango_font_metrics_get_ascent(metrics) +
            pango_font_metrics_get_descent(metrics));

    ws->titlebar_height = ws->text_height;
    if (ws->titlebar_height < ws->min_titlebar_height)
    {
        ws->titlebar_height = ws->min_titlebar_height;
    }

    pango_font_metrics_unref(metrics);
    // Review S2: the context is per-call (upstream emerald reuses a
    // persistent ws->pango_context; the port made it per-call without the
    // matching free) — unref it, one leak per theme load otherwise.
    g_object_unref(context);
}

// port of main.c:259-290 (get_b_offset)
int get_b_offset(gint y)
{
    static int boffset[B_COUNT + 1];
    static bool offset_init = false;
    if (!offset_init)
    {
        int b_t = 0;
        for (int i = 0; i < B_COUNT; i++)
        {
            boffset[i] = b_t;
            if (btbistate[b_t])
            {
                boffset[i + 1] = b_t;
                i++;
            }

            b_t++;
        }

        offset_init = true;
    }

    return boffset[y];
}

// port of main.c:5514-5562 (load_buttons_image), parameterized on theme dir
void load_buttons_image(window_settings *ws, const std::string & theme_dir,
    gint y)
{
    int rel_button = get_b_offset(y);

    if (ws->button_array[y] != NULL)
    {
        cairo_surface_destroy(ws->button_array[y]);
    }

    ws->button_array[y] = NULL;

    std::string file = theme_dir + "/buttons." + std::string(b_types[y]) + ".png";
    int pix_width = 0, pix_height = 0;

    if (fs::exists(file))
    {
        GError *err = nullptr;
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(file.c_str(), &err);
        if (pixbuf)
        {
            int width  = gdk_pixbuf_get_width(pixbuf);
            int height = gdk_pixbuf_get_height(pixbuf);
            cairo_surface_t *surface = cairo_image_surface_create(
                CAIRO_FORMAT_ARGB32, width, height);
            cairo_t *cr = cairo_create(surface);
            gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(cr);
            cairo_destroy(cr);
            ws->button_array[y] = surface;
            pix_width  = width / S_COUNT;
            pix_height = height;
            g_object_unref(pixbuf);
        }

        if (err)
        {
            g_error_free(err);
        }
    }

    if (ws->button_array[y] == NULL)
    {
        pix_width  = 16;
        pix_height = 16;
        // create an empty surface
        ws->button_array[y] = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, pix_width * S_COUNT, pix_height);
    }

    ws->c_icon_size[rel_button].w = pix_width;
    ws->c_icon_size[rel_button].h = pix_height;

    for (int i = 0; i < S_COUNT; i++)
    {
        if (ws->button_surface[i + y * S_COUNT] != NULL)
        {
            cairo_surface_destroy(ws->button_surface[i + y * S_COUNT]);
        }

        ws->button_surface[i + y * S_COUNT] =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pix_width, pix_height);
        cairo_t *cr = cairo_create(ws->button_surface[i + y * S_COUNT]);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_translate(cr, -i * pix_width, 0);
        cairo_set_source_surface(cr, ws->button_array[y], 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);
    }
}

// port of main.c:5564-5695 (load_buttons_glow_images), parameterized on theme dir
void load_buttons_glow_images(window_settings *ws, const std::string & theme_dir)
{
    int pix_width = 0, pix_height = 0;
    int pix_width2 = 0, pix_height2 = 0;
    // buttons.glow.png is only designed for 12 elements
    gint b_glow_count = 12;

    if (ws->button_glow_array != NULL)
    {
        cairo_surface_destroy(ws->button_glow_array);
    }

    ws->button_glow_array = NULL;
    if (ws->button_inactive_glow_array != NULL)
    {
        cairo_surface_destroy(ws->button_inactive_glow_array);
    }

    ws->button_inactive_glow_array = NULL;

    auto load_glow = [theme_dir] (const char *name, cairo_surface_t **out,
                                  int & pw, int & ph)
    {
        std::string path = std::string(theme_dir) + "/buttons." + name + ".png";
        if (!fs::exists(path))
        {
            return;
        }

        GError *err = nullptr;
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path.c_str(), &err);
        if (pixbuf)
        {
            int width  = gdk_pixbuf_get_width(pixbuf);
            int height = gdk_pixbuf_get_height(pixbuf);
            cairo_surface_t *surface = cairo_image_surface_create(
                CAIRO_FORMAT_ARGB32, width, height);
            cairo_t *cr = cairo_create(surface);
            gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(cr);
            cairo_destroy(cr);
            *out = surface;
            pw   = width / 12;
            ph   = height;
            g_object_unref(pixbuf);
        }

        if (err)
        {
            g_error_free(err);
        }
    };

    if (ws->use_button_glow)
    {
        load_glow("glow", &ws->button_glow_array, pix_width, pix_height);
    }

    if (ws->use_button_inactive_glow)
    {
        load_glow("inactive_glow", &ws->button_inactive_glow_array,
            pix_width2, pix_height2);
    }

    if ((ws->button_glow_array != NULL) &&
        (ws->button_inactive_glow_array != NULL))
    {
        if ((pix_width != pix_width2) || (pix_height != pix_height2))
        {
            // The cstyleCast below is inside glib's g_warning() macro
            // expansion (a --force configuration), not in this call.
            // cppcheck-suppress cstyleCast
            g_warning(
                "Choose same size glow images for active and inactive windows."
                "\nInactive glow image is scaled for now.");

            cairo_surface_t *tmp_surface = cairo_image_surface_create(
                CAIRO_FORMAT_ARGB32, pix_width * 12, pix_height);
            cairo_t *tmp_cr = cairo_create(tmp_surface);
            cairo_scale(tmp_cr, pix_width / static_cast<double>(pix_width2),
                pix_height / static_cast<double>(pix_height2));
            cairo_set_source_surface(tmp_cr, ws->button_inactive_glow_array, 0, 0);
            cairo_pattern_set_extend(cairo_get_source(tmp_cr), CAIRO_EXTEND_PAD);
            cairo_paint(tmp_cr);
            cairo_destroy(tmp_cr);

            cairo_surface_destroy(ws->button_inactive_glow_array);
            ws->button_inactive_glow_array = tmp_surface;
        }
    } else
    {
        if (ws->button_glow_array == NULL)
        {
            if (ws->button_inactive_glow_array != NULL)
            {
                pix_width  = pix_width2;
                pix_height = pix_height2;
            } else
            {
                pix_width  = 16;
                pix_height = 16;
            }
        }

        if ((ws->button_glow_array == NULL) && ws->use_button_glow)
        {
            ws->button_glow_array = cairo_image_surface_create(
                CAIRO_FORMAT_ARGB32, pix_width * b_glow_count, pix_height);
        }

        if ((ws->button_inactive_glow_array == NULL) &&
            ws->use_button_inactive_glow)
        {
            ws->button_inactive_glow_array = cairo_image_surface_create(
                CAIRO_FORMAT_ARGB32, pix_width * b_glow_count, pix_height);
        }
    }

    ws->c_glow_size.w = pix_width;
    ws->c_glow_size.h = pix_height;

    auto split_glow = [ws] (cairo_surface_t *array, cairo_surface_t **out,
                            int pw, int ph)
    {
        for (int i = 0; i < B_COUNT; i++)
        {
            if (out[i] != NULL)
            {
                cairo_surface_destroy(out[i]);
            }

            out[i] = NULL;
            if (i < 12)
            {
                out[i] = cairo_image_surface_create(
                    CAIRO_FORMAT_ARGB32, pw, ph);
                cairo_t *cr = cairo_create(out[i]);
                cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
                cairo_translate(cr, -i * pw, 0);
                cairo_set_source_surface(cr, array, 0, 0);
                cairo_paint(cr);
                cairo_destroy(cr);
            }
        }
    };
    if (ws->use_button_glow)
    {
        split_glow(ws->button_glow_array, ws->button_glow_surface,
            pix_width, pix_height);
    }

    if (ws->use_button_inactive_glow)
    {
        split_glow(ws->button_inactive_glow_array,
            ws->button_inactive_glow_surface, pix_width, pix_height);
    }
}

void load_button_image_setting(window_settings *ws, const std::string & theme_dir)
{
    for (int i = 0; i < B_COUNT; i++)
    {
        load_buttons_image(ws, theme_dir, i);
    }

    if (ws->use_button_glow || ws->use_button_inactive_glow)
    {
        load_buttons_glow_images(ws, theme_dir);
    }
}
} // namespace (anonymous)

// ── Shadow texture generation ───────────────────────────────────────────────
//
// Port of emerald's XRender shadow generation (original main.c:5280-5412):
// the original fills the window silhouette (a sharp white rectangle — the
// decorations are drawn and then cleared), blurs it with an XRender
// convolution filter (two-pass Gaussian, kernel width 2*radius + 2), shifts
// the result by (shadow_offset_x, shadow_offset_y) and stores it in
// ws->large_shadow_surface (+ ws->shadow_pattern, tiled by
// draw_shadow_background). XRender filters don't exist on Wayland, so the
// blur is computed directly as a two-pass box blur (the box matches the
// Gaussian to visual precision). Improvement over the original: the
// silhouette is the engine-rendered frame's alpha channel (reference client
// 400x300), so the shadow follows the window's actual shape — the theme's
// per-corner rounding, border width, semi-transparency. The original's
// sharp rectangle only approximates that; themes whose corner radius
// exceeds the blur scale show square shadow corners in the original.
//
// The content size matches the original exactly (the frame content region,
// INCLUDING the shadow margin): left_space + left_corner_space + 1 +
// right_corner_space + right_space, etc. draw_shadow_background() tiles that
// texture with the same left/right/top/bottom splits.
// (namespace-scope pair: create_large_shadow_surface is declared in
// emerald-settings.hpp inside wf::emeraldwf, so both live there.)
namespace wf
{
namespace emeraldwf
{
// port of main.c:5183-5275 (update_shadow) minus the XRender parts.
// Computes the derived space fields used by the frame size formula and the
// shadow draw path. Exported (was in the TU-local anonymous namespace) so
// tests can drive the derived shadow state directly.
void update_shadow(frame_settings *fs)
{
    window_settings *ws = fs->ws;

    int size = 0;
    if (ws->shadow_radius > 0.0)
    {
        size = static_cast<int>(ws->shadow_radius * 2 + 2);
    }

    if ((ws->shadow_radius <= 0.0) && (ws->shadow_offset_x == 0) &&
        (ws->shadow_offset_y == 0))
    {
        size = 0;
    }

    size = size / 2;

    ws->left_space   = ws->win_extents.left + size - ws->shadow_offset_x;
    ws->right_space  = ws->win_extents.right + size + ws->shadow_offset_x;
    ws->top_space    = ws->win_extents.top + size - ws->shadow_offset_y;
    ws->bottom_space = ws->win_extents.bottom + size + ws->shadow_offset_y;

    ws->left_space   = std::max(ws->win_extents.left, ws->left_space);
    ws->right_space  = std::max(ws->win_extents.right, ws->right_space);
    ws->top_space    = std::max(ws->win_extents.top, ws->top_space);
    ws->bottom_space = std::max(ws->win_extents.bottom, ws->bottom_space);

    ws->shadow_left_space   = std::max(0, size - ws->shadow_offset_x);
    ws->shadow_right_space  = std::max(0, size + ws->shadow_offset_x);
    ws->shadow_top_space    = std::max(0, size - ws->shadow_offset_y);
    ws->shadow_bottom_space = std::max(0, size + ws->shadow_offset_y);

    ws->shadow_left_corner_space   = std::max(0, size + ws->shadow_offset_x);
    ws->shadow_right_corner_space  = std::max(0, size - ws->shadow_offset_x);
    ws->shadow_top_corner_space    = std::max(0, size + ws->shadow_offset_y);
    ws->shadow_bottom_corner_space = std::max(0, size - ws->shadow_offset_y);

    const int CORNER_REDUCTION = 3;
    ws->left_corner_space = std::max(0, ws->shadow_left_corner_space -
        CORNER_REDUCTION);
    ws->right_corner_space = std::max(0, ws->shadow_right_corner_space -
        CORNER_REDUCTION);
    ws->top_corner_space = std::max(0, ws->shadow_top_corner_space -
        CORNER_REDUCTION);
    ws->bottom_corner_space = std::max(0, ws->shadow_bottom_corner_space -
        CORNER_REDUCTION);

    ws->normal_top_corner_space = std::max(0, ws->top_corner_space -
        ws->titlebar_height);
    ws->switcher_top_corner_space = std::max(0, ws->top_corner_space -
        SWITCHER_TOP_EXTRA);
    ws->switcher_bottom_corner_space = std::max(0, ws->bottom_corner_space -
        SWITCHER_SPACE);

    // The shadow texture itself is generated by create_large_shadow_surface()
    // (called from ThemeSettings::load after this), so the pattern-based draw
    // path in emerald-engine.cpp gets real shadow content.
}

void create_large_shadow_surface(ThemeSettings & ts)
{
    window_settings *ws = ts.ws();

    // destroy previous texture (theme reload)
    if (ws->large_shadow_surface != nullptr)
    {
        cairo_surface_destroy(ws->large_shadow_surface);
        ws->large_shadow_surface = nullptr;
    }

    if (ws->shadow_pattern != nullptr)
    {
        cairo_pattern_destroy(ws->shadow_pattern);
        ws->shadow_pattern = nullptr;
    }

    // "no shadow" path (matches emerald's `if (size <= 0) return`).
    if ((ws->shadow_radius <= 0.0) && (ws->shadow_offset_x == 0) &&
        (ws->shadow_offset_y == 0))
    {
        return;
    }

    // The texture IS the reference frame's shadow field: it is the blurred,
    // +offset-shifted window silhouette over the FULL reference frame
    // (d_width x d_height = the frame size), with the silhouette (the window
    // as the engine draws it) at the window's actual position in the frame,
    // [fw_l, fw_l+fw_w) x [fw_t, fw_t+fw_h). draw_shadow_background maps
    // texture origin to frame origin and tiles: the four corner regions are
    // sampled 1:1 from the texture's edges (so the texture's corner content
    // IS the frame's corner shadow field, dense edge exactly the offset away
    // from the window's outer edge, fade completing within the reserved
    // spread), and the four middle bands sample one constant column/row
    // (deep in the silhouette's core, where the field is uniform along the
    // band). Any other sliver placement misaligns the 1:1 strips: the
    // original emerald used [shadow_left, d_width-shadow_right) with
    // d_width = shadow_left + shadow_left_corner + 1 + shadow_right +
    // shadow_right_corner, which places the sliver's right/bottom edges
    // win_extents.right/bottom px PAST the corner split lines — the visible
    // symptom is a dense "box" at the bottom-right corner (the fade tail
    // painted where the true shadow is already zero) and the same shift on
    // the right/bottom bands.
    const int client_w = 400, client_h = 300;
    DecorContext ctx;
    ctx.client_width  = client_w;
    ctx.client_height = client_h;
    ctx.width  = frame_width(*ws, client_w);
    ctx.height = frame_height(*ws, client_w, client_h);
    ctx.active = true;
    ctx.fs     = ts.fs_active();

    const int d_width  = ctx.width;
    const int d_height = ctx.height;
    if ((d_width < 1) || (d_height < 1))
    {
        return;
    }

    cairo_surface_t *surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, d_width, d_height);

    const int size = static_cast<int>(ws->shadow_radius * 2 + 2) / 2;

    // The silhouette is the window as the engine ACTUALLY draws it — border
    // width, per-corner rounding (the engine's corner radius + round flags),
    // semi-transparency. Render the frame at a LARGE reference client size so
    // the theme's corner radius is a small fraction of the window (top corners
    // round, bottom stay square per the per-corner flags). The frame's window
    // box (border + client) maps 1:1 onto the texture's window region —
    // preserving the per-corner shape — while the client (transparent in the
    // render; the real window composites there) is forced opaque, as the
    // original's white rectangle did.
    std::vector<float> buf((size_t)d_width * d_height, 0.0f);
    {
        // NULL the shadow so the reference render contains ONLY the window
        // (no shadow background to contaminate the silhouette alpha).
        cairo_surface_t *saved_tex = ws->large_shadow_surface;
        cairo_pattern_t *saved_pat = ws->shadow_pattern;
        ws->large_shadow_surface = nullptr;
        ws->shadow_pattern = nullptr;
        cairo_surface_t *frame = emeraldwf_render_frame(ts, ctx);
        ws->large_shadow_surface = saved_tex;
        ws->shadow_pattern = saved_pat;
        if (frame)
        {
            const unsigned char *fd =
                static_cast<const unsigned char*>(cairo_image_surface_get_data(frame));
            const int fst = cairo_image_surface_get_stride(frame);
            // Frame's window (border + client) region — the engine's
            // decoration box (legacy.c engine_draw_frame): the window box is
            // inset from the frame edges by left_space - win_extents.left
            // (= the pure shadow margin; 0 when offset >= radius on that
            // side), and the per-corner rounding is drawn at the box's own
            // corners, NOT at (left_space, top_space).
            const int fw_l = ws->left_space - ws->win_extents.left;
            const int fw_t = ws->top_space - ws->win_extents.top;
            const int fw_w = ctx.width - ws->left_space - ws->right_space +
                ws->win_extents.left + ws->win_extents.right;
            const int fw_h = ctx.height - ws->top_space - ws->bottom_space +
                ws->win_extents.top + ws->win_extents.bottom;
            // Texture's window region: the sliver at the window's position in
            // the frame, 1:1 (no collapsing), see the d_width comment.
            const int tw_l = fw_l;
            const int tw_t = fw_t;
            const int tw_w = fw_w;
            const int tw_h = fw_h;
            // Border widths: win_extents are the border extents.
            const int b_l    = ws->win_extents.left;
            const int b_r    = ws->win_extents.right;
            const int b_t    = ws->win_extents.top;
            const int b_b    = ws->win_extents.bottom;
            const int mid_w  = tw_w - b_l - b_r;  // texture client width
            const int mid_h  = tw_h - b_t - b_b;  // texture titlebar+client height
            const int fmid_w = fw_w - b_l - b_r; // frame client width
            const int fmid_h = fw_h - b_t - b_b; // frame titlebar+client height
            for (int y = 0; y < d_height; y++)
            {
                for (int x = 0; x < d_width; x++)
                {
                    // Outside the window region: the shadow margin
                    // (transparent; the blur fills it).
                    if ((x < tw_l) || (x >= tw_l + tw_w) ||
                        (y < tw_t) || (y >= tw_t + tw_h))
                    {
                        continue;
                    }

                    const int lx = x - tw_l; // position within the window region
                    const int ly = y - tw_t;
                    int fx, fy;
                    // X: left border 1:1, client scaled, right border 1:1.
                    if (lx < b_l)
                    {
                        fx = fw_l + lx;
                    } else if (lx < b_l + mid_w)
                    {
                        // mid_w == fmid_w (tw_w == fw_w), so the single guard
                        // is enough (cppcheck: knownConditionTrueFalse).
                        fx = (mid_w > 0) ?
                            fw_l + b_l +
                            static_cast<int>(static_cast<double>(lx - b_l) * fmid_w / mid_w) :
                            fw_l + b_l;
                    } else
                    {
                        fx = fw_l + fw_w - (tw_w - lx);
                    }

                    // Y: top border 1:1, titlebar+client scaled, bottom 1:1.
                    if (ly < b_t)
                    {
                        fy = fw_t + ly;
                    } else if (ly < b_t + mid_h)
                    {
                        // mid_h == fmid_h (tw_h == fw_h), so the single guard
                        // is enough (cppcheck: knownConditionTrueFalse).
                        fy = (mid_h > 0) ?
                            fw_t + b_t +
                            static_cast<int>(static_cast<double>(ly - b_t) * fmid_h / mid_h) :
                            fw_t + b_t;
                    } else
                    {
                        fy = fw_t + fw_h - (tw_h - ly);
                    }

                    float a = fd[fy * fst + fx * 4 + 3] / 255.0f;
                    // The window interior (client + right/bottom borders) is
                    // transparent in the render — force it opaque, as the
                    // original's white rectangle did. This also keeps the
                    // bottom corners square (they're forced, not frame alpha).
                    if ((lx >= b_l) && (ly >= b_t))
                    {
                        a = 1.0f;
                    }

                    buf[(size_t)y * d_width + x] = a;
                }
            }

            cairo_surface_destroy(frame);
        } else
        {
            // Fallback: the original's sharp white rectangle.
            const int sx = ws->shadow_left_space;
            const int sy = ws->shadow_top_space;
            const int sw = d_width - ws->shadow_left_space -
                ws->shadow_right_space;
            const int sh = d_height - ws->shadow_top_space -
                ws->shadow_bottom_space;
            for (int y = sy; y < sy + sh; y++)
            {
                for (int x = sx; x < sx + sw; x++)
                {
                    buf[(size_t)y * d_width + x] = 1.0f;
                }
            }
        }
    }

    // Two-pass box blur of the silhouette, kernel width 2*size = 2*(r+1) —
    // the original's Gaussian kernel width (create_gaussian_kernel:
    // alpha*2 + 2 with alpha = radius; size = width/2). Moving sums,
    // zero-padded at the texture edges (the original's XRender clipping
    // does the same). A box kernel matches the Gaussian to visual
    // precision; ~50% coverage on the silhouette boundary, full `size` px
    // inside, zero `size` px outside. Because the silhouette carries the
    // theme's per-corner rounding, the shadow corners follow the window's
    // shape (the original only approximates that: its sharp rectangle
    // rounds at blur scale, which diverges for themes whose corner radius
    // exceeds the blur scale).
    //
    // The blurred field is computed over the EXTENDED range
    // [-half, d+half) in each axis (zero-padded): the +offset shift below
    // samples it `offset` px outside the texture edges (the fade tail past
    // the top/left edges when offset < size) — those box sums are
    // well-defined and must NOT be clipped to zero.
    const int W    = 2 * size;
    const int half = W / 2;
    const float inv_W = 1.0f / (float)W;
    const int ew = d_width + 2 * half; // extended width
    const int eh = d_height + 2 * half; // extended height
    std::vector<float> tmp((size_t)d_height * ew, 0.0f);
    for (int y = 0; y < d_height; y++)
    {
        const float *b = buf.data() + (size_t)y * d_width;
        float *t = tmp.data() + (size_t)y * ew;
        // Window [x-half, x+half] for x in [-half, d_width+half), zero-padded.
        // d_width >= 1 here (early return above for d_width < 1), so the old
        // `if (d_width > 0)` guard was always true (cppcheck:
        // knownConditionTrueFalse).
        float sum = b[0]; // window at x = -half: [−2half, 0] ∩ [0, d_width) = {0}
        for (int x = -half; x < d_width + half; x++)
        {
            t[x + half] = sum * inv_W;
            const int out = x - half;
            if ((out >= 0) && (out < d_width))
            {
                sum -= b[out];
            }

            const int in = x + half + 1;
            if ((in >= 0) && (in < d_width))
            {
                sum += b[in];
            }
        }
    }

    // shadow_color is stored as 16-bit X11 components (load_shadow_color
    // _setting: c.red * 65536) — normalize to [0,1] by 65535, not 255
    // (dividing by 255 clamps every real color to white).
    const double cr_ = ws->shadow_color[0] / 65535.0;
    const double cg  = ws->shadow_color[1] / 65535.0;
    const double cb  = ws->shadow_color[2] / 65535.0;
    const double opacity = ws->shadow_opacity;

    // Vertical pass (produces the fully blurred silhouette in `blurred`).
    std::vector<float> blurred((size_t)eh * ew, 0.0f);
    for (int x = 0; x < ew; x++)
    {
        float sum = 0.0f;
        if (d_height > 0)
        {
            sum = tmp[x]; // window at y = -half: [−2half, 0] ∩ [0, d_height) = {0}
        }

        for (int y = -half; y < d_height + half; y++)
        {
            blurred[(size_t)(y + half) * ew + x] = sum * inv_W;
            const int out = y - half;
            if ((out >= 0) && (out < d_height))
            {
                sum -= tmp[(size_t)out * ew + x];
            }

            const int in = y + half + 1;
            if ((in >= 0) && (in < d_height))
            {
                sum += tmp[(size_t)in * ew + x];
            }
        }
    }

    // Write (premultiplied) + shift by the offset. The original shifts the
    // blurred silhouette by (shadow_offset_x, shadow_offset_y)
    // (set_picture_transform) — net: a drop shadow falling to +x/+y. Apply
    // the shift here: texture(x, y) samples the blurred silhouette at
    // (x - offset_x, y - offset_y) in the EXTENDED field (negative
    // pre-shift indices are the zero-padded box sums, not "out of range").
    unsigned char *data = cairo_image_surface_get_data(surface);
    const int stride    = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < d_height; y++)
    {
        for (int x = 0; x < d_width; x++)
        {
            const int rx = x - ws->shadow_offset_x + half;
            const int ry = y - ws->shadow_offset_y + half;
            if ((rx < 0) || (ry < 0) || (rx >= ew) || (ry >= eh))
            {
                continue; // only for |offset| beyond the extended range
            }

            const double cov = blurred[(size_t)ry * ew + rx];
            if (cov > 0.0)
            {
                // Cairo ARGB32 is PREMULTIPLIED: color channels carry
                // color * alpha. (Cairo fills premultiply internally; raw
                // writes must do it explicitly.)
                const double A = (cov > 1.0 ? 1.0 : cov) * opacity;
                // The alpha (and the premultiplied color) must be CLAMPED to
                // [0,255]: casting a double to unsigned char WRAPS (mod 256),
                // so A > 1 (opacity > 1 / dense coverage) would wrap the alpha
                // 0->255->0->255... instead of saturating to a solid 255 core.
                // That wrap is what produced the banded "layers".
                double af = A * 255.0 + 0.5;
                if (af > 255.0)
                {
                    af = 255.0;
                } else if (af < 0.0)
                {
                    af = 0.0;
                }

                const unsigned char ab = (unsigned char)af;
                // Leave color bytes at 0 for transparent pixels (the
                // original's zero-initialized surface): keeps the raw
                // surface byte-identical to its PNG roundtrip
                // (surfaces_equal compares all 4 bytes).
                if (ab != 0)
                {
                    unsigned char *px = data + y * stride + 4 * x;
                    double rf = cb * A * 255.0 + 0.5;
                    double gf = cg * A * 255.0 + 0.5;
                    double bf = cr_ * A * 255.0 + 0.5;
                    if (rf > 255.0)
                    {
                        rf = 255.0;
                    }

                    if (gf > 255.0)
                    {
                        gf = 255.0;
                    }

                    if (bf > 255.0)
                    {
                        bf = 255.0;
                    }

                    px[0] = (unsigned char)bf;
                    px[1] = (unsigned char)gf;
                    px[2] = (unsigned char)rf;
                    px[3] = ab;
                }
            }
        }
    }

    // Raw data writes are invisible to cairo's compositing until this
    // (cairo 1.14+) — without it the texture tiles as fully transparent.
    cairo_surface_mark_dirty(surface);

    ws->large_shadow_surface = surface;
    ws->shadow_pattern = cairo_pattern_create_for_surface(surface);
    // NEAREST, as in the original (the texture is pre-blurred; nearest
    // sampling keeps the 9-region tiling crisp at the split lines).
    cairo_pattern_set_filter(ws->shadow_pattern, CAIRO_FILTER_NEAREST);
}
} // namespace emeraldwf
} // namespace wf

using namespace wf::emeraldwf;

ThemeSettings::ThemeSettings()
{
    ws_     = static_cast<window_settings*>(g_malloc0(sizeof(window_settings)));
    fs_act_ = static_cast<frame_settings*>(g_malloc0(sizeof(frame_settings)));
    fs_inact_     = static_cast<frame_settings*>(g_malloc0(sizeof(frame_settings)));
    fs_act_->ws   = ws_;
    fs_inact_->ws = ws_;
    ws_->fs_act   = fs_act_;
    ws_->fs_inact = fs_inact_;
    ws_->pango_context = nullptr;
    ws_->font_desc     = nullptr;

    apply_emerald_defaults(ws_);
}

ThemeSettings::ThemeSettings(ThemeSettings && other) noexcept :
    ws_(other.ws_), fs_act_(other.fs_act_), fs_inact_(other.fs_inact_),
    engine_(std::move(other.engine_)), valid_(other.valid_),
    settings_path_(std::move(other.settings_path_)),
    theme_dir_(std::move(other.theme_dir_))
{
    other.ws_     = nullptr;
    other.fs_act_ = nullptr;
    other.fs_inact_ = nullptr;
    other.valid_    = false;
}

ThemeSettings& ThemeSettings::operator =(ThemeSettings && other) noexcept
{
    if (this != &other)
    {
        // Release current resources.
        if (ws_ != nullptr)
        {
            emerald_engine_fini_engine(ws_);
            g_free(ws_);
        }

        if (fs_act_ != nullptr)
        {
            g_free(fs_act_);
        }

        if (fs_inact_ != nullptr)
        {
            g_free(fs_inact_);
        }

        ws_     = other.ws_;
        fs_act_ = other.fs_act_;
        fs_inact_ = other.fs_inact_;
        engine_   = std::move(other.engine_);
        valid_    = other.valid_;
        settings_path_ = std::move(other.settings_path_);
        theme_dir_     = std::move(other.theme_dir_);

        other.ws_     = nullptr;
        other.fs_act_ = nullptr;
        other.fs_inact_ = nullptr;
        other.valid_    = false;
    }

    return *this;
}

ThemeSettings::~ThemeSettings()
{
    if (ws_ == nullptr)
    {
        return;
    }

    for (int i = 0; i < S_COUNT * B_COUNT; i++)
    {
        if (ws_->button_surface[i] != NULL)
        {
            cairo_surface_destroy(ws_->button_surface[i]);
        }
    }

    for (int i = 0; i < B_COUNT; i++)
    {
        if (ws_->button_array[i] != NULL)
        {
            cairo_surface_destroy(ws_->button_array[i]);
        }
    }

    if (ws_->button_glow_array != NULL)
    {
        cairo_surface_destroy(ws_->button_glow_array);
    }

    if (ws_->button_inactive_glow_array != NULL)
    {
        cairo_surface_destroy(ws_->button_inactive_glow_array);
    }

    for (int i = 0; i < B_COUNT; i++)
    {
        if (ws_->button_glow_surface[i] != NULL)
        {
            cairo_surface_destroy(ws_->button_glow_surface[i]);
        }

        if (ws_->button_inactive_glow_surface[i] != NULL)
        {
            cairo_surface_destroy(ws_->button_inactive_glow_surface[i]);
        }
    }

    if (ws_->tobj_layout != nullptr)
    {
        g_free(ws_->tobj_layout);
    }

    if (ws_->font_desc != nullptr)
    {
        pango_font_description_free(ws_->font_desc);
    }

    if (ws_->large_shadow_surface != nullptr)
    {
        cairo_surface_destroy(ws_->large_shadow_surface);
    }

    if (ws_->shadow_pattern != nullptr)
    {
        cairo_pattern_destroy(ws_->shadow_pattern);
    }

    // Tear down the engine's per-workspace state before freeing ws_.
    // fini_engine frees engine_fs / engine_fs_inact owned by the engine.
    emerald_engine_fini_engine(ws_);

    g_free(ws_);
    g_free(fs_act_);
    g_free(fs_inact_);
}

bool ThemeSettings::load(const std::string & settings_ini,
    const std::string & theme_dir,
    const std::string & engine_override)
{
    settings_path_ = settings_ini;
    theme_dir_     = theme_dir;

    // vendored defaults dir: <project>/third_party/emerald/defaults.
    // The plugin is expected to be run with the tree layout, so resolve
    // relative to the executable's directory (libdir/wayfire).
    std::string defaults_dir;
    {
        char exepath[4096] = {0};
        ssize_t len = readlink("/proc/self/exe", exepath, sizeof(exepath) - 1);
        if (len > 0)
        {
            std::string p(exepath, len);
            // .../emerald-wf/build/src/emeraldwf -> 3 levels up
            defaults_dir = fs::path(p).parent_path().parent_path()
                .parent_path() / "third_party" / "emerald" /
                "defaults";
        }
    }
    if (defaults_dir.empty() || !fs::is_directory(defaults_dir))
    {
        defaults_dir = file_relative_root().parent_path()
            .parent_path()
            .parent_path() / "third_party" / "emerald" /
            "defaults";
    }

    // The installed-defaults theme dir — Review R8: the first-run seed must
    // find it in the installed layout, where the build-tree-only
    // defaults_dir above reaches neither the vendored defaults nor the
    // installed copy. Same resolution the engine-mismatch path uses below:
    // 1. <so-dir>/emeraldwf-defaults/theme   → installed (install.sh)
    // 2. <XDG>/emeraldwf/defaults/theme      → user-dir fallback
    // 3. <project>/third_party/emerald/defaults/theme → build tree
    const std::string default_theme_dir =
        [&]
    {
        auto exists_dir = [] (const fs::path & p)
        {
            std::error_code ec;
            return fs::is_directory(p, ec);
        };
        fs::path so_dir;
        {
            static int in_so = 0; // lives in THIS .so's data segment
            Dl_info info;
            if (dladdr(reinterpret_cast<void*>(&in_so), &info) &&
                (info.dli_fname != nullptr))
            {
                so_dir = fs::path(info.dli_fname).parent_path();
            }
        }
        if (!so_dir.empty())
        {
            fs::path p = so_dir / "emeraldwf-defaults" / "theme";
            if (exists_dir(p))
            {
                return p.string();
            }
        }

        const char *xdg = std::getenv("XDG_DATA_HOME");
        const fs::path user_dir =
            (xdg != nullptr && xdg[0] != '\0') ?
            fs::path(xdg) :
            fs::path(std::string(g_get_home_dir()) +
                "/.local/share");
        fs::path p = user_dir / "emeraldwf" / "defaults" / "theme";
        if (exists_dir(p))
        {
            return p.string();
        }

        return (file_relative_root().parent_path()
            .parent_path()
            .parent_path() / "third_party" / "emerald" /
            "defaults" / "theme")
                   .string();
    }();

    // Review R8: seed ~/.emerald from the defaults on first run — in BOTH
    // layouts. settings.ini exists only in the build tree's vendored
    // defaults; the theme dir comes from the installed copy when present
    // (install.sh ships it) and the build tree otherwise.
    copy_from_defaults_if_needed(settings_ini, theme_dir, defaults_dir,
        default_theme_dir);

    // Part 08 (final): emeraldwf reads NO keys from settings.ini. The file
    // is kept in emerald's format for compatibility (seeding, X11 emerald
    // interop, .emerald packages), but every global setting it used to
    // carry is owned by WCM ([emeraldwf] in the wayfire config):
    // - the 8 behavior keys (fades, cropping, no-scroll-shade) — part 08;
    // - the titlebar click actions (double/middle) — finding 40, WCM
    // defines them and apply_titlebar_action_options() applies them on
    // every load, so a settings.ini edit would never have reached the
    // render path anyway;
    // - hover_cursor / blur_type — dead settings (no per-node cursor API
    // in wayfire 0.11, compiz blur out of scope), no consumer.
    // ws_ fields start from the structural defaults in the constructor and
    // are finished by the plugin's apply_*_options() calls.

    // ---- theme.ini ----
    std::string theme_ini = theme_dir + "/theme.ini";

    // Part 07 (emerald-theme-manager) engine semantics:
    // - override empty  → the theme's own [engine] (themes are self-
    // contained; the theme editor's Frame Engine tab writes this).
    // - override == the theme's engine → identical to the empty case.
    // - override != the theme's engine (MISMATCH) → run the override
    // engine with an EMPTY theme: its own defaults. A theme's colors,
    // geometry and [<engine>_settings] are authored for its own engine,
    // so applying them to a foreign engine renders nonsense — the
    // engine runs bare instead (the WCM "Engine Override" option).
    gchar *theme_eng = nullptr;
    {
        GKeyFile *tf = g_key_file_new();
        g_key_file_load_from_file(tf, theme_ini.c_str(), G_KEY_FILE_NONE,
            nullptr);
        theme_eng = g_key_file_get_string(tf, "engine", "engine", nullptr);
        g_key_file_free(tf);
    }
    const bool engine_mismatch =
        !engine_override.empty() && theme_eng != nullptr &&
        g_strcmp0(theme_eng, engine_override.c_str()) != 0;
    const bool load_theme = !engine_mismatch;
    /* On a mismatch the engine runs on emerald's DEFAULT theme — that's what "no theme installed" means in
     * emerald. Its per-engine sections ([truglass_settings], ...) give the engine its stock colors: an
     * engine's private struct is zero-initialized (g_malloc0), so bare struct defaults are fully transparent
     * and render nothing.
     *
     * default_theme_dir (resolved above, before the first-run seed) searches:
     *   1. <so-dir>/emeraldwf-defaults/theme   → installed (install.sh) 2. <XDG>/emeraldwf/defaults/theme
     *      → user-dir fallback 3. <project>/third_party/emerald/defaults/theme → build tree */
    const std::string sections_dir =
        load_theme ? theme_dir : default_theme_dir;
    GKeyFile *f = g_key_file_new();
    if (!load_theme)
    {
        engine_ = engine_override;
        g_key_file_load_from_file(f,
            (sections_dir + "/theme.ini").c_str(),
            G_KEY_FILE_NONE, nullptr);
    } else
    {
        g_key_file_load_from_file(f, theme_ini.c_str(), G_KEY_FILE_NONE,
            nullptr);
        if (!engine_override.empty())
        {
            engine_ = engine_override;
        }

        if (theme_eng != nullptr)
        {
            engine_ = theme_eng;
        }
    }

    g_free(theme_eng);

    // Resolve the engine .so dir. The plugin is dlopen'd INTO wayfire, so
    ///proc/self/exe is the wayfire binary (/usr/bin/wayfire), NOT us — using
    // it made an installed plugin resolve to /usr/... (nonexistent) and the
    // engine dlopen failed → nothing painted. Instead locate OUR .so via
    // dladdr() on an address that lives inside it (a local static's address),
    // then search relative to that .so's directory:
    // 1. <so-dir>/../emeraldwf/engines   → installed plugin
    // (/usr/lib64/wayfire/libemeraldwf.so → /usr/lib64/emeraldwf/engines)
    // 2. <so-dir>/../engines             → build tree
    // (.../build/src/libemeraldwf.so → .../build/engines)
    // 3. cwd/__FILE__-relative build/engines (fallback)
    const std::string engine_dir =
        [&]
    {
        auto exists_dir = [] (const fs::path & p)
        {
            std::error_code ec;
            return fs::is_directory(p, ec);
        };
        fs::path so_dir;
        {
            static int in_so = 0; // lives in THIS .so's data segment
            Dl_info info;
            if (dladdr(reinterpret_cast<void*>(&in_so), &info) &&
                (info.dli_fname != nullptr))
            {
                so_dir = fs::path(info.dli_fname).parent_path();
            }
        }
        if (!so_dir.empty())
        {
            // 1. installed layout: <libdir>/wayfire → <libdir>/emeraldwf/engines
            fs::path installed =
                so_dir.parent_path() / "emeraldwf" / "engines";
            if (exists_dir(installed))
            {
                return installed.string();
            }

            // 2. build tree: <builddir>/src → <builddir>/engines
            fs::path builddir = so_dir.parent_path() / "engines";
            if (exists_dir(builddir))
            {
                return builddir.string();
            }
        }

        // 3. fallback: cwd-relative __FILE__ (project root 3 hops up)
        return (file_relative_root().parent_path()
            .parent_path()
            .parent_path() / "build" / "engines")
                   .string();
    }();

    if (!emerald_engine_load(engine_, engine_dir))
    {
        // The cstyleCast below is inside glib's g_warning() macro
        // expansion (a --force configuration), not in this call.
        // cppcheck-suppress cstyleCast
        g_warning("ThemeSettings: failed to load engine '%s', using legacy",
            engine_.c_str());
        engine_ = "legacy";
        if (!emerald_engine_load(engine_, engine_dir))
        {
            // Review R3: report the failure — the previous code ignored the
            // fallback's return and published a theme whose engine never
            // loaded: valid_ == true, but nothing paints. Now load() returns
            // false with valid_ false, so reload_theme() keeps the previous
            // theme (its "keeping previous theme" branch) and init()'s
            // decorators/nodes see valid() == false (no-op render) instead of
            // a silently dead theme.
            // The cstyleCast below is inside glib's g_warning() macro
            // expansion (a --force configuration), not in this call.
            // cppcheck-suppress cstyleCast
            g_warning("ThemeSettings: legacy engine also failed to load "
                      "(engine dir: %s) — theme stays invalid",
                engine_dir.c_str());
            g_key_file_free(f);
            return false;
        }
    }

    // LFACSS(text/titlebar, text_halo/titlebar, button/buttons, button_halo/buttons)
    // LFACSS equivalents (explicit, to avoid the macro's `ws` name clash).
    // f holds the user theme (normal) or emerald's default theme
    // (mismatch override), so these read the right values in both cases.
    load_color_setting(f, &ws_->fs_act->text.color, "active_text", "titlebar");
    load_color_setting(f, &ws_->fs_inact->text.color, "inactive_text", "titlebar");
    load_float_setting(f, &ws_->fs_act->text.alpha, "active_text_alpha", "titlebar");
    load_float_setting(f, &ws_->fs_inact->text.alpha, "inactive_text_alpha", "titlebar");
    load_color_setting(f, &ws_->fs_act->text_halo.color, "active_text_halo", "titlebar");
    load_color_setting(f, &ws_->fs_inact->text_halo.color, "inactive_text_halo", "titlebar");
    load_float_setting(f, &ws_->fs_act->text_halo.alpha, "active_text_halo_alpha", "titlebar");
    load_float_setting(f, &ws_->fs_inact->text_halo.alpha, "inactive_text_halo_alpha", "titlebar");
    load_color_setting(f, &ws_->fs_act->button.color, "active_button", "buttons");
    load_color_setting(f, &ws_->fs_inact->button.color, "inactive_button", "buttons");
    load_float_setting(f, &ws_->fs_act->button.alpha, "active_button_alpha", "buttons");
    load_float_setting(f, &ws_->fs_inact->button.alpha, "inactive_button_alpha", "buttons");
    load_color_setting(f, &ws_->fs_act->button_halo.color, "active_button_halo", "buttons");
    load_color_setting(f, &ws_->fs_inact->button_halo.color, "inactive_button_halo", "buttons");
    load_float_setting(f, &ws_->fs_act->button_halo.alpha, "active_button_halo_alpha", "buttons");
    load_float_setting(f, &ws_->fs_inact->button_halo.alpha, "inactive_button_halo_alpha", "buttons");

    emerald_engine_init_engine(ws_);
    // The engine's own section comes from the same keyfile: the user
    // theme's [<engine>_settings] normally, the default theme's on a
    // mismatch (absent for oxygen/line there → engine struct defaults).
    call_load_engine_settings(f, ws_);

    // Review R1: the theme's [titlebar] titlebar_font was read here and
    // stored in font_desc — then titlebar_font_changed(ws_, "Sans 10") below
    // freed it and re-derived text_height/titlebar_height from "Sans 10",
    // discarding every theme's font (including emerald's own default theme,
    // which pins Bitstream Vera Sans Bold 9). Read it once and pass it to
    // titlebar_font_changed() — the single place font_desc, text_height and
    // titlebar_height are derived together (upstream emerald applies the
    // theme font through exactly this property).
    gchar *font_str = g_key_file_get_string(f, "titlebar", "titlebar_font",
        nullptr);
    const char *titlebar_font =
        font_str != nullptr ? font_str : "Sans 10";
    load_bool_setting(f, &ws_->use_pixmap_buttons, "use_pixmap_buttons",
        "buttons");

    if (ws_->use_pixmap_buttons)
    {
        load_bool_setting(f, &ws_->use_button_glow, "use_button_glow",
            "buttons");
        load_bool_setting(f, &ws_->use_button_inactive_glow,
            "use_button_inactive_glow", "buttons");
        load_button_image_setting(ws_, sections_dir);
    } else
    {
        ws_->use_button_glow = FALSE;
        ws_->use_button_inactive_glow = FALSE;
    }

    load_shadow_color_setting(f, ws_->shadow_color, "shadow_color", "shadow");
    load_int_setting(f, &ws_->shadow_offset_x, "shadow_offset_x", "shadow");
    load_int_setting(f, &ws_->shadow_offset_y, "shadow_offset_y", "shadow");
    load_float_setting(f, &ws_->shadow_radius, "shadow_radius", "shadow");
    load_float_setting(f, &ws_->shadow_opacity, "shadow_opacity", "shadow");
    load_string_setting(f, &ws_->tobj_layout, "title_object_layout",
        "titlebar");
    load_int_setting(f, &ws_->button_offset, "vertical_offset", "buttons");
    load_int_setting(f, &ws_->button_hoffset, "horizontal_offset", "buttons");
    load_int_setting(f, &ws_->win_extents.top, "top", "borders");
    load_int_setting(f, &ws_->win_extents.left, "left", "borders");
    load_int_setting(f, &ws_->win_extents.right, "right", "borders");
    load_int_setting(f, &ws_->win_extents.bottom, "bottom", "borders");
    load_int_setting(f, &ws_->extra_extents.top, "top", "extra_grab");
    load_int_setting(f, &ws_->extra_extents.left, "left", "extra_grab");
    load_int_setting(f, &ws_->extra_extents.right, "right", "extra_grab");
    load_int_setting(f, &ws_->extra_extents.bottom, "bottom", "extra_grab");
    load_int_setting(f, &ws_->min_titlebar_height, "min_titlebar_height",
        "titlebar");

    g_key_file_free(f);

    // Review R1: the theme's font, "Sans 10" only when the key is absent
    // (emerald's main.c defaults). Runs after the min_titlebar_height read
    // above: titlebar_height floors at min_titlebar_height.
    titlebar_font_changed(ws_, titlebar_font);
    g_free(font_str);

    update_shadow(fs_act_);
    update_shadow(fs_inact_);
    create_large_shadow_surface(*this);

    valid_ = true;

    return true;
}

window_settings*ThemeSettings::ws() const
{
    return ws_;
}

frame_settings*ThemeSettings::fs_active() const
{
    return fs_act_;
}

frame_settings*ThemeSettings::fs_inactive() const
{
    return fs_inact_;
}

const std::string& ThemeSettings::engine() const
{
    return engine_;
}

const std::string& ThemeSettings::theme_dir() const
{
    return theme_dir_;
}

bool ThemeSettings::valid() const
{
    return valid_;
}

std::optional<int> wf::emeraldwf::parse_titlebar_action(const std::string & name)
{
    static const std::pair<const char*, int> kActions[] = {
        {"shade", DOUBLE_CLICK_SHADE},
        {"maximize", DOUBLE_CLICK_MAXIMIZE},
        {"minimize", DOUBLE_CLICK_MINIMIZE},
        {"send_to_back", DOUBLE_CLICK_SEND_TO_BACK},
        {"close", DOUBLE_CLICK_CLOSE},
        {"none", DOUBLE_CLICK_NONE},
    };

    // Case-insensitive, trimmed.
    std::string s;
    s.reserve(name.size());
    for (char c : name)
    {
        if (std::isspace(static_cast<unsigned char>(c)))
        {
            continue;
        }

        s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (s.empty())
    {
        return std::nullopt; // "no override" — keep the settings.ini value
    }

    const auto it = std::find_if(std::begin(kActions), std::end(kActions),
        [&s] (const auto & a) { return s == a.first; });
    if (it != std::end(kActions))
    {
        return it->second;
    }

    return std::nullopt;
}

// Resolve the effective titlebar action (see the header for the contract).
int wf::emeraldwf::resolve_titlebar_action(const std::string & override, int base)
{
    if (auto act = parse_titlebar_action(override))
    {
        return *act;
    }

    return base;
}

void wf::emeraldwf::emeraldwf_apply_button_fade(window_settings *ws,
    bool use_fade, bool use_pulse,
    int total_duration_ms, int step_duration_ms,
    int pulse_wait_ms, int pulse_min_opacity_pct)
{
    const int step = (step_duration_ms > 0) ? step_duration_ms : 50;
    int num = (total_duration_ms > 0) ?
        total_duration_ms / step :
        0;
    if (num == 0)
    {
        num = 1;
    }

    ws->use_button_fade = use_fade;
    ws->button_fade_step_duration = step;
    ws->button_fade_num_steps     = num;

    // Same derivation as emerald's own loader (main.c:5753-5779): the pulse
    // peak (and with it the pulse cycle length) is derived from the minimum
    // opacity. pulse_len == 0 marks "pulse off" for the consumers.
    if (use_pulse)
    {
        const int min_opacity =
            (pulse_min_opacity_pct < 0) ? 0 :
            (pulse_min_opacity_pct > 100) ? 100 : pulse_min_opacity_pct;
        ws->button_fade_pulse_len_steps =
            num * (100 - min_opacity) / 100;
        ws->button_fade_pulse_wait_steps =
            (pulse_wait_ms > 0) ? pulse_wait_ms / step : 0;
    } else
    {
        ws->button_fade_pulse_len_steps  = 0;
        ws->button_fade_pulse_wait_steps = 0;
    }
}
