/*
 * emerald-engine.hpp — engine dlopen bridge + pure render (overview §5.4, part 02).
 */
#ifndef EMERALDWF_ENGINE_HPP
#define EMERALDWF_ENGINE_HPP

#include <algorithm>
#include <string>

#include <cairo.h>

#include "emerald-types.hpp"
#include "emerald-settings.hpp"

namespace wf::emeraldwf
{
// Review F3: the `emeraldwf/engine` option name feeds the dlopen() path
// ("lib" + name + ".so"). The option is user/WCM-editable (and poller-synced
// from the ini), so on a shared-config / multi-user setup a value like
// "../../tmp/evil" would dlopen an attacker-placed .so. Accept only
// [A-Za-z0-9_-]+ — every shipped engine matches it, and the characters
// cannot form a path component. Pure (no wayfire symbols) so it is
// testable headlessly (tests/test_settings.cpp).
inline bool is_valid_engine_name(const std::string & name)
{
    if (name.empty())
    {
        return false;
    }

    return std::all_of(name.begin(), name.end(),
        [] (char c)
    {
        return c == '_' || c == '-' ||
        (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9');
    });
}

// dlopen bridge (port of src/engine_loader.c:32-84): searches
// ~/.emerald/engines/lib<name>.so then <install>/lib/emeraldwf/engines/.
bool emerald_engine_load(const std::string & name, const std::string & system_dir);
void emerald_engine_unload();

// M1 (issue #1): bridge-state accessors. A failed load must leave the
// previous engine observable as intact (the reload path keeps the
// previous theme — the test asserts the engine survived), and a
// "loaded" engine must be one the bridge can actually draw with
// (handle + engine_draw_frame both live).
std::string emerald_engine_active_name();
bool emerald_engine_is_loaded();

// Calls the loaded engine's init_engine(window_settings*) so the engine
// allocates its private ws/fs (engine_ws / engine_fs). Must be called
// after emerald_engine_load() and before load_engine_settings.
void emerald_engine_init_engine(window_settings *ws);

// Calls the loaded engine's fini_engine(window_settings*) to free
// engine-private state. Must be called before freeing the window_settings.
void emerald_engine_fini_engine(const window_settings *ws);

// R1 (issue #1): probe the theme dir for the pixmap assets the pixmap
// engine loads. The engine (third_party/emerald/engines/pixmap.c) loads
// 11 `pixmaps.active_<type>.png` textures — and 11
// `pixmaps.inactive_<type>.png` when [pixmap_settings]
// inactive_use_active_pixmaps is not true (its private struct is
// zero-initialized, so the key absent → false → the inactive set is
// required). The bridge must not invoke engine_draw_frame unless every
// file the engine will dereference exists: the pixmap engine reads its
// textures with no NULL check (pixmap.c:250 → compositor segfault).
// Pure (filesystem + GKeyFile only) so it is testable headlessly.
bool theme_has_pixmap_assets(const std::string &theme_dir);

// Renders the COMPLETE frame (shadow background + engine frame + title
// text w/ halo + icon + button glows/backgrounds) into an ARGB32
// cairo_image_surface of size frame_width x frame_height (D7).
// Caller destroys the returned surface.
// Port of draw_window_decoration_real() + draw_shadow_background()
// (emerald main.c:1768-2050) with X pixmap -> image surface.
cairo_surface_t *emeraldwf_render_frame(const ThemeSettings & ts,
    const DecorContext & ctx);
}

#endif
