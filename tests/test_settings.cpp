/*
 * test_settings.cpp — ThemeSettings loading tests (part 02, headless).
 */
#include <gtest/gtest.h>

#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <cairo.h>

#include <wayfire/config/option.hpp>
#include <wayfire/config/types.hpp>
#include <wayfire/util/duration.hpp>

#include "emerald-button-fade.hpp"
#include "emerald-config-sync.hpp"
#include "emerald-engine.hpp"
#include "emerald-file-stamp.hpp"
#include "emerald-layout.hpp"
#include "emerald-settings.hpp"
// Review R2: fixture paths forwarded by meson (configure_file) — the
///proc/self/exe heuristic broke for out-of-tree builds, where the engine
// .so's live in <builddir>/engines, not <srcroot>/build/engines.
#include "test-paths.h"

using namespace wf::emeraldwf;
namespace fs = std::filesystem;

namespace
{
std::string project_root()
{
    return EMERALDWF_TEST_SOURCE_ROOT;
}

std::string defaults_dir()
{
    return project_root() + "/third_party/emerald/defaults";
}

// The 11 pixmap types the pixmap engine loads (pixmap.c p_types[]).
const char *PIXMAP_TYPES[] = {
    "top", "top_left", "top_right", "left", "right",
    "bottom", "bottom_left", "bottom_right",
    "title", "title_left", "title_right"};

// R1 (issue #1) INTEGRATION: a fully packaged pixmap theme must actually
// RENDER — the gate must open (has_pixmap_assets) AND the pixmap engine
// must draw its textures (not the gray fallback). This is the regression
// test for the original bug: pre-R1 the gate was permanently false for
// correct themes, so this render path never ran and the pixmap engine
// was silently dead.
//
// The pixmap engine loads its textures from a HARDCODED ~/.emerald/theme
// (make_filename, third_party/emerald/libengine/emerald.c:66 — finding
// M5), so this test controls HOME. It must run before any other test in
// this binary: g_get_home_dir() caches on first use, so the setenv must
// precede the first glib home query process-wide. (Registered first
// below; gtest runs in registration order.)
class PixmapIntegrationTest : public ::testing::Test
{
  protected:
    static void make_png(const fs::path &p, int r, int g, int b)
    {
        cairo_surface_t *s = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, 16, 16);
        cairo_t *cr = cairo_create(s);
        cairo_set_source_rgb(cr, r / 255.0, g / 255.0, b / 255.0);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_write_to_png(s, p.string().c_str());
        cairo_surface_destroy(s);
    }

    void SetUp() override
    {
        const char *h = std::getenv("HOME");
        old_home_ = (h != nullptr) ? std::string(h) : "";
        home_ = fs::temp_directory_path() /
            (fs::path("emeraldwf-pixmap-int") / std::to_string(::getpid()));
        theme_dir_ = home_ / ".emerald" / "theme";
        fs::create_directories(theme_dir_);
        ::setenv("HOME", home_.string().c_str(), 1);

        // settings.ini — content is irrelevant to emeraldwf (part 08:
        // no keys are read from it); its presence skips the first-run
        // seed into our temp HOME.
        std::ofstream(home_ / ".emerald" / "settings.ini") << "# test\n";

        // theme.ini — the vendored defaults with engine=pixmap and the
        // inactive set REQUIRED (inactive_use_active_pixmaps=false), so
        // both the probe and the engine expect all 22 textures.
        {
            std::ifstream in(defaults_dir() + "/theme/theme.ini");
            std::string ini((std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>());
            const auto p1 = ini.find("engine=vrunner");
            const auto p2 = ini.find("inactive_use_active_pixmaps=true");
            ASSERT_NE(p1, std::string::npos) <<
                "vendored theme.ini no longer pins engine=vrunner?";
            ASSERT_NE(p2, std::string::npos) <<
                "vendored theme.ini no longer pins inactive_use_active_pixmaps?";
            ini.replace(p1, strlen("engine=vrunner"), "engine=pixmap");
            ini.replace(p2,
                strlen("inactive_use_active_pixmaps=true"),
                "inactive_use_active_pixmaps=false");
            std::ofstream(theme_dir_ / "theme.ini") << ini;
        }

        // button PNGs — the defaults theme sets use_pixmap_buttons=true.
        for (const auto &entry :
            fs::directory_iterator(defaults_dir() + "/theme"))
        {
            const std::string name = entry.path().filename().string();
            if (name.rfind("buttons.", 0) == 0)
            {
                fs::copy_file(entry.path(),
                    theme_dir_ / entry.path().filename(),
                    fs::copy_options::overwrite_existing);
            }
        }

        // the 22 pixmap textures: active_top is a distinctive RED — the
        // engine draws the `top` texture as the WHOLE titlebar band
        // (pixmap.c "top bar"), so a red band proves the engine rendered;
        // the gray fallback would put (153,153,153,204) there. (The
        // `title` texture only spans the Pango title width, which is
        // zero in this port's decor_t — d.layout is unset — so it is not
        // a reliable marker.) Everything else is neutral.
        for (auto t : PIXMAP_TYPES)
        {
            const bool red = (std::string(t) == "top");
            make_png(theme_dir_ /
                (std::string("pixmaps.active_") + t + ".png"),
                red ? 255 : 120, red ? 0 : 120, red ? 0 : 120);
            make_png(theme_dir_ /
                (std::string("pixmaps.inactive_") + t + ".png"),
                red ? 192 : 120, red ? 0 : 120, red ? 0 : 120);
        }
    }

    void TearDown() override
    {
        emerald_engine_unload();
        if (!old_home_.empty())
        {
            ::setenv("HOME", old_home_.c_str(), 1);
        } else
        {
            ::unsetenv("HOME");
        }
        fs::remove_all(home_);
    }

    std::string old_home_;
    fs::path home_;
    fs::path theme_dir_;
};

TEST_F(PixmapIntegrationTest, FullyPackagedTheme_RendersPixmapNotFallback)
{
    ThemeSettings ts;
    ASSERT_TRUE(ts.load(home_ / ".emerald" / "settings.ini",
        theme_dir_.string()))
        << "loading a fully packaged pixmap theme must succeed";
    EXPECT_EQ(ts.engine(), "pixmap");
    EXPECT_TRUE(ts.has_pixmap_assets())
        << "a complete 22-file asset set must open the gate";

    // windowed, active — same ctx shape as the render suite
    const int client_w = 400, client_h = 300;
    window_settings *ws = ts.ws();
    DecorContext ctx;
    ctx.client_width  = client_w;
    ctx.client_height = client_h;
    ctx.width  = frame_width(*ws, client_w);
    ctx.height = frame_height(*ws, client_w, client_h);
    ctx.active = true;
    ctx.fs     = ts.fs_active();
    ctx.state  = 0;
    ctx.actions = EWA_CLOSE | EWA_MAXIMIZE | EWA_MINIMIZE;
    for (int i = 0; i < 11; i++)
    {
        ctx.tobj_item_state[i] = 3;
    }

    for (int i = 0; i < 9; i++)
    {
        ctx.button_states[i] = 0;
    }

    cairo_surface_t *surf = emeraldwf_render_frame(ts, ctx);
    ASSERT_NE(surf, nullptr);

    const int w       = cairo_image_surface_get_width(surf);
    const int stride  = cairo_image_surface_get_stride(surf);
    const unsigned char *data =
        (const unsigned char*)cairo_image_surface_get_data(surf);

    // Sample the titlebar center strip: [w/2-50, w/2+50] at the titlebar
    // mid row (top_space already includes the top border; the titlebar
    // band is [top_space, top_space + titlebar_height)). The `top`
    // texture is pure red — the gray fallback would put exactly
    // (153,153,153,204) here.
    //
    // The assertions are MAJORITY-based, not exact-pixel: the strip
    // avoids the corners and the right-side buttons, but antialiasing /
    // edge blending and the title halo can tint a few pixels, so
    // "most of the strip is the red texture" (and "not most of it the
    // fallback gray") is the stable contract.
    const int y = ws->top_space + ws->titlebar_height / 2;
    int red_px  = 0;
    int gray_px = 0;
    int total   = 0;
    for (int x = w / 2 - 50; x <= w / 2 + 50; x++)
    {
        if (x < 0 || x >= w)
        {
            continue;
        }

        const unsigned char *px = data + y * stride + x * 4;
        // ARGB32, premultiplied: B, G, R, A little-endian
        const int r = px[2], g = px[1], b = px[0], a = px[3];
        total++;
        if ((r > 150) && (g < 120) && (b < 120))
        {
            red_px++;
        }

        // the fallback fill is exactly (153,153,153,204)
        if ((r == 153) && (g == 153) && (b == 153) && (a == 204))
        {
            gray_px++;
        }
    }

    EXPECT_GT(total, 0);
    EXPECT_GT(red_px, total / 2)
        << "the titlebar center must be the RED `top` texture"
        << " (pixmap engine rendered); red=" << red_px
        << "/" << total
        << " — a gray titlebar means the fallback path ran and the"
        << " pixmap engine was never invoked (the pre-R1 bug)";
    EXPECT_LE(gray_px, total / 10)
        << "the titlebar center must not be the gray fallback fill"
        << " (153,153,153,204); gray=" << gray_px << "/" << total;

    cairo_surface_destroy(surf);
}

// R2 (issue #1): theme.ini is user-editable and the 500 ms poller
// live-reloads it — an out-of-range shadow_radius must be clamped at
// load, never reaching the quadratic shadow allocations in
// create_large_shadow_surface() (pre-fix: radius 100000 -> frame
// ~2*200002 px -> ~640 GB vector -> std::bad_alloc, uncaught in the
// wl_timer callback -> std::terminate killed wayfire; a negative radius
// with non-zero offsets bypassed the "no shadow" early return and
// wrapped the size_t extents the same way).
namespace
{
void write_shadow_theme(const fs::path &dir, const std::string &radius,
    const std::string &offset_x = "0",
    const std::string &offset_y = "0")
{
    fs::create_directories(dir / "theme");
    std::ifstream in(defaults_dir() + "/theme/theme.ini");
    std::string ini((std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    struct
    {
        const char *key;
        std::string value;
    } subs[] = {
        {"shadow_radius=11.5", "shadow_radius=" + radius},
        {"shadow_offset_x=0", "shadow_offset_x=" + offset_x},
        {"shadow_offset_y=0", "shadow_offset_y=" + offset_y},
    };
    for (auto &s : subs)
    {
        const auto p = ini.find(s.key);
        if (p != std::string::npos)
        {
            ini.replace(p, strlen(s.key), s.value);
        }
    }

    std::ofstream(dir / "theme" / "theme.ini") << ini;
    std::ifstream s(defaults_dir() + "/settings.ini");
    std::string sc((std::istreambuf_iterator<char>(s)),
        std::istreambuf_iterator<char>());
    std::ofstream(dir / "settings.ini") << sc;
}
}

class SettingsTest : public ::testing::Test
{
  public:
    void SetUp() override
    {
        tmp_dir_ = fs::temp_directory_path() /
            (fs::path("emeraldwf-test") /
                std::to_string(::getpid()));
        fs::create_directories(tmp_dir_ / "theme");
    }

    void TearDown() override
    {
        fs::remove_all(tmp_dir_);
    }

    fs::path tmp_dir_;
};

// M5 (issue #1): the pixmap/zootreeves engines resolve their pixmaps
// via the vendored make_filename() (third_party/emerald/libengine/
// emerald.c:66), which HARDCODES ~/.emerald/theme. With a non-default
// theme_dir the bridge probes the right dir but the engine loads from
// the wrong one — load() must warn. The decision is factored into a
// pure function so it is testable without a full theme load.
TEST(ThemeDirMismatch, OnlyPixmapEnginesMismatch)
{
    const std::string def =
        std::string{g_get_home_dir()} + "/.emerald/theme";
    const std::string alt = "/tmp/emeraldwf-alt-theme";

    EXPECT_FALSE(theme_dir_mismatched_for_engine("pixmap", def))
        << "the default theme dir is NOT a mismatch";
    EXPECT_TRUE(theme_dir_mismatched_for_engine("pixmap", alt));
    EXPECT_TRUE(theme_dir_mismatched_for_engine("zootreeves", alt));
    EXPECT_FALSE(theme_dir_mismatched_for_engine("oxygen", alt))
        << "only make_filename-based engines (pixmap/zootreeves) "
           "resolve pixmaps from the hardcoded dir";
    EXPECT_FALSE(theme_dir_mismatched_for_engine("legacy", alt));
}

// M5 (issue #1) INTEGRATION: loading a pixmap theme from a NON-default
// dir must emit a load-time warning (captured via g_log_set_handler).
// The fixture's theme (default dir) is copied to an alt dir; the
// engine still loads its textures from the (hardcoded) default dir, so
// the load succeeds — the warning is the observable.
TEST_F(PixmapIntegrationTest, NonDefaultThemeDir_WarnsForPixmapEngine)
{
    const auto alt_dir = home_ / "alt-theme";
    fs::copy(theme_dir_, alt_dir,
        fs::copy_options::recursive);

    // glib: an installed handler REPLACES the default (GLogFunc returns
    // void). Capture the warnings; remove the handler by ID (RAII, so an
    // early assertion failure cannot leave it installed).
    std::vector<std::string> warnings;
    auto capture = +[] (const gchar *, GLogLevelFlags, const gchar *msg,
        gpointer ud)
    {
        static_cast<std::vector<std::string> *>(ud)->emplace_back(msg);
    };
    const guint handler_id =
        g_log_set_handler(nullptr, G_LOG_LEVEL_WARNING, capture, &warnings);
    struct HandlerGuard
    {
        guint id;
        ~HandlerGuard()
        {
            g_log_remove_handler(nullptr, id);
        }
    } guard{handler_id};

    ThemeSettings ts;
    const bool ok = ts.load(
        (home_ / ".emerald" / "settings.ini").string(),
        alt_dir.string(), "");

    ASSERT_TRUE(ok) << "the pixmap load from the alt dir must succeed "
        "(the engine's textures live in the hardcoded default dir)";

    const bool warned = std::any_of(warnings.begin(), warnings.end(),
        [&] (const std::string & w)
        {
            return w.find(alt_dir.string()) != std::string::npos &&
                w.find("pixmap") != std::string::npos;
        });
    EXPECT_TRUE(warned)
        << "load() must warn that the pixmap engine resolves its "
           "pixmaps from the hardcoded ~/.emerald/theme; captured: "
        << (warnings.empty() ? "(none)" : warnings.front());
}

// M6 (issue #1): a corrupt theme.ini used to load into an EMPTY GKeyFile
// with NO diagnostic — g_key_file_load_from_file() got a nullptr GError,
// so the theme silently ran on bare defaults while engine-load failures
// were loud. Now load() passes a GError*, g_warning's the path, and
// keeps the tolerant behavior (the load continues with defaults).
// (The missing-theme.ini case takes the same GError path, but is covered
// by the M8 seeding change instead — a missing file is re-seeded, not
// warned about.)
// M7 (issue #1): signed overflow in the button-fade pulse derivation.
// `num * (100 - min_opacity)` is int arithmetic; with
// button_fade_total_duration = INT_MAX and step = 50 (both reachable via
// wayfire.ini + config sync), num = INT_MAX/50 = 42949672, and
// num * 100 = 4294967200 overflows INT_MAX — UB, a bogus step count.
// The fix uses an int64_t intermediate.
// M8 (issue #1): first-run seeding used to be coupled to settings.ini —
// copy_from_defaults_if_needed() returned early if settings.ini existed,
// so a destroyed theme/theme.ini was NEVER re-seeded (the theme silently
// ran on bare defaults). Upstream checks the two files independently.
TEST_F(SettingsTest, MissingThemeIniReseedsIndependentOfSettingsIni)
{
    const fs::path dir = tmp_dir_ / "m8-reseed";
    fs::create_directories(dir / "theme");
    std::ofstream(dir / "settings.ini") << "# present\n";
    // theme/ exists but theme.ini is MISSING (destroyed/never created).

    ThemeSettings ts;
    const bool ok = ts.load((dir / "settings.ini").string(),
        (dir / "theme").string());
    ASSERT_TRUE(ok);

    EXPECT_TRUE(fs::exists(dir / "theme" / "theme.ini"))
        << "a missing theme.ini must be re-seeded even when "
           "settings.ini exists (pre-fix: the early return skipped "
           "the theme seed)";

    // The seed is the vendored default theme (spot-check its engine).
    GKeyFile *f = g_key_file_new();
    g_key_file_load_from_file(f,
        (dir / "theme" / "theme.ini").c_str(),
        G_KEY_FILE_NONE, nullptr);
    gchar *eng = g_key_file_get_string(f, "engine", "engine", nullptr);
    EXPECT_STREQ(eng, "vrunner")
        << "the re-seeded theme must be the vendored default";
    g_free(eng);
    g_key_file_free(f);
}

TEST_F(SettingsTest, ExistingThemeNotOverwrittenBySeed)
{
    // Both files exist -> nothing is re-seeded; the user's theme.ini
    // content survives the load.
    const fs::path dir = tmp_dir_ / "m8-keep";
    fs::create_directories(dir / "theme");
    std::ofstream(dir / "settings.ini") << "# present\n";
    std::ofstream(dir / "theme" / "theme.ini")
        << "[engine]\nengine=oxygen\n[titlebar]\nactive_text=#ff0000\n";

    ThemeSettings ts;
    const bool ok = ts.load((dir / "settings.ini").string(),
        (dir / "theme").string());
    ASSERT_TRUE(ok);

    // The user theme was used, not the default (red active_text).
    EXPECT_FLOAT_EQ(ts.ws()->fs_act->text.color.r, 1.0f);
    EXPECT_FLOAT_EQ(ts.ws()->fs_act->text.color.g, 0.0f);
}

TEST_F(SettingsTest, FreshDirSeedsBothFiles)
{
    // Neither file exists -> both are seeded (the pre-existing first-run
    // behavior, pinned here so the M8 decoupling can't regress it).
    const fs::path dir = tmp_dir_ / "m8-fresh";
    fs::create_directories(dir / "theme");

    ThemeSettings ts;
    const bool ok = ts.load((dir / "settings.ini").string(),
        (dir / "theme").string());
    ASSERT_TRUE(ok);
    EXPECT_TRUE(fs::exists(dir / "settings.ini"))
        << "a fresh dir must seed settings.ini";
    EXPECT_TRUE(fs::exists(dir / "theme" / "theme.ini"))
        << "a fresh dir must seed theme.ini";
}

TEST(ButtonFadeOverflow, HugeDurationPulseDerivationDoesNotOverflow)
{
    auto *ws = new window_settings{};

    emeraldwf_apply_button_fade(ws, true, true, INT_MAX, 50, 0, 0);
    const int num = INT_MAX / 50; // 42949672
    ASSERT_EQ(ws->button_fade_num_steps, num);
    EXPECT_EQ(ws->button_fade_pulse_len_steps, num)
        << "min_opacity=0 -> pulse_len == num; pre-fix num * 100 "
           "overflowed int (UB, wrapped to a garbage value)";
    delete ws;
}

TEST(ButtonFadeOverflow, PulseDerivationFormulaUnchanged)
{
    auto *ws = new window_settings{};

    // Sanity: the int64_t intermediate must not change the derivation
    // for in-range values. num = 1000/50 = 20; min_opacity = 50 ->
    // pulse_len = 20 * 50 / 100 = 10.
    emeraldwf_apply_button_fade(ws, true, true, 1000, 50, 0, 50);
    EXPECT_EQ(ws->button_fade_num_steps, 20);
    EXPECT_EQ(ws->button_fade_pulse_len_steps, 10);

    // min_opacity = 100 -> pulse_len = 0 (pulse cycle empty).
    emeraldwf_apply_button_fade(ws, true, true, 1000, 50, 0, 100);
    EXPECT_EQ(ws->button_fade_pulse_len_steps, 0);
    delete ws;
}

TEST_F(SettingsTest, CorruptThemeIniWarnsWithPathAndStaysTolerant)
{
    const fs::path dir = tmp_dir_ / "m6-corrupt";
    fs::create_directories(dir / "theme");
    std::ofstream(dir / "theme" / "theme.ini")
        << "this is not a keyfile [[[ {{{\n";
    std::ofstream(dir / "settings.ini") << "# test\n";

    // Capture glib warnings (handler replaced by ID; RAII removal).
    std::vector<std::string> warnings;
    auto capture = +[] (const gchar *, GLogLevelFlags, const gchar *msg,
        gpointer ud)
    {
        static_cast<std::vector<std::string> *>(ud)->emplace_back(msg);
    };
    const guint handler_id =
        g_log_set_handler(nullptr, G_LOG_LEVEL_WARNING, capture, &warnings);
    struct HandlerGuard
    {
        guint id;
        ~HandlerGuard()
        {
            g_log_remove_handler(nullptr, id);
        }
    } guard{handler_id};

    ThemeSettings ts;
    const bool ok = ts.load((dir / "settings.ini").string(),
        (dir / "theme").string());
    ASSERT_TRUE(ok)
        << "a corrupt theme.ini must not kill the load (tolerant: bare "
           "defaults)";

    const std::string theme_ini_path =
        (dir / "theme" / "theme.ini").string();
    const bool warned = std::any_of(warnings.begin(), warnings.end(),
        [&] (const std::string & w)
        {
            return w.find(theme_ini_path) != std::string::npos;
        });
    EXPECT_TRUE(warned)
        << "load() must warn with the theme.ini path on a GKeyFile "
           "failure; captured: "
        << (warnings.empty() ? "(none)" : warnings.front());
}

TEST_F(SettingsTest, ShadowRadiusClampedAtLoad)
{
    const fs::path dir = tmp_dir_ / "shadow-clamp-huge";
    write_shadow_theme(dir, "100000");

    ThemeSettings ts;
    ASSERT_TRUE(ts.load((dir / "settings.ini").string(),
        (dir / "theme").string()))
        << "a huge shadow_radius must not kill the load"
        << " (pre-fix: ~640 GB allocation -> bad_alloc -> terminate)";
    EXPECT_EQ(ts.ws()->shadow_radius, 200.0)
        << "out-of-range shadow_radius must be clamped at load";
}

TEST_F(SettingsTest, ShadowRadiusNegativeClampedToZero)
{
    // negative radius + non-zero offset: pre-fix this bypassed the
    // "no shadow" early return (which requires ALL THREE zero) and
    // reached the allocations with negative extents (size_t wrap).
    const fs::path dir = tmp_dir_ / "shadow-clamp-negative";
    write_shadow_theme(dir, "-50", "0", "10");

    ThemeSettings ts;
    ASSERT_TRUE(ts.load((dir / "settings.ini").string(),
        (dir / "theme").string()))
        << "a negative shadow_radius must not kill the load";
    EXPECT_EQ(ts.ws()->shadow_radius, 0.0)
        << "negative shadow_radius must clamp to 0";
}

TEST_F(SettingsTest, ShadowRadiusNaNNormalizedToZero)
{
    // GKeyFile parses "nan"/"inf" strings; std::clamp alone lets NaN
    // through (every comparison with NaN is false) and the later
    // double->int conversion is UB.
    const fs::path dir = tmp_dir_ / "shadow-clamp-nan";
    write_shadow_theme(dir, "nan");

    ThemeSettings ts;
    ASSERT_TRUE(ts.load((dir / "settings.ini").string(),
        (dir / "theme").string()))
        << "a non-finite shadow_radius must not kill the load";
    EXPECT_EQ(ts.ws()->shadow_radius, 0.0)
        << "NaN shadow_radius must normalize to 0";
}

TEST_F(SettingsTest, ShadowRadiusBoundaryClamped)
{
    const fs::path dir = tmp_dir_ / "shadow-clamp-boundary";
    write_shadow_theme(dir, "200.1");

    ThemeSettings ts;
    ASSERT_TRUE(ts.load((dir / "settings.ini").string(),
        (dir / "theme").string()));
    EXPECT_EQ(ts.ws()->shadow_radius, 200.0)
        << "just above the cap must clamp to it";
}

TEST_F(SettingsTest, ShadowOffsetsClamped)
{
    // The offsets are an independent DoS vector:
    // shadow_right_space / shadow_*_corner_space are max(0, size ±
    // offset) — an unclamped 2e9 offset balloons the frame to ~2e9 px
    // and the quadratic allocations with it (radius 0 + non-zero
    // offset also bypasses the "no shadow" early return).
    const fs::path dir = tmp_dir_ / "shadow-clamp-offsets";
    write_shadow_theme(dir, "11.5", "2000000000", "-2000000000");

    ThemeSettings ts;
    ASSERT_TRUE(ts.load((dir / "settings.ini").string(),
        (dir / "theme").string()))
        << "huge shadow offsets must not kill the load"
        << " (pre-fix: ~2e9 px frame -> quadratic allocation)";
    EXPECT_EQ(ts.ws()->shadow_offset_x, 200);
    EXPECT_EQ(ts.ws()->shadow_offset_y, -200);
}


TEST_F(SettingsTest, DefaultSettings)
{
    // Point at the vendored defaults.
    ThemeSettings ts;
    bool ok = ts.load(defaults_dir() + "/settings.ini",
        defaults_dir() + "/theme");
    ASSERT_TRUE(ok) << "load() failed on the vendored defaults";
    EXPECT_TRUE(ts.valid());

    window_settings *ws = ts.ws();
    ASSERT_NE(ws, nullptr);

    // settings.ini core
    EXPECT_EQ(ws->double_click_action, 0); // DOUBLE_CLICK_SHADE
    EXPECT_EQ(ws->middle_click_action, 3); // DOUBLE_CLICK_SEND_TO_BACK
    EXPECT_EQ(ws->blur_type, 0); // BLUR_TYPE_NONE
    // Part 08: use_decoration_cropping / use_button_fade are WCM-owned —
    // the loader leaves them untouched (the plugin applies the WCM values
    // after load; see LoaderLeavesWcmOwnedKeysUntouched).
    EXPECT_FALSE(ws->use_decoration_cropping);
    EXPECT_FALSE(ws->use_button_fade);

    // default layout (vendored theme.ini: title_object_layout=I(30):T:N(2)X(2)C:)
    ASSERT_NE(ws->tobj_layout, nullptr);
    EXPECT_STREQ(ws->tobj_layout, "I(30):T:N(2)X(2)C:");

    // vendored theme.ini borders: top=4 bottom=5 left=5 right=5
    EXPECT_EQ(ws->win_extents.left, 5);
    EXPECT_EQ(ws->win_extents.top, 4);
    EXPECT_EQ(ws->win_extents.right, 5);
    EXPECT_EQ(ws->win_extents.bottom, 5);
    EXPECT_EQ(ws->min_titlebar_height, 18);

    // titlebar height comes from the font (>= min_titlebar_height)
    EXPECT_GE(ws->titlebar_height, ws->min_titlebar_height);

    // shadow math (update_shadow)
    EXPECT_GE(ws->left_space, ws->win_extents.left);
    EXPECT_GE(ws->top_space, ws->win_extents.top);
    EXPECT_GE(ws->bottom_space, ws->win_extents.bottom);

    // frame settings
    EXPECT_NE(ts.fs_active(), nullptr);
    EXPECT_NE(ts.fs_inactive(), nullptr);
    EXPECT_EQ(ts.fs_active()->ws, ws);
    EXPECT_EQ(ts.fs_inactive()->ws, ws);
}

// Review S4 (covers the R1 fix): the theme's [titlebar] titlebar_font must
// reach ws->font_desc (previously the loaded value was clobbered by a
// hardcoded "Sans 10"). The goldens cannot catch this — the render tests
// draw no title text, and min_titlebar_height clamps both fonts — so the
// font string itself is the assertion.
TEST_F(SettingsTest, TitlebarFont_ComesFromThemeIni)
{
    ThemeSettings ts;
    ASSERT_TRUE(ts.load(defaults_dir() + "/settings.ini",
        defaults_dir() + "/theme")) <<
        "load() failed on the vendored defaults";
    window_settings *ws = ts.ws();

    // The default theme pins titlebar_font=Bitstream Vera Sans Bold 9.
    ASSERT_NE(ws->font_desc, nullptr) << "font_desc must be set";
    gchar *font = pango_font_description_to_string(ws->font_desc);
    EXPECT_STREQ(font, "Bitstream Vera Sans Bold 9");
    g_free(font);
    EXPECT_GT(ws->text_height, 0);
    EXPECT_GE(ws->titlebar_height, ws->min_titlebar_height);
}

TEST_F(SettingsTest, TitlebarFont_DefaultsToSans10WhenAbsent)
{
    // A theme without a [titlebar] titlebar_font key falls back to
    // "Sans 10" (emerald's own default).
    std::ofstream(tmp_dir_ / "theme" / "theme.ini") <<
        "[engine]\n"
        "engine=truglass\n";

    ThemeSettings ts;
    ASSERT_TRUE(ts.load((tmp_dir_ / "settings.ini").string(),
        (tmp_dir_ / "theme").string()));
    window_settings *ws = ts.ws();

    ASSERT_NE(ws->font_desc, nullptr);
    gchar *font = pango_font_description_to_string(ws->font_desc);
    EXPECT_STREQ(font, "Sans 10");
    g_free(font);
}

TEST_F(SettingsTest, CustomTheme)
{
    // Write a tiny theme.ini with truglass_settings + a titlebar key.
    std::string theme_ini =
        "[engine]\n"
        "engine=truglass\n"
        "[truglass_settings]\n"
        "active_outer=#ff0000\n"
        "active_outer_alpha=1\n";
    std::ofstream(tmp_dir_ / "theme" / "theme.ini") << theme_ini;

    ThemeSettings ts;
    bool ok = ts.load((tmp_dir_ / "settings.ini").string(),
        (tmp_dir_ / "theme").string());
    ASSERT_TRUE(ok);

    EXPECT_EQ(ts.engine(), "truglass");
    // Custom theme.ini doesn't set use_pixmap_buttons; the value comes from
    // emerald's default (false) since our theme.ini doesn't override it.
    EXPECT_FALSE(ts.ws()->use_pixmap_buttons);

    // copy_from_defaults_if_needed must have created settings.ini
    EXPECT_TRUE(fs::exists(tmp_dir_ / "settings.ini"));

    // theme.ini already exists, so default theme PNGs are NOT copied.
    EXPECT_FALSE(fs::exists(tmp_dir_ / "theme" / "buttons.close.png"));
}

TEST_F(SettingsTest, MissingThemeDir)
{
    // Nonexistent theme dir -> defaults copied.
    ThemeSettings ts;
    bool ok = ts.load((tmp_dir_ / "settings.ini").string(),
        (tmp_dir_ / "theme").string());
    ASSERT_TRUE(ok);

    // theme.ini must now exist (copied from defaults)
    EXPECT_TRUE(fs::exists(tmp_dir_ / "theme" / "theme.ini"));
    EXPECT_TRUE(fs::exists(tmp_dir_ / "theme" / "buttons.close.png"));
    EXPECT_EQ(ts.engine(), "vrunner") <<
        "vendored default theme.ini sets engine=vrunner";
}

// ── Part 07 (emerald-theme-manager): engine precedence ─────────────────────
//
// Regression: the wayfire `engine` option used to FORCE the engine,
// overriding theme.ini's [engine] engine — so the theme editor's engine
// picker (which writes theme.ini) was a no-op whenever the option was
// set. Now the theme wins and the option is only the default.
// ── Part 07 (emerald-theme-manager): engine override ──────────────────────
//
// Semantics: empty override → the theme's own [engine]; an override equal
// to the theme's engine → same; an override DIFFERENT from the theme's
// engine → that engine runs with an EMPTY theme (its own defaults),
// because a theme's colors/geometry/[<engine>_settings] are authored for
// its own engine.
TEST_F(SettingsTest, ThemeEngineUsedWhenOverrideEmpty)
{
    const std::string theme_ini =
        "[theme]\n"
        "version=1.0\n"
        "[engine]\n"
        "engine=truglass\n";
    std::ofstream(tmp_dir_ / "theme" / "theme.ini") << theme_ini;

    ThemeSettings ts;
    ASSERT_TRUE(ts.load((tmp_dir_ / "settings.ini").string(),
        (tmp_dir_ / "theme").string()));
    EXPECT_EQ(ts.engine(), "truglass") <<
        "no override: the theme's own engine is used";
}

TEST_F(SettingsTest, EngineOverrideMatchAppliesTheme)
{
    const std::string theme_ini =
        "[theme]\n"
        "version=1.0\n"
        "[engine]\n"
        "engine=vrunner\n"
        "[titlebar]\n"
        "active_text=#00ff00\n"
        "[shadow]\n"
        "shadow_radius=42\n";
    std::ofstream(tmp_dir_ / "theme" / "theme.ini") << theme_ini;

    // override == the theme's engine → identical to no override: theme applies.
    ThemeSettings ts;
    ASSERT_TRUE(ts.load((tmp_dir_ / "settings.ini").string(),
        (tmp_dir_ / "theme").string(), "vrunner"));
    EXPECT_EQ(ts.engine(), "vrunner");
    EXPECT_NEAR(ts.ws()->fs_act->text.color.r, 0.0, 1e-6);
    EXPECT_NEAR(ts.ws()->fs_act->text.color.g, 1.0, 1e-6) <<
        "theme colors must apply when the override matches the theme";
}

// Regression: a mismatched override must NOT run on the theme's foreign
// assets — the engine gets emerald's DEFAULT theme (its own stock look:
// an engine's private struct is zero-initialized = transparent, so bare
// struct defaults would render nothing).
TEST_F(SettingsTest, EngineOverrideMismatchRunsDefaultTheme)
{
    const std::string theme_ini =
        "[theme]\n"
        "version=1.0\n"
        "[engine]\n"
        "engine=vrunner\n"
        "[titlebar]\n"
        "active_text=#00ff00\n"
        "[shadow]\n"
        "shadow_radius=42\n"
        "min_titlebar_height=99\n";
    std::ofstream(tmp_dir_ / "theme" / "theme.ini") << theme_ini;

    ThemeSettings ts;
    ASSERT_TRUE(ts.load((tmp_dir_ / "settings.ini").string(),
        (tmp_dir_ / "theme").string(), "truglass"));
    EXPECT_EQ(ts.engine(), "truglass") <<
        "an explicit override selects the engine";
    // Default theme values, NOT the user theme's:
    EXPECT_NEAR(ts.ws()->fs_act->text.color.r, 1.0, 1e-6) <<
        "theme colors must NOT apply to a mismatched engine "
        "(default theme text is white, the user theme is green)";
    EXPECT_NEAR(ts.ws()->shadow_radius, 11.5, 1e-6) <<
        "the default theme's shadow (11.5) must apply, not the "
        "user theme's (42) or the struct default (15)";
    EXPECT_EQ(ts.ws()->min_titlebar_height, 18) <<
        "the default theme's min height (18) must apply, not the "
        "user theme's (99)";
    EXPECT_STREQ(ts.ws()->tobj_layout, "I(30):T:N(2)X(2)C:") <<
        "the default theme's layout must apply, not the user "
        "theme's";
}

TEST_F(SettingsTest, EngineOverrideAppliesWhenThemeUnpinned)
{
    // theme.ini without an [engine] section → the override applies,
    // and the theme's (engine-agnostic) colors still load.
    const std::string theme_ini =
        "[theme]\n"
        "version=1.0\n"
        "[titlebar]\n"
        "active_text=#00ff00\n";
    std::ofstream(tmp_dir_ / "theme" / "theme.ini") << theme_ini;

    ThemeSettings ts;
    ASSERT_TRUE(ts.load((tmp_dir_ / "settings.ini").string(),
        (tmp_dir_ / "theme").string(), "vrunner"));
    EXPECT_EQ(ts.engine(), "vrunner");
    EXPECT_NEAR(ts.ws()->fs_act->text.color.g, 1.0, 1e-6) <<
        "theme colors apply to a theme that does not pin an engine";
}

TEST_F(SettingsTest, EnginesLoad)
{
    // Each engine name must load (emerald_engine_load true).
    const std::vector<std::string> engines = {
        "legacy", "truglass", "vrunner", "oxygen",
        "zootreeves", "pixmap", "line"};

    const std::string engine_dir = EMERALDWF_TEST_ENGINE_DIR;
    for (const auto & name : engines)
    {
        EXPECT_TRUE(emerald_engine_load(name, engine_dir)) <<
            "engine " << name << " failed to load from " << engine_dir;
        emerald_engine_unload();
    }
}

// ── get_b_t_offset (part 06 regression: "minimize icon is restore") ─────────
//
// The button surface arrays are indexed by B_* (bistate pairs interleaved:
// MAXIMIZE=1/RESTORE=2, SHADE=6/UNSHADE=7, ABOVE=8/UNABOVE=9, STICK=10/
// UNSTICK=11), not by the separate B_T_* enum. The draw path does
// `b = get_b_t_offset(b_t); if (bistate && state) b++;` — so this mapping
// must land on the base surface, with +1 reaching the bistate twin.
TEST_F(SettingsTest, ButtonTOffset_MapsToSurfaceSpace)
{
    EXPECT_EQ(get_b_t_offset(B_T_CLOSE), B_CLOSE);
    EXPECT_EQ(get_b_t_offset(B_T_MAXIMIZE), B_MAXIMIZE);
    EXPECT_EQ(get_b_t_offset(B_T_MINIMIZE), B_MINIMIZE);
    EXPECT_EQ(get_b_t_offset(B_T_HELP), B_HELP);
    EXPECT_EQ(get_b_t_offset(B_T_MENU), B_MENU);
    EXPECT_EQ(get_b_t_offset(B_T_SHADE), B_SHADE);
    EXPECT_EQ(get_b_t_offset(B_T_ABOVE), B_ABOVE);
    EXPECT_EQ(get_b_t_offset(B_T_STICKY), B_STICK);
    EXPECT_EQ(get_b_t_offset(B_T_SUPERMAXIMIZE), B_SUPERMAXIMIZE);
}

TEST_F(SettingsTest, ButtonTOffset_BistateStepReachesTwin)
{
    // The +1 bistate step in the draw path must select the twin surface.
    EXPECT_EQ(get_b_t_offset(B_T_MAXIMIZE) + 1, B_RESTORE);
    EXPECT_EQ(get_b_t_offset(B_T_SHADE) + 1, B_UNSHADE);
    EXPECT_EQ(get_b_t_offset(B_T_ABOVE) + 1, B_UNABOVE);
    EXPECT_EQ(get_b_t_offset(B_T_STICKY) + 1, B_UNSTICK);
    // Non-bistate buttons (CLOSE, MINIMIZE, HELP, MENU, SUPERMAXIMIZE per
    // btbistate[] in titlebar.h) have no twin; the draw path never applies
    // the +1 step to them, so only their base surface is used.
}

// ── Shadow texture (part 06 regression: "no shadow" — large_shadow_surface
// was never generated in the port, so draw_shadow_background short-circuited
// to a transparent paint for every engine). ─────────────────────────────────
// The tiling (draw_shadow_background) maps texture origin to frame origin;
// the texture is the FULL reference frame's shadow field, with the sliver
// (the window silhouette) at the window's position in the frame:
// [left_space-ext.left, d_width-right) x [top_space-ext.top,
// d_height-bottom). See create_large_shadow_surface.
static int sliver_l(const window_settings *ws)
{
    return ws->left_space - ws->win_extents.left;
}

static int sliver_t(const window_settings *ws)
{
    return ws->top_space - ws->win_extents.top;
}

static int sliver_w(const window_settings *ws, int tex_w)
{
    return tex_w - ws->left_space - ws->right_space + ws->win_extents.left +
           ws->win_extents.right;
}

static int sliver_h(const window_settings *ws, int tex_h)
{
    return tex_h - ws->top_space - ws->bottom_space + ws->win_extents.top +
           ws->win_extents.bottom;
}

TEST_F(SettingsTest, ShadowTexture_GeneratedForShadowedTheme)
{
    ThemeSettings ts;
    ASSERT_TRUE(ts.load(defaults_dir() + "/settings.ini",
        defaults_dir() + "/theme"));
    window_settings *ws = ts.ws();

    // The default theme enables a shadow (radius 11.5, opacity 0.8).
    EXPECT_GT(ws->shadow_radius, 0.0);
    ASSERT_NE(ws->large_shadow_surface, nullptr) <<
        "shadow texture not generated";
    ASSERT_NE(ws->shadow_pattern, nullptr);

    // Content size: the full reference frame, with the sliver at the
    // window's position in it.
    const int w = cairo_image_surface_get_width(ws->large_shadow_surface);
    const int h = cairo_image_surface_get_height(ws->large_shadow_surface);
    EXPECT_GT(w, sliver_l(ws) + sliver_w(ws, w));
    EXPECT_GT(h, sliver_t(ws) + sliver_h(ws, h));

    const int stride =
        cairo_image_surface_get_stride(ws->large_shadow_surface);
    const unsigned char *data =
        (const unsigned char*)
        cairo_image_surface_get_data(ws->large_shadow_surface);
    auto alpha_at = [&] (int x, int y)
    {
        return data[y * stride + x * 4 + 3];
    };
    // Dense core: where the 2*size box sits fully inside the (offset)
    // sliver, coverage is 1 and alpha saturates at shadow_opacity * 255.
    // The core starts at (sliver_l + offset_x + size, sliver_t +
    // offset_y + size).
    const int size = (int)(ws->shadow_radius * 2 + 2) / 2;
    const unsigned char max_a =
        alpha_at(sliver_l(ws) + ws->shadow_offset_x + size + 1,
            sliver_t(ws) + ws->shadow_offset_y + size + 1);
    EXPECT_GE(max_a,
        (unsigned char)(ws->shadow_opacity * 255.0 * 0.9)) <<
        "no dense core in the shadow texture";
}

// ── Drop-shadow geometry (the silhouette — the engine-rendered frame's
// alpha channel, carrying the theme's per-corner rounding — is box-blurred
// with kernel width 2*size, placed at (0,0) in the texture with its edges
// on the corner split lines [0, left+1) x [0, top+1) — which the 9-region
// tiling in draw_shadow_background maps onto the window's outer box — and
// then shifted by (shadow_offset_x, shadow_offset_y): the shadow's dense
// edge lands exactly the offset away from the window's outer edge, falling
// toward +x/+y. The reserved spread per side (size ∓ offset) fully contains
// the outward fade, so the texture edges are zero past the fade. Coverage:
// ~50% on the silhouette boundary, full `size` px inside, zero `size` px
// outside.) ───────────────────────────────────────────────────────────────
// Re-runs update_shadow + texture generation for modified radius/offsets
// (update_shadow is exported so tests drive the derived state directly
// instead of duplicating the arithmetic).
static void regrow_shadow(ThemeSettings & ts, double radius, int offset_x,
    int offset_y)
{
    window_settings *ws = ts.ws();
    ws->shadow_radius   = radius;
    ws->shadow_offset_x = offset_x;
    ws->shadow_offset_y = offset_y;
    update_shadow(ts.fs_active());
    create_large_shadow_surface(ts);
}

static unsigned char alpha_at(cairo_surface_t *s, int x, int y)
{
    const unsigned char *data =
        (const unsigned char*)cairo_image_surface_get_data(s);
    return data[cairo_image_surface_get_stride(s) * y + 4 * x + 3];
}

TEST_F(SettingsTest, ShadowTexture_DropShadowFallsDownward)
{
    ThemeSettings ts;
    ASSERT_TRUE(ts.load(defaults_dir() + "/settings.ini",
        defaults_dir() + "/theme"));
    window_settings *ws = ts.ws();

    // radius 4 -> size 5; offset_y = size shifts the silhouette a full size
    // down: no spread above the window, spread below is size + offset.
    regrow_shadow(ts, 4, 0, 5);
    cairo_surface_t *tex = ws->large_shadow_surface;
    const int h = cairo_image_surface_get_height(tex);

    EXPECT_EQ(ws->shadow_top_space, 0) <<
        "a full-size downward offset must leave no shadow space above";
    EXPECT_EQ(ws->shadow_bottom_space, 10);

    const int size = (int)(ws->shadow_radius * 2 + 2) / 2;
    const int sl_t = sliver_t(ws);
    // Sample in the dense core's x range.
    const int x = sliver_l(ws) + ws->shadow_offset_x + size + 1;

    // The dense edge sits exactly (sliver_t + offset_y + size) below the
    // texture's top; the row just above it is already substantial.
    EXPECT_GE(alpha_at(tex, x, sl_t + ws->shadow_offset_y + size), 200) <<
        "no dense core at the dense edge";
    EXPECT_GT(alpha_at(tex, x, sl_t + ws->shadow_offset_y + size - 1), 150);
    // Nothing above the window's top edge (sliver_t rows) is dense: with a
    // full-size offset the window top is at the texture top, so row 0 is
    // the (occluded) fade tail, not the core.
    for (int y = 0; y < sl_t; y++)
    {
        EXPECT_LT(alpha_at(tex, x, y), 200) <<
            "dense shadow above the window's top edge";
    }

    // The outward fade reaches the reserved spread's edge: the spread is
    // size + offset, the fade needs `size` past the shifted sliver edge
    // (sliver_t + sliver_h + offset_y + size rows = the texture's bottom
    // edge exactly).
    EXPECT_GT(alpha_at(tex, x, h - 1), 0) <<
        "the fade must reach the reserved spread's edge";
}

TEST_F(SettingsTest, ShadowTexture_DropShadowFallsRightward)
{
    ThemeSettings ts;
    ASSERT_TRUE(ts.load(defaults_dir() + "/settings.ini",
        defaults_dir() + "/theme"));
    window_settings *ws = ts.ws();

    // radius 4 -> size 5; offset_x = size shifts the silhouette a full size
    // right: no spread left of the window, spread right is size + offset.
    regrow_shadow(ts, 4, 5, 0);
    cairo_surface_t *tex = ws->large_shadow_surface;
    const int w = cairo_image_surface_get_width(tex);

    EXPECT_EQ(ws->shadow_left_space, 0) <<
        "a full-size rightward offset must leave no shadow space on the "
        "left";
    EXPECT_EQ(ws->shadow_right_space, 10);

    const int size = (int)(ws->shadow_radius * 2 + 2) / 2;
    const int sl_t = sliver_t(ws);
    const int sl   = sliver_l(ws);
    // Sample in the dense core's y range.
    const int y = sl_t + ws->shadow_offset_y + size + 1;

    // The dense edge sits exactly (sliver_l + offset_x + size) right of the
    // texture's left; the column just left of it is already substantial.
    EXPECT_GE(alpha_at(tex, sl + ws->shadow_offset_x + size, y), 200) <<
        "no dense core at the dense edge";
    EXPECT_GT(
        alpha_at(tex, sl + ws->shadow_offset_x + size - 1, y), 150);
    // Nothing left of the window's left edge (sliver_l columns) is dense:
    // with a full-size offset the window left is at the texture left, so
    // column 0 is the (occluded) fade tail, not the core.
    for (int xx = 0; xx < sl; xx++)
    {
        EXPECT_LT(alpha_at(tex, xx, y), 200) <<
            "dense shadow left of the window's left edge";
    }

    // The outward fade reaches the reserved spread's edge: the spread is
    // size + offset, the fade needs `size` past the shifted sliver edge
    // (= the texture's right edge exactly).
    EXPECT_GT(alpha_at(tex, w - 1, y), 0) <<
        "the fade must reach the reserved spread's edge";
}

TEST_F(SettingsTest, ShadowTexture_DropShadowAsymmetricSpread)
{
    ThemeSettings ts;
    ASSERT_TRUE(ts.load(defaults_dir() + "/settings.ini",
        defaults_dir() + "/theme"));
    window_settings *ws = ts.ws();

    // Partial offset: the shadow exists on all sides, but the reserved
    // spread is size - offset on the minus side and size + offset on the
    // plus side (the sliver's position carries the shift).
    regrow_shadow(ts, 4, 2, 2);
    cairo_surface_t *tex = ws->large_shadow_surface;
    const int h = cairo_image_surface_get_height(tex);

    EXPECT_EQ(ws->shadow_top_space, 3);
    EXPECT_EQ(ws->shadow_bottom_space, 7);
    EXPECT_EQ(ws->shadow_left_space, 3);
    EXPECT_EQ(ws->shadow_right_space, 7);

    const int size = (int)(ws->shadow_radius * 2 + 2) / 2;
    // Sample in the dense core.
    const int x = sliver_l(ws) + ws->shadow_offset_x + size + 1;
    const int y = sliver_t(ws) + ws->shadow_offset_y + size + 1;

    // Above the window: the shadow exists within the reduced top space
    // (size - offset = 3 rows above the window's top edge), ramping up
    // toward the window.
    EXPECT_GT(alpha_at(tex, x, 0), 0) <<
        "no shadow in the reduced top space";
    EXPECT_GT(alpha_at(tex, x, 1), alpha_at(tex, x, 0)) <<
        "the top fade must ramp up toward the window";
    // Below the window: the fade reaches the reserved spread's edge
    // (sliver_t + sliver_h + offset_y + size rows = the texture's bottom
    // edge exactly).
    EXPECT_GT(alpha_at(tex, x, h - 1), 0) <<
        "the bottom fade must reach the reserved spread's edge";
    // Left margin (reduced): the shadow exists within the first column,
    // ramping up toward the window.
    EXPECT_GT(alpha_at(tex, 0, y), 0) <<
        "no shadow in the reduced left space";
    EXPECT_GT(alpha_at(tex, 1, y), alpha_at(tex, 0, y)) <<
        "the left fade must ramp up toward the window";
    // Right: the fade reaches the reserved spread's edge.
    EXPECT_GT(
        alpha_at(tex, cairo_image_surface_get_width(tex) - 1, y), 0) <<
        "the right fade must reach the reserved spread's edge";
}

// The silhouette carries the theme's per-corner rounding (top corners
// rounded, bottom square): the frame's corner arc removes silhouette
// material at the top-left corner, so the shadow there must be more
// transparent than the straight edge at the same distance — the old stacked
// layers left square, equal-alpha corners.
TEST_F(SettingsTest, ShadowTexture_CornersAreRounded)
{
    ThemeSettings ts;
    ASSERT_TRUE(ts.load(defaults_dir() + "/settings.ini",
        defaults_dir() + "/theme"));
    window_settings *ws = ts.ws();

    regrow_shadow(ts, 4, 0, 0); // spread 5, kernel width 2*size = 10
    cairo_surface_t *tex = ws->large_shadow_surface;

    // The silhouette's top-left corner sits at (sliver_l, sliver_t) = the
    // window's outer top-left corner in frame coords. The default theme
    // rounds the top corners with an arc (radius 11.5) centered at
    // (sliver_l+11.5, sliver_t+11.5): the point 2px diagonally inside the
    // corner is OUTSIDE the arc (frame transparent there), while a point on
    // the straight edge at the same x-distance is inside (opaque border).
    const int sl = sliver_l(ws);
    const int st = sliver_t(ws);
    EXPECT_LT(alpha_at(tex, sl - 2, st - 2), alpha_at(tex, sl + 2, st + 8)) <<
        "shadow corner not rounded (no less transparent than the edge)";
    EXPECT_GT(alpha_at(tex, sl + 2, st + 8), 100) <<
        "the straight edge must be dense at the same distance";
    // The corner decays toward zero off the diagonal.
    EXPECT_LT(alpha_at(tex, sl, st), alpha_at(tex, sl + 2, st + 8)) <<
        "corner does not decay off the diagonal";
}

TEST_F(SettingsTest, ShadowTexture_CenteredWhenNoOffset)
{
    ThemeSettings ts;
    ASSERT_TRUE(ts.load(defaults_dir() + "/settings.ini",
        defaults_dir() + "/theme"));
    window_settings *ws = ts.ws();

    // No offset: the spread is symmetric (size on every side) and the fade
    // is present 1 px past the sliver edge on all four sides. The
    // sliver's edges sit at (sliver_l, sliver_t) and (sliver_l+sliver_w,
    // sliver_t+sliver_h); the box kernel (2*size+1 samples) reaches
    // `size` px past each edge, so the fade spans to the reserved
    // spread's edge exactly.
    regrow_shadow(ts, 4, 0, 0);
    cairo_surface_t *tex = ws->large_shadow_surface;
    const int w = cairo_image_surface_get_width(tex);
    const int h = cairo_image_surface_get_height(tex);

    EXPECT_EQ(ws->shadow_top_space, ws->shadow_bottom_space);
    EXPECT_EQ(ws->shadow_left_space, ws->shadow_right_space);

    const int size = (int)(ws->shadow_radius * 2 + 2) / 2;
    const int sl   = sliver_l(ws);
    const int st   = sliver_t(ws);
    const int sw   = sliver_w(ws, w);
    const int sh   = sliver_h(ws, h);
    const int cx   = sl + size + 2;  // deep in the dense core
    const int cy   = st + size + 2;

    // A point deep in the core is dense.
    EXPECT_GE(alpha_at(tex, cx, cy),
        (unsigned char)(ws->shadow_opacity * 255.0 * 0.9)) <<
        "no dense core in the centered shadow";

    // Each side has a fade: present 1 px outside the sliver edge and at the
    // reserved spread's edge, ramping monotonically toward the core.
    // Top (edge at st, fade in rows < st):
    EXPECT_GT(alpha_at(tex, cx, st - 1), 0) << "top fade missing";
    EXPECT_GT(alpha_at(tex, cx, st - size + 1), 0) <<
        "top fade must reach the spread's edge";
    EXPECT_GT(alpha_at(tex, cx, st - size + 2), alpha_at(tex, cx, st - size + 1)) <<
        "top fade must ramp up toward the window";
    // Bottom (edge at st+sh, fade in rows > st+sh):
    EXPECT_GT(alpha_at(tex, cx, st + sh + 1), 0) << "bottom fade missing";
    EXPECT_GT(alpha_at(tex, cx, st + sh + size - 1), 0) <<
        "bottom fade must reach the spread's edge";
    EXPECT_GT(alpha_at(tex, cx, st + sh + size - 2),
        alpha_at(tex, cx, st + sh + size - 1)) <<
        "bottom fade must ramp down away from the window";
    // Left (edge at sl, fade in cols < sl):
    EXPECT_GT(alpha_at(tex, sl - 1, cy), 0) << "left fade missing";
    EXPECT_GT(alpha_at(tex, sl - size + 1, cy), 0) <<
        "left fade must reach the spread's edge";
    EXPECT_GT(alpha_at(tex, sl - size + 2, cy),
        alpha_at(tex, sl - size + 1, cy)) <<
        "left fade must ramp up toward the window";
    // Right (edge at sl+sw, fade in cols > sl+sw):
    EXPECT_GT(alpha_at(tex, sl + sw + 1, cy), 0) << "right fade missing";
    EXPECT_GT(alpha_at(tex, sl + sw + size - 1, cy), 0) <<
        "right fade must reach the spread's edge";
    EXPECT_GT(alpha_at(tex, sl + sw + size - 2, cy),
        alpha_at(tex, sl + sw + size - 1, cy)) <<
        "right fade must ramp down away from the window";
}

// The texture must be visible to cairo COMPOSITING, not just to raw data
// reads — the generator writes pixels directly, which cairo (1.14+) only
// sees after cairo_surface_mark_dirty. Without it the 9-region tiling in
// draw_shadow_background renders a fully transparent shadow.
TEST_F(SettingsTest, ShadowTexture_VisibleToCairoCompositing)
{
    ThemeSettings ts;
    ASSERT_TRUE(ts.load(defaults_dir() + "/settings.ini",
        defaults_dir() + "/theme"));
    window_settings *ws = ts.ws();
    ASSERT_NE(ws->large_shadow_surface, nullptr);

    const int w = cairo_image_surface_get_width(ws->large_shadow_surface);
    const int h = cairo_image_surface_get_height(ws->large_shadow_surface);
    cairo_surface_t *bg = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
        w, h);
    cairo_t *cr = cairo_create(bg);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    cairo_set_source_surface(cr, ws->large_shadow_surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    const unsigned char *d =
        (const unsigned char*)cairo_image_surface_get_data(bg);
    const int stride = cairo_image_surface_get_stride(bg);

    // A point inside the dense core (the core starts at
    // (sliver_l + offset_x + size, sliver_t + offset_y + size)): the white
    // background must be darkened by the shadow coverage.
    const int size = (int)(ws->shadow_radius * 2 + 2) / 2;
    const int px   = sliver_l(ws) + ws->shadow_offset_x + size + 1;
    const int py   = sliver_t(ws) + ws->shadow_offset_y + size + 1;
    EXPECT_LT(d[stride * py + 4 * px + 2], 200) <<
        "shadow texture invisible to cairo compositing";
}

TEST_F(SettingsTest, ShadowTexture_NoShadowTheme_NoTexture)
{
    // A theme without a shadow must leave both fields NULL (the
    // "no shadow" path of emerald's generator).
    fs::path theme = tmp_dir_ / "theme";
    {
        std::ofstream out(theme / "theme.ini");
        out << "[engine]\nengine=legacy\n\n" <<
            "[shadow]\n" <<
            "shadow_radius=0\n" <<
            "shadow_offset_x=0\n" <<
            "shadow_offset_y=0\n";
    }
    ThemeSettings ts;
    ASSERT_TRUE(ts.load((tmp_dir_ / "settings.ini").string(),
        theme.string()));
    window_settings *ws = ts.ws();
    EXPECT_EQ(ws->large_shadow_surface, nullptr);
    EXPECT_EQ(ws->shadow_pattern, nullptr);
}

TEST_F(SettingsTest, ShadowTexture_ColorMatchesSetting)
{
    // part 06 finding 11: load_shadow_color_setting() stores 16-bit X11
    // components (c.red * 65536, the original emerald convention). The
    // texture generator must normalize by 65535 — with the (buggy) 255
    // divisor every non-black shadow color clamps to WHITE, and the tiled
    // shadow ring then paints a white band across the frame.
    //
    // Note: the color must be UNSATURATED. With a pure primary like
    // #ff0000 the 16-bit value is 65536, which both divisors clamp to
    // 1.0 (65536/65535 and 65536/255 > 1) — indistinguishable. #808080
    // (128/255) is 32896 in 16-bit: /65535 -> 128, /255 -> 255.
    fs::path theme = tmp_dir_ / "theme";
    {
        std::ofstream out(theme / "theme.ini");
        out << "[engine]\nengine=legacy\n\n" <<
            "[shadow]\n" <<
            "shadow_color=#808080\n" <<
            "shadow_opacity=1\n" <<
            "shadow_radius=12\n" <<
            "shadow_offset_x=0\n" <<
            "shadow_offset_y=0\n";
    }
    ThemeSettings ts;
    ASSERT_TRUE(ts.load((tmp_dir_ / "settings.ini").string(),
        theme.string()));
    window_settings *ws = ts.ws();

    // 16-bit storage: (128/255) * 65536 -> 32896 (truncated).
    EXPECT_EQ(ws->shadow_color[0], 32896);
    EXPECT_EQ(ws->shadow_color[1], 32896);
    EXPECT_EQ(ws->shadow_color[2], 32896);
    ASSERT_NE(ws->large_shadow_surface, nullptr);

    // Sample the texture center (inside the silhouette). The rendered
    // silhouette is not fully opaque there (the blur reaches the hollow
    // corner notches), so un-premultiply the channels: stored channel =
    // color * (alpha/255)  ->  color = channel * 255 / alpha.
    const int w = cairo_image_surface_get_width(ws->large_shadow_surface);
    const int h = cairo_image_surface_get_height(ws->large_shadow_surface);
    const int stride =
        cairo_image_surface_get_stride(ws->large_shadow_surface);
    const unsigned char *p =
        (const unsigned char*)cairo_image_surface_get_data(
            ws->large_shadow_surface) +
        (h / 2) * stride + (w / 2) * 4;
    // ARGB32 bytes: [B, G, R, A]. Gray 128 shadow -> un-premultiplied color
    // ~128 on every channel. The 255-divisor bug clamped the color to white
    // (255), which un-premultiply would expose as ~255.
    const int a = p[3];
    ASSERT_GT(a, 0) << "center fully transparent";
    EXPECT_NEAR(p[2] * 255.0 / a, 128, 8) << "red: " << (int)p[2] <<
        " alpha: " << a;
    EXPECT_NEAR(p[1] * 255.0 / a, 128, 8) << "green: " << (int)p[1] <<
        " alpha: " << a;
    EXPECT_NEAR(p[0] * 255.0 / a, 128, 8) << "blue: " << (int)p[0] <<
        " alpha: " << a;
}

// ── part 06 finding 14: engine teardown on live engine switching ───────────
//
// The engine is a process-wide global (e_draw) whose per-ws state is
// `ws->engine_ws` (the engine's private_ws). The original emerald ran
// fini_engine only at shutdown; the port added runtime engine switching
// (engine option + live reload), so fini MUST free the private state —
// the pre-fix fini leaked pws (and the zootreeves/pixmap cairo pixmap
// surfaces) on every switch, and the stale window_settings (engine_ws of
// a dlclose'd, *different* engine) kept being rendered by the new
// engine's code (foreign-struct read) — the root of the "crash after
// ~5 engine switches" report.
TEST(EngineLifecycle, ReloadFreesEnginePrivateState)
{
    window_settings *ws =
        (window_settings*)calloc(1, sizeof(window_settings));
    frame_settings *f1 = (frame_settings*)calloc(1, sizeof(frame_settings));
    frame_settings *f2 = (frame_settings*)calloc(1, sizeof(frame_settings));
    ws->fs_act   = f1;
    ws->fs_inact = f2;

    const std::string eng = EMERALDWF_TEST_ENGINE_DIR;
    ASSERT_TRUE(emerald_engine_load("zootreeves", eng));
    emerald_engine_init_engine(ws);
    EXPECT_NE(ws->engine_ws, nullptr) << "init_engine must allocate pws";
    const void *pws1 = ws->engine_ws;

    // Theme-only reload (same engine): the keep-.so fast path must still
    // fini the previous ws (freeing its pws).
    ASSERT_TRUE(emerald_engine_load("zootreeves", eng));
    EXPECT_EQ(ws->engine_ws, nullptr) <<
        "old ws's pws must be freed on same-engine reload";

    // Engine switch: the leaving engine's fini already ran; the new engine
    // must init FRESH state, never reusing the old pws pointer.
    ASSERT_TRUE(emerald_engine_load("legacy", eng));
    emerald_engine_init_engine(ws);
    EXPECT_NE(ws->engine_ws, nullptr);
    EXPECT_NE(ws->engine_ws, pws1);

    emerald_engine_unload();
    EXPECT_EQ(ws->engine_ws, nullptr) << "unload must fini the engine";

    free(f1);
    free(f2);
    free(ws);
}

// Review F3: the `emeraldwf/engine` option name feeds the dlopen() path
// ("lib" + name + ".so"). It is user/WCM-editable (poller-synced from the
// ini), so a hostile value like "../../tmp/evil" would dlopen an
// attacker-placed .so on a shared-config box. The predicate must accept
// exactly [A-Za-z0-9_-]+ and emerald_engine_load() must refuse the rest
// BEFORE any path is built (no dlopen, no state churn).
TEST(EngineName, IsValidEngineNameAcceptsOnlyAlnumDashUnderscore)
{
    using wf::emeraldwf::is_valid_engine_name;
    // Every shipped engine (meson engine_names) must pass.
    EXPECT_TRUE(is_valid_engine_name("legacy"));
    EXPECT_TRUE(is_valid_engine_name("line"));
    EXPECT_TRUE(is_valid_engine_name("vrunner"));
    EXPECT_TRUE(is_valid_engine_name("truglass"));
    EXPECT_TRUE(is_valid_engine_name("oxygen"));
    EXPECT_TRUE(is_valid_engine_name("pixmap"));
    EXPECT_TRUE(is_valid_engine_name("zootreeves"));
    // Custom engines with dash/underscore/digits are legitimate.
    EXPECT_TRUE(is_valid_engine_name("my-engine"));
    EXPECT_TRUE(is_valid_engine_name("engine_v2"));
    EXPECT_TRUE(is_valid_engine_name("A0"));
    // Path components / separators / empty must not.
    EXPECT_FALSE(is_valid_engine_name(""));
    EXPECT_FALSE(is_valid_engine_name("../../tmp/evil"));
    EXPECT_FALSE(is_valid_engine_name("./evil"));
    EXPECT_FALSE(is_valid_engine_name("libevil.so"));
    EXPECT_FALSE(is_valid_engine_name("evil/x"));
    EXPECT_FALSE(is_valid_engine_name("/abs/evil"));
    EXPECT_FALSE(is_valid_engine_name("evil name"));
}

TEST(EngineName, LoadRefusesUnsanizedName)
{
    const std::string eng = EMERALDWF_TEST_ENGINE_DIR;
    emerald_engine_unload(); // deterministic start (no engine loaded)

    // The guard fires before the .so path is ever built — refused load
    // leaves the loader in its previous (unloaded) state.
    EXPECT_FALSE(emerald_engine_load("../../tmp/evil", eng));
    EXPECT_FALSE(emerald_engine_load("libevil.so", eng));

    // The loader must still work after a refusal.
    EXPECT_TRUE(emerald_engine_load("legacy", eng));
    emerald_engine_unload();
}

// WCM's metadata parser segfaults on an EMPTY <value></value> element: the
// string-option desc loop dereferences n->children->content without a null
// check (children is NULL for an empty element). The emeraldwf metadata must
// never ship an empty value element (the Engine Override combo entry uses
// the marker string "no_override" instead).
TEST(Metadata, NoEmptyValueElements)
{
    const std::string path =
        std::string(EMERALDWF_TEST_METADATA_DIR) + "/emeraldwf.xml";
    std::ifstream in(path);
    ASSERT_TRUE(in.good()) << path;
    std::string content((std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    EXPECT_EQ(content.find("<value></value>"), std::string::npos) <<
        "empty <value> segfaults WCM's metadata parser";
    EXPECT_EQ(content.find("<value/>"), std::string::npos);
    size_t pos = 0;
    while ((pos = content.find("<value>", pos)) != std::string::npos)
    {
        const size_t start = pos + std::string("<value>").size();
        const size_t end   = content.find("</value>", start);
        ASSERT_NE(end, std::string::npos);
        EXPECT_GT(end, start) <<
            "empty <value> at offset " << pos << " segfaults WCM";
        pos = end;
    }
}

// ── Part 06 (product): titlebar click actions configurable via WCM ─────────
//
// parse_titlebar_action() maps the emeraldwf/double_click_action and
// middle_click_action option strings to emerald's titlebar action enum.
// Empty = no override (keep settings.ini); unknown = no override (fallback,
// so a typo in the wayfire config never breaks click behavior).
TEST(TitlebarAction, ParseAllValues)
{
    struct
    {
        const char *name;
        int expected;
    } cases[] = {
        {"shade", DOUBLE_CLICK_SHADE},
        {"maximize", DOUBLE_CLICK_MAXIMIZE},
        {"minimize", DOUBLE_CLICK_MINIMIZE},
        {"send_to_back", DOUBLE_CLICK_SEND_TO_BACK},
        {"close", DOUBLE_CLICK_CLOSE},
        {"none", DOUBLE_CLICK_NONE},
    };
    for (const auto & c : cases)
    {
        auto v = parse_titlebar_action(c.name);
        ASSERT_TRUE(v.has_value()) << c.name;
        EXPECT_EQ(*v, c.expected) << c.name;
    }
}

TEST(TitlebarAction, CaseInsensitiveAndTrimmed)
{
    EXPECT_EQ(parse_titlebar_action("MAXIMIZE"), DOUBLE_CLICK_MAXIMIZE);
    EXPECT_EQ(parse_titlebar_action("  Minimize  "), DOUBLE_CLICK_MINIMIZE);
    EXPECT_EQ(parse_titlebar_action("Send_To_Back"), DOUBLE_CLICK_SEND_TO_BACK);
}

TEST(TitlebarAction, EmptyAndUnknownYieldNoOverride)
{
    EXPECT_FALSE(parse_titlebar_action("").has_value());
    EXPECT_FALSE(parse_titlebar_action("   ").has_value());
    EXPECT_FALSE(parse_titlebar_action("bogus").has_value());
}

TEST_F(SettingsTest, TitlebarActionOverrideAppliesToLoadedTheme)
{
    // Load the default theme, then apply the WCM override exactly the way
    // the plugin does (parse_titlebar_action → ws field). The loader no
    // longer reads the click actions from settings.ini (WCM owns them);
    // the structural default is DOUBLE_CLICK_SHADE; the override must
    // win.
    ThemeSettings ts;
    ASSERT_TRUE(ts.load(defaults_dir() + "/settings.ini",
        defaults_dir() + "/theme"));
    window_settings *ws     = ts.ws();
    const int default_value = ws->double_click_action;
    EXPECT_EQ(default_value, DOUBLE_CLICK_SHADE);

    auto act = parse_titlebar_action("maximize");
    ASSERT_TRUE(act.has_value());
    ws->double_click_action = *act;
    EXPECT_EQ(ws->double_click_action, DOUBLE_CLICK_MAXIMIZE);
    EXPECT_NE(ws->double_click_action, default_value);

    // Unknown value → nullopt → the plugin assigns nothing, so the
    // structural default stands (a typo in the wayfire config is a
    // no-op, not a behavior change).
    auto bogus = parse_titlebar_action("bogus");
    EXPECT_FALSE(bogus.has_value());
}

// ── Finding 30: the click-action override is CLEARABLE ─────────────────────
//
// Regression: the option callback only wrote ws when the parse succeeded,
// so once `double_click_action = maximize` was set, an unparseable value
// (typo) left `maximize` stuck in ws until a full theme reload.
// resolve_titlebar_action() fixes the contract: an empty/unknown value
// resolves to the caller's default (emerald's own, at the call sites).
TEST_F(SettingsTest, ResolveTitlebarActionFallsBackToDefault)
{
    // An explicit action wins (WCM defines the click action).
    EXPECT_EQ(resolve_titlebar_action("maximize", 0),
        DOUBLE_CLICK_MAXIMIZE);
    EXPECT_EQ(resolve_titlebar_action("SHADE", 4), DOUBLE_CLICK_SHADE);
    EXPECT_EQ(resolve_titlebar_action("send_to_back", 0),
        DOUBLE_CLICK_SEND_TO_BACK);
    EXPECT_EQ(resolve_titlebar_action("none", 3), DOUBLE_CLICK_NONE);

    // Empty / whitespace → the passed default stands (not a reset to 0).
    EXPECT_EQ(resolve_titlebar_action("", 3), 3);
    EXPECT_EQ(resolve_titlebar_action("   ", 1), 1);

    // Unknown / typo → the passed default, not a behavior change.
    EXPECT_EQ(resolve_titlebar_action("bogus", 2), 2);
}

// Finding 42 — a theme swap must leave the DecorContext's frame_settings
// pointer VALID. Regression: emerald_decorator_t::set_theme() did not
// refresh ctx.fs (only the node's render() did), so after reload_theme()
// — any settings.ini / theme.ini / engine change — the decorator's
// ctx.fs dangled into the DESTROYED previous ThemeSettings. The first
// pointer event after the reload (hover → layout.hit_button →
// ctx.fs->ws) used the freed memory and wayfire segfaulted: the whole
// session died (user report: pulse toggle in the theme editor, then
// hover over a titlebar button).
TEST_F(SettingsTest, DecorCtxFsTracksThemeSwap)
{
    ThemeSettings ts_a;
    ASSERT_TRUE(ts_a.load(defaults_dir() + "/settings.ini",
        defaults_dir() + "/theme"));
    ThemeSettings ts_b;
    ASSERT_TRUE(ts_b.load(defaults_dir() + "/settings.ini",
        defaults_dir() + "/theme"));
    ASSERT_NE(ts_a.fs_active(), ts_b.fs_active());

    DecorContext ctx;
    ctx.active = true;
    decor_ctx_set_theme(ctx, ts_a, ctx.active);
    ASSERT_EQ(ctx.fs, ts_a.fs_active());

    // The swap emerald_decorator_t::set_theme performs on reload.
    decor_ctx_set_theme(ctx, ts_b, ctx.active);
    EXPECT_EQ(ctx.fs, ts_b.fs_active());
    EXPECT_NE(ctx.fs, ts_a.fs_active());

    // Inactive state selects the inactive frame_settings.
    ctx.active = false;
    decor_ctx_set_theme(ctx, ts_b, ctx.active);
    EXPECT_EQ(ctx.fs, ts_b.fs_inactive());
}

// Finding 43 — button fade/pulse state machine (upstream emerald's
// counters[]/pulsating[] + fade timer, src/main.c:1388-1607). Regression:
// emeraldwf only ported draw_buttons_without_fade — use_button_fade /
// use_button_fade_pulse had NO effect. The fade is a crossfade between the
// button's non-hovered and hovered surface columns (the button itself
// never fades out); the pulse breathes the HIGHLIGHT while a button is
// hovered, bottoming out at the "pulse min opacity" floor.
TEST_F(SettingsTest, ButtonFadeIdleStaysNonHovered)
{
    // A never-hovered button stays idle: the timer skips it and it draws
    // the non-hovered surface at full opacity.
    ButtonFadeConfig cfg;
    cfg.enabled   = true;
    cfg.num_steps = 5;
    ButtonFadeState st;
    auto r = button_fade_tick(cfg, st);
    EXPECT_FALSE(r.changed);
    EXPECT_FALSE(r.animating);
    auto a = button_fade_alphas(st, cfg.num_steps);
    EXPECT_DOUBLE_EQ(a.normal, 1.0);
    EXPECT_DOUBLE_EQ(a.hover, 0.0);
}

TEST_F(SettingsTest, ButtonFadeHoverInAndOut)
{
    ButtonFadeConfig cfg;
    cfg.enabled   = true;
    cfg.num_steps = 5;

    // Hovering an idle button starts the fade in (counter 0 → 1) and the
    // highlight crosses over to full over num steps, then settles at the
    // max (steady "at max fade" — the timer stops).
    ButtonFadeState st;
    button_fade_on_hover(cfg, st, true);
    ASSERT_EQ(st.counter, 1);
    int ticks = 0;
    for (int i = 0; i < 12; i++)
    {
        auto r = button_fade_tick(cfg, st);
        if (r.changed)
        {
            ticks++;
        }

        if (!r.animating)
        {
            break;
        }
    }

    EXPECT_EQ(ticks, 5); // 1→2, 2→3, …, 5→6
    EXPECT_EQ(st.counter, 6); // num+1 = at max fade
    auto a = button_fade_alphas(st, cfg.num_steps);
    EXPECT_DOUBLE_EQ(a.hover, 1.0);
    EXPECT_DOUBLE_EQ(a.normal, 0.0);

    // Leaving the button mirrors the counter negative (1 - min(c, num+1)
    // = -num) and the highlight fades back over num steps to idle.
    button_fade_on_hover(cfg, st, false);
    ASSERT_EQ(st.counter, -5);
    ticks = 0;
    for (int i = 0; i < 12; i++)
    {
        auto r = button_fade_tick(cfg, st);
        if (r.changed)
        {
            ticks++;
        }

        if (!r.animating)
        {
            break;
        }
    }

    EXPECT_EQ(ticks, 5); // -5→-4, …, -1→0
    EXPECT_EQ(st.counter, 0);
    a = button_fade_alphas(st, cfg.num_steps);
    EXPECT_DOUBLE_EQ(a.hover, 0.0);
    EXPECT_DOUBLE_EQ(a.normal, 1.0);
}

TEST_F(SettingsTest, ButtonFadeMidFadeReversal)
{
    ButtonFadeConfig cfg;
    cfg.enabled   = true;
    cfg.num_steps = 5;
    ButtonFadeState st;
    st.counter = 3; // mid fade-in (hover alpha 2/5)
    button_fade_on_hover(cfg, st, false);
    // Fade out resumes at the current alpha (1 - 3 = -2), not from the top.
    EXPECT_EQ(st.counter, -2);
    button_fade_on_hover(cfg, st, true);
    // Re-hovering mirrors it back (1 - (-2) = 3).
    EXPECT_EQ(st.counter, 3);
}

TEST_F(SettingsTest, ButtonFadePressStopsFade)
{
    ButtonFadeConfig cfg;
    cfg.enabled   = true;
    cfg.num_steps = 5;
    ButtonFadeState st;
    st.counter   = 3;
    st.pulsating = true;
    button_fade_on_press(st);
    EXPECT_EQ(st.counter, 0);
    EXPECT_FALSE(st.pulsating);
    // The pressed/idle button draws the non-hovered (pressed) column fully.
    auto a = button_fade_alphas(st, cfg.num_steps);
    EXPECT_DOUBLE_EQ(a.normal, 1.0);
    EXPECT_DOUBLE_EQ(a.hover, 0.0);
}

TEST_F(SettingsTest, ButtonFadePulseBreathesHighlight)
{
    // The pulse runs while the button is HOVERED: the highlight breathes
    // between full and the min-opacity floor, holding `wait` steps at the
    // top — it never settles, and the button never disappears.
    ButtonFadeConfig cfg;
    cfg.enabled   = true;
    cfg.pulse     = true;
    cfg.num_steps = 10;
    cfg.pulse_len_steps  = 4; // floor = 1 - 4/10 = 0.6
    cfg.pulse_wait_steps = 2;

    ButtonFadeState st;
    button_fade_on_hover(cfg, st, true); // fade in, counter → 1
    double min_seen = 1.0, max_seen = 0.0;
    bool saw_pulsating = false;
    bool measuring     = false;
    for (int i = 0; i < 80; i++)
    {
        auto r = button_fade_tick(cfg, st);
        ASSERT_TRUE(r.animating) <<
            "the pulse must not settle while the button is hovered";
        if (!measuring)
        {
            // The first fade-in (counter 1 → num) is a plain fade, not the
            // pulse — only measure once the pulse is active.
            measuring = st.pulsating;
            continue;
        }

        saw_pulsating = true;
        auto a = button_fade_alphas(st, cfg.num_steps);
        min_seen = std::min(min_seen, a.hover);
        max_seen = std::max(max_seen, a.hover);
    }

    EXPECT_TRUE(saw_pulsating);
    EXPECT_DOUBLE_EQ(max_seen, 1.0) << "the pulse reaches full highlight";
    EXPECT_GE(min_seen, 0.6 - 1e-9) <<
        "the pulse bottoms out at the min-opacity floor, never at 0";

    // Unhovering stops the pulse: the highlight fades out to idle.
    button_fade_on_hover(cfg, st, false);
    ASSERT_FALSE(st.pulsating);
    for (int i = 0; i < 24; i++)
    {
        auto r = button_fade_tick(cfg, st);
        if (!r.animating)
        {
            break;
        }
    }

    EXPECT_EQ(st.counter, 0);
    auto a = button_fade_alphas(st, cfg.num_steps);
    EXPECT_DOUBLE_EQ(a.hover, 0.0);
    EXPECT_DOUBLE_EQ(a.normal, 1.0);
}

// Finding 44 — the mtime poller's stamp must see same-second rewrites.
// Regression: the poller compared st.st_mtime, which has ONE-SECOND
// granularity. The themer's apply is debounced 500 ms and the plugin
// polls every 500 ms, so a check-then-uncheck within the same second
// left the mtime unchanged — the plugin never reloaded and e.g. the
// button pulse kept breathing after the checkbox was unchecked.
TEST_F(SettingsTest, LoaderLeavesWcmOwnedKeysUntouched)
{
    // Part 08: no_scroll_shade, use_decoration_cropping, and the button
    // fade/pulse set are WCM-owned — the loader must NOT read them from
    // settings.ini (one setting, one editor; the values in settings.ini
    // are stale after migration).
    std::string theme_ini =
        "[engine]\n"
        "engine=truglass\n"
        "[truglass_settings]\n";
    std::ofstream(tmp_dir_ / "theme" / "theme.ini") << theme_ini;

    std::ofstream sf(tmp_dir_ / "settings.ini");
    sf << "[buttons]\n" <<
        "use_button_fade = true\n" <<
        "button_fade_total_duration = 500\n" <<
        "[titlebars]\n" <<
        "no_scroll_shade = true\n" <<
        "[decorations]\n" <<
        "use_decoration_cropping = true\n";
    sf.close();

    ThemeSettings ts;
    bool ok = ts.load((tmp_dir_ / "settings.ini").string(),
        (tmp_dir_ / "theme").string());
    ASSERT_TRUE(ok);

    auto *ws = ts.ws();
    EXPECT_FALSE(ws->use_button_fade);
    EXPECT_FALSE(ws->titlebar_no_scroll_shade);
    EXPECT_FALSE(ws->use_decoration_cropping);
    // The structural defaults stand: the ini's total_duration = 500 must
    // NOT have been derived into num_steps (= 10 at the 50 ms step) — that
    // mapping is WCM's job now (emeraldwf_apply_button_fade).
    EXPECT_EQ(ws->button_fade_num_steps, 5);
    EXPECT_EQ(ws->button_fade_pulse_len_steps, 0);
}

TEST_F(SettingsTest, ApplyButtonFadeMapsDurationsToSteps)
{
    // Part 08: emeraldwf_apply_button_fade() converts the WCM option
    // values (ms, percent) into the ws step fields the node consumes —
    // the same derivation emerald's own loader used (main.c:5753-5779).
    auto *ws = new window_settings();
    std::memset(ws, 0, sizeof(*ws));

    emeraldwf_apply_button_fade(ws, true, true,
        500 /*total*/, 50 /*step*/,
        200 /*pulse wait*/, 50 /*min opacity*/);
    EXPECT_TRUE(ws->use_button_fade);
    EXPECT_EQ(ws->button_fade_step_duration, 50);
    EXPECT_EQ(ws->button_fade_num_steps, 10); // 500/50
    EXPECT_EQ(ws->button_fade_pulse_len_steps, 5); // 10*(100-50)/100
    EXPECT_EQ(ws->button_fade_pulse_wait_steps, 4); // 200/50

    // Pulse off → the pulse fields are zeroed (the node derives
    // `pulse = pulse_len_steps > 0`).
    emeraldwf_apply_button_fade(ws, true, false,
        200, 50, 200, 50);
    EXPECT_EQ(ws->button_fade_num_steps, 4);
    EXPECT_EQ(ws->button_fade_pulse_len_steps, 0);
    EXPECT_EQ(ws->button_fade_pulse_wait_steps, 0);

    // Min opacity clamps to [0, 100] (a bad WCM value must not make the
    // pulse negative or exceed num_steps).
    emeraldwf_apply_button_fade(ws, true, true,
        200, 50, 0, 150);
    EXPECT_EQ(ws->button_fade_pulse_len_steps, 0); // (100-100)
    emeraldwf_apply_button_fade(ws, true, true,
        200, 50, 0, -10);
    EXPECT_EQ(ws->button_fade_pulse_len_steps, 4); // (100-0)/100 * 4

    // Degenerate durations: total/step ≤ 0 fall back to sane values
    // (one step, default 50 ms step), never 0.
    emeraldwf_apply_button_fade(ws, true, false,
        0, 0, 0, 0);
    EXPECT_EQ(ws->button_fade_num_steps, 1);
    EXPECT_EQ(ws->button_fade_step_duration, 50);

    delete ws;
}

TEST_F(SettingsTest, FileStampDetectsSameSecondRewrites)
{
    fs::path dir = fs::temp_directory_path() / "emeraldwf-stamp";
    fs::path p   = dir / (std::to_string(::getpid()) + ".ini");
    fs::create_directories(dir);

    auto write_ini = [&] (const char *content)
    {
        std::ofstream out(p.string());
        out << content;
    };
    write_ini("use_button_fade_pulse = true\n");
    const std::uint64_t s1 = emwf_file_stamp(p.string());
    write_ini("use_button_fade_pulse = false\n");
    const std::uint64_t s2 = emwf_file_stamp(p.string());

    ASSERT_NE(s1, 0u) << "an existing file must have a non-zero stamp";
    EXPECT_NE(s1, s2) <<
        "a same-second rewrite must be visible to the poller";
    EXPECT_EQ(emwf_file_stamp(p.string() + ".missing"), 0u);

    fs::remove_all(dir);
}

// ── Finding 24: WCM option changes reach the in-memory config manager ──────
//
// Regression: WCM is a separate process — toggling a checkbox only writes
// the ini, it never notifies the running wayfire, which (0.11) does not
// auto-reload its config. The mtime poller used to sync ONLY the four
// string options (engine, theme_dir, double/middle_click_action), so
// checkbox edits like enable_shade never reached the plugin: unchecking did
// nothing ("when i uncheck Enable Shade it still works"), and after a
// restart re-checking did nothing either ("now when i check Enable Shade it
// doesn't work"). sync_emeraldwf_options_from_config() now covers EVERY
// emeraldwf option, and set_value() fires the option callbacks that apply
// the side effects (binding add/remove, decorator publish, unshade-all).

class ConfigSyncTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        ini_path_ = (fs::temp_directory_path() /
            "emeraldwf-config-sync-test.ini").string();
    }

    void write_ini(const std::string & content)
    {
        std::ofstream(ini_path_) << content;
    }

    template<typename T>
    std::shared_ptr<wf::config::option_t<T>> make_opt(const char *name, const T & def)
    {
        return std::make_shared<wf::config::option_t<T>>(name, def);
    }

    wf::animation_description_t animation(int ms, const char *name = "linear")
    {
        // Test-local smoothing lambda: the headless test binary interposes
        // an empty local smoothing::linear over libwf-config's.
        auto linear = [] (double x) { return x; };
        return wf::animation_description_t{
            ms, wf::animation::smoothing::smooth_function(linear), name};
    }

    using cb_t = wf::config::option_base_t::updated_callback_t;

    std::string ini_path_;
};

TEST_F(ConfigSyncTest, AppliesEveryOptionTypeAndFiresCallbacks)
{
    auto engine  = make_opt<std::string>("emeraldwf/engine", "legacy");
    auto borders = make_opt<bool>("emeraldwf/maximized_borders", true);
    auto shade   = make_opt<bool>("emeraldwf/enable_shade", true);
    auto tb  = make_opt<int>("emeraldwf/csd_titlebar_height", 64);
    auto pad = make_opt<int>("emeraldwf/shadow_pad", 0);
    auto mod = make_opt<wf::keybinding_t>("emeraldwf/shade_modifier",
        wf::keybinding_t(0, 0));
    auto dur = make_opt<wf::animation_description_t>(
        "emeraldwf/shade_duration", animation(1000));
    // Part 08: the WCM-owned global emerald settings.
    auto no_scroll = make_opt<bool>("emeraldwf/no_scroll_shade", false);
    auto cropping  = make_opt<bool>("emeraldwf/use_decoration_cropping", false);
    auto fade = make_opt<bool>("emeraldwf/use_button_fade", true);
    auto fade_pulse   = make_opt<bool>("emeraldwf/use_button_fade_pulse", true);
    auto fade_total   = make_opt<int>("emeraldwf/button_fade_total_duration", 250);
    auto fade_step    = make_opt<int>("emeraldwf/button_fade_step_duration", 50);
    auto fade_wait    = make_opt<int>("emeraldwf/button_fade_pulse_wait_duration", 0);
    auto fade_opacity =
        make_opt<int>("emeraldwf/button_fade_pulse_min_opacity", 0);

    int callbacks = 0;
    wf::config::option_base_t::updated_callback_t cb =
        [&callbacks] { ++callbacks; };
    engine->add_updated_handler(&cb);
    borders->add_updated_handler(&cb);
    shade->add_updated_handler(&cb);
    tb->add_updated_handler(&cb);
    pad->add_updated_handler(&cb);
    mod->add_updated_handler(&cb);
    dur->add_updated_handler(&cb);
    no_scroll->add_updated_handler(&cb);
    cropping->add_updated_handler(&cb);
    fade->add_updated_handler(&cb);
    fade_pulse->add_updated_handler(&cb);
    fade_total->add_updated_handler(&cb);
    fade_step->add_updated_handler(&cb);
    fade_wait->add_updated_handler(&cb);
    fade_opacity->add_updated_handler(&cb);

    // Exactly the kind of [emeraldwf] section WCM writes on save.
    write_ini("[emeraldwf]\n"
              "engine = truglass\n"
              "maximized_borders = false\n"
              "enable_shade = false\n"
              "csd_titlebar_height = 42\n"
              "shadow_pad = 8\n"
              "shade_modifier = <ctrl> <super>\n"
              "shade_duration = 300ms linear\n"
              "no_scroll_shade = true\n"
              "use_decoration_cropping = true\n"
              "use_button_fade = false\n"
              "use_button_fade_pulse = false\n"
              "button_fade_total_duration = 500\n"
              "button_fade_step_duration = 100\n"
              "button_fade_pulse_wait_duration = 300\n"
              "button_fade_pulse_min_opacity = 40\n");

    emerald_config_options_t targets{engine, nullptr, nullptr, nullptr,
        nullptr, nullptr, borders, nullptr, shade, tb, pad, mod, dur,
        no_scroll, cropping, fade, fade_pulse, fade_total, fade_step,
        fade_wait, fade_opacity};
    EXPECT_TRUE(sync_emeraldwf_options_from_config(ini_path_, targets)) <<
        "changed values must be reported so the poller arms a reload";

    EXPECT_EQ(engine->get_value(), "truglass");
    EXPECT_FALSE(borders->get_value());
    EXPECT_FALSE(shade->get_value()) <<
        "the enable_shade checkbox is the regression itself";
    EXPECT_EQ(tb->get_value(), 42);
    EXPECT_EQ(pad->get_value(), 8);

    auto expected_mod =
        wf::option_type::from_string<wf::keybinding_t>("<ctrl> <super>");
    ASSERT_TRUE(expected_mod.has_value());
    EXPECT_EQ(mod->get_value(), *expected_mod);

    EXPECT_EQ(dur->get_value().length_ms, 300);

    // Part 08: the global emerald settings sync too (the user's "uncheck
    // pulse and it keeps breathing" report is this path).
    EXPECT_TRUE(no_scroll->get_value());
    EXPECT_TRUE(cropping->get_value());
    EXPECT_FALSE(fade->get_value());
    EXPECT_FALSE(fade_pulse->get_value());
    EXPECT_EQ(fade_total->get_value(), 500);
    EXPECT_EQ(fade_step->get_value(), 100);
    EXPECT_EQ(fade_wait->get_value(), 300);
    EXPECT_EQ(fade_opacity->get_value(), 40);

    EXPECT_EQ(callbacks, 15) <<
        "set_value() must fire each option's callback (that is how the "
        "plugin applies the side effects)";
}

TEST_F(ConfigSyncTest, LeavesAbsentKeysAlone)
{
    // The bug's other half: a WCM save rewrites the whole file, so options
    // the user did NOT touch must keep their in-memory values (and must not
    // fire callbacks — a spurious enable_shade callback would re-shade /
    // unshade every window on every unrelated WCM edit).
    auto engine = make_opt<std::string>("emeraldwf/engine", "legacy");
    auto shade  = make_opt<bool>("emeraldwf/enable_shade", true);
    auto tb     = make_opt<int>("emeraldwf/csd_titlebar_height", 64);

    int shade_cb = 0, tb_cb = 0;
    cb_t shade_handler = [&shade_cb] { ++shade_cb; };
    cb_t tb_handler = [&tb_cb] { ++tb_cb; };
    shade->add_updated_handler(&shade_handler);
    tb->add_updated_handler(&tb_handler);

    write_ini("[emeraldwf]\nengine = truglass\n");

    // Review F2 (-Wmissing-field-initializers): named-field init — all
    // options are null unless registered (and robust to struct reordering).
    emerald_config_options_t targets;
    targets.engine = engine;
    targets.enable_shade = shade;
    targets.csd_titlebar_height = tb;
    EXPECT_TRUE(sync_emeraldwf_options_from_config(ini_path_, targets));
    EXPECT_EQ(engine->get_value(), "truglass");
    EXPECT_TRUE(shade->get_value());
    EXPECT_EQ(tb->get_value(), 64);
    EXPECT_EQ(shade_cb, 0);
    EXPECT_EQ(tb_cb, 0);
}

TEST_F(ConfigSyncTest, NoChangeMeansNoCallbacksAndFalse)
{
    auto engine = make_opt<std::string>("emeraldwf/engine", "truglass");
    auto shade  = make_opt<bool>("emeraldwf/enable_shade", true);

    int engine_cb = 0, shade_cb = 0;
    cb_t engine_handler = [&engine_cb] { ++engine_cb; };
    cb_t shade_handler = [&shade_cb] { ++shade_cb; };
    engine->add_updated_handler(&engine_handler);
    shade->add_updated_handler(&shade_handler);

    write_ini("[emeraldwf]\nengine = truglass\nenable_shade = true\n");

    // Review F2 (-Wmissing-field-initializers): named-field init.
    emerald_config_options_t targets;
    targets.engine = engine;
    targets.enable_shade = shade;
    EXPECT_FALSE(sync_emeraldwf_options_from_config(ini_path_, targets)) <<
        "unchanged values must not report a change (the poller would "
        "re-arm the debounced theme reload on every 500 ms tick)";
    EXPECT_EQ(engine_cb, 0);
    EXPECT_EQ(shade_cb, 0);

    // Idempotent: a second pass over the same file changes nothing.
    EXPECT_FALSE(sync_emeraldwf_options_from_config(ini_path_, targets));
}

TEST_F(ConfigSyncTest, IgnoresUnparseableValues)
{
    auto tb  = make_opt<int>("emeraldwf/csd_titlebar_height", 64);
    auto dur = make_opt<wf::animation_description_t>(
        "emeraldwf/shade_duration", animation(1000));
    auto shade = make_opt<bool>("emeraldwf/enable_shade", true);

    int tb_cb = 0, dur_cb = 0;
    cb_t tb_handler = [&tb_cb] { ++tb_cb; };
    cb_t dur_handler = [&dur_cb] { ++dur_cb; };
    tb->add_updated_handler(&tb_handler);
    dur->add_updated_handler(&dur_handler);

    write_ini("[emeraldwf]\n"
              "csd_titlebar_height = abc\n"
              "shade_duration = banana\n"
              "enable_shade = false\n");

    // Review F2 (-Wmissing-field-initializers): named-field init.
    emerald_config_options_t targets;
    targets.enable_shade = shade;
    targets.csd_titlebar_height = tb;
    targets.shade_duration = dur;
    EXPECT_TRUE(sync_emeraldwf_options_from_config(ini_path_, targets));
    EXPECT_FALSE(shade->get_value());
    EXPECT_EQ(tb->get_value(), 64);
    EXPECT_EQ(dur->get_value().length_ms, 1000);
    EXPECT_EQ(tb_cb, 0) << "an unparseable value must not touch the option";
    EXPECT_EQ(dur_cb, 0);
}

TEST_F(ConfigSyncTest, MissingFileReturnsFalse)
{
    auto shade = make_opt<bool>("emeraldwf/enable_shade", true);
    int cb     = 0;
    cb_t handler = [&cb] { ++cb; };
    shade->add_updated_handler(&handler);

    // Review F2 (-Wmissing-field-initializers): named-field init.
    emerald_config_options_t targets;
    targets.enable_shade = shade;
    EXPECT_FALSE(sync_emeraldwf_options_from_config(
        "/nonexistent/emeraldwf/wayfire.ini", targets));
    EXPECT_TRUE(shade->get_value());
    EXPECT_EQ(cb, 0);
}

// ---- R1 (issue #1): the pixmap-asset probe must match the files the
// pixmap engine ACTUALLY loads (pixmap.c p_types[]: 11 `active_` textures,
// plus 11 `inactive_` ones when [pixmap_settings]
// inactive_use_active_pixmaps is not true — the engine's private struct is
// zero-initialized, so the key absent → the inactive set is required).
// The pre-fix probe checked five filenames no theme ships (pixmaps.top.png
// vs the engine's pixmaps.active_top.png) with any_of: permanently false
// for correct themes (pixmap engine dead → gray fallback) and, even if
// "fixed" to the right names, any_of would let a PARTIAL set through and
// the engine would dereference NULL surfaces (compositor segfault). ----
namespace
{
class PixmapAssetsTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        const auto name =
            testing::UnitTest::GetInstance()->current_test_info()->name();
        dir_ = fs::temp_directory_path() /
            (fs::path("emeraldwf-pixmaps") / std::to_string(::getpid()) /
                name);
        fs::create_directories(dir_);
    }

    void TearDown() override
    {
        fs::remove_all(dir_);
    }

    void touch(const std::string &name)
    {
        std::ofstream(dir_ / ("pixmaps." + name + ".png")) << "png";
    }

    void make_set(const char *prefix)
    {
        for (auto t : PIXMAP_TYPES)
        {
            touch(std::string(prefix) + t);
        }
    }

    void write_theme_ini(const std::string &content)
    {
        std::ofstream(dir_ / "theme.ini") << content;
    }

    fs::path dir_;
};

TEST_F(PixmapAssetsTest, BothSetsComplete_ReturnsTrue)
{
    make_set("active_");
    make_set("inactive_");
    EXPECT_TRUE(theme_has_pixmap_assets(dir_.string()));
}

TEST_F(PixmapAssetsTest, FullActiveSet_WithoutInactive_ReturnsFalse)
{
    // The engine's private struct is zero-initialized, so a theme with
    // only the active set (no [pixmap_settings]) still loads the
    // inactive set — and would dereference NULL surfaces for it.
    make_set("active_");
    EXPECT_FALSE(theme_has_pixmap_assets(dir_.string()));
}

TEST_F(PixmapAssetsTest, PartialActiveSet_ReturnsFalse)
{
    // Exactly the five names the pre-fix probe checked: with any_of they
    // pass, but the engine needs all eleven and would dereference the
    // six missing surfaces.
    for (const char *t : {"top", "left", "right", "bottom", "title"})
    {
        touch(std::string("active_") + t);
    }
    EXPECT_FALSE(theme_has_pixmap_assets(dir_.string()));
}

TEST_F(PixmapAssetsTest, InactiveSetRequired_OneMissingFile)
{
    make_set("active_");
    make_set("inactive_");
    fs::remove(dir_ / "pixmaps.inactive_title.png");
    EXPECT_FALSE(theme_has_pixmap_assets(dir_.string()));
}

TEST_F(PixmapAssetsTest, InactiveSetNotRequired_WhenReusingActive)
{
    make_set("active_");
    write_theme_ini(
        "[pixmap_settings]\ninactive_use_active_pixmaps=true\n");
    EXPECT_TRUE(theme_has_pixmap_assets(dir_.string()));
}

TEST_F(PixmapAssetsTest, EmptyDir_ReturnsFalse)
{
    EXPECT_FALSE(theme_has_pixmap_assets(dir_.string()));
}
}
}
