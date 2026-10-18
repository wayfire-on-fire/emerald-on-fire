/*
 * make_goldens.cpp — generate golden PNGs for test_render.cpp (part 02).
 *
 * Run: build/tests/make_goldens
 * Writes tests/golden/<engine>_<state>_<mode>.png for each engine x
 * {active, inactive} x {windowed, maximized} at client 400x300.
 *
 * Goldens must be visually reviewed and committed (spec: part 02 "Tests").
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include <cairo.h>
#include <cairo-features.h>
#include <glib.h>

#include "emerald-engine.hpp"
#include "emerald-settings.hpp"
// Review R2: fixture paths forwarded by meson (configure_file) — the
// __FILE__-relative root + /build/engines heuristic only works for
// in-tree builds (this tool is run from the build tree).
#include "test-paths.h"

using namespace wf::emeraldwf;
namespace fs = std::filesystem;

namespace
{
std::string project_root()
{
    return EMERALDWF_TEST_SOURCE_ROOT;
}

DecorContext make_ctx(const ThemeSettings &ts, bool active, bool maximized)
{
    const window_settings *ws = ts.ws();
    const int client_w = 400, client_h = 300;

    DecorContext ctx;
    ctx.client_width = client_w;
    ctx.client_height = client_h;
    ctx.width = frame_width(*ws, client_w);
    ctx.height = frame_height(*ws, client_w, client_h);
    ctx.active = active;
    ctx.fs = active ? ts.fs_active() : ts.fs_inactive();
    ctx.state = maximized
                    ? (EWS_MAXIMIZED_HORIZONTALLY | EWS_MAXIMIZED_VERTICALLY)
                    : 0;
    ctx.actions = EWA_CLOSE | EWA_MAXIMIZE | EWA_MINIMIZE;

    for (int i = 0; i < 11; i++)
        ctx.tobj_item_state[i] = 3;
    for (int i = 0; i < 9; i++)
        ctx.button_states[i] = 0;

    return ctx;
}

bool save_png(cairo_surface_t *surf, const fs::path &file)
{
    GError *err = nullptr;
    cairo_status_t status = cairo_surface_write_to_png(surf, file.c_str());
    if (status != CAIRO_STATUS_SUCCESS)
    {
        fprintf(stderr, "write_png %s: %s\n", file.c_str(),
                cairo_status_to_string(status));
        return false;
    }
    (void)err;
    return true;
}
}

int main()
{
    std::string root = project_root();
    fs::path golden = fs::path(root) / "tests" / "golden";
    fs::create_directories(golden);

    ThemeSettings ts;
    if (!ts.load(root + "/third_party/emerald/defaults/settings.ini",
                 root + "/third_party/emerald/defaults/theme"))
    {
        fprintf(stderr, "failed to load defaults\n");
        return 1;
    }

    const char *engines[] = {"legacy", "truglass", "vrunner", "oxygen",
                             "zootreeves", "pixmap", "line"};
    for (const char *engine : engines)
    {
        const std::string engine_dir = EMERALDWF_TEST_ENGINE_DIR;
        if (!emerald_engine_load(engine, engine_dir))
        {
            fprintf(stderr, "FAILED to load engine %s\n", engine);
            continue;
        }
        // Allocate the engine's private ws/fs for this window_settings.
        emerald_engine_init_engine(ts.ws());
        for (bool active : {true, false})
        {
            for (bool maximized : {true, false})
            {
                std::string mode = maximized ? "max" : "win";
                std::string state = active ? "active" : "inactive";
                DecorContext ctx = make_ctx(ts, active, maximized);

                cairo_surface_t *surf = emeraldwf_render_frame(ts, ctx);
                if (surf == nullptr)
                {
                    fprintf(stderr, "render failed: %s_%s_%s\n", engine,
                            state.c_str(), mode.c_str());
                    continue;
                }
                fs::path file = golden /
                               (std::string(engine) + "_" + state + "_" +
                                mode + ".png");
                if (!save_png(surf, file))
                {
                    fprintf(stderr, "save failed: %s\n", file.c_str());
                }
                cairo_surface_destroy(surf);
            }
        }
        emerald_engine_unload();
    }

    fprintf(stderr, "goldens written to %s\n", golden.c_str());
    return 0;
}
