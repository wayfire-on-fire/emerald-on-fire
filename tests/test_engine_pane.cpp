// Part 07 regression: engine module lifetime + shared settings registry
// (findings 46/47 — the "Round Top Left Corner" crash).
//
// Background: the port originally vendored libemeraldengine as a STATIC
// archive into every engine .so. append_engine() then dlclosed the engine
// module after building the pane — refcount zero, module unmapped — while
// the pane's widgets kept "toggled"/"value-changed" callbacks into the
// module's code (cb_apply_setting). The first click in the engine pane
// dispatched a signal callback into freed mmap: SEGV_MAPERR. The same
// per-.so vendoring also left each .so's global_theme_file uninitialized
// and its apply flag false, so engine-pane edits were silently never
// written to theme.ini.
//
// Upstream builds libemeraldengine as ONE shared library (see
// libengine/emeraldengine.pc.in: Libs: -lemeraldengine): one SettingList,
// one global_theme_file, one apply flag per process. This test pins that
// invariant:
// 1. after init_engine_list(), libemeraldengine.so is mapped and the
// single SettingList holds the engines' registrations;
// 2. toggling a registered engine checkbox survives (no SEGV) and
// reaches the write path: theme.ini in a sandboxed HOME carries the
// new value after the 500 ms debounce.
//
// On the buggy static-vendored build: (1) fails — the engine modules are
// dlclose-unmapped and each re-dlopen yields a fresh empty registry — and
// (2) segfaults (callback into the freed module code).
//
// The init flow (engine scan) runs ONCE per process — a gtest::Environment
// — because the shared SettingList is process-global and the real theme
// editor calls init_engine_list() exactly once.
//
// Needs a real display (GTK event loop + dlopen of the installed engines);
// skips when none is available.

#include <gtest/gtest.h>

#include <glib.h>
#include <gtk/gtk.h>

// The vendored libengine headers carry no extern "C" guards (C project);
// keep the C linkage from a C++ translation unit without modifying them.
extern "C" {
#include <libengine.h>
}

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/types.h>

// Review R2: the engine dir forwarded by meson (configure_file) — the scan
// below seeds the sandbox's ~/.emerald/engines from it, so the test works
// in out-of-tree builds where the baked-in ENGINE_DIR (the install
// location) is empty.
#include "test-paths.h"

// Same interposition as the real themer (themer/emerald-of.c): under
// wayfire, file writes ARE the live reload (the plugin's mtime poller);
// do not shell out to killall from a test.
void send_reload_signal()
{}

namespace
{
std::filesystem::path g_home;
GtkWidget *g_nvbox = nullptr;

SettingItem *find_item(const char *section, const char *key)
{
    for (GSList *l = get_setting_list(); l; l = l->next)
    {
        SettingItem *s = static_cast<SettingItem*>(l->data);
        if (s->section && s->key &&
            (g_strcmp0(s->section, section) == 0) &&
            (g_strcmp0(s->key, key) == 0))
        {
            return s;
        }
    }

    return nullptr;
}

bool module_mapped(const std::string & needle)
{
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line))
    {
        if (line.find(needle) != std::string::npos)
        {
            return true;
        }
    }

    return false;
}

// Set when no display is available; each test then GTEST_SKIPs.
bool g_no_display = false;

// One-shot process init, in the theme editor's exact order
// (themer/main.c). The sandbox HOME must be set and verified before
// gtk_init: the apply path writes ~/.emerald/theme/theme.ini via
// g_get_home_dir() (cached on first use) — a leaked real home would
// destroy the user's live theme.
class EnginePaneEnv : public ::testing::Environment
{
  public:
    void SetUp() override
    {
        std::ostringstream name;
        name << "emeraldwf-engine-pane-" << getpid();
        g_home = std::filesystem::temp_directory_path() / name.str();
        std::filesystem::create_directories(g_home / ".emerald" / "theme");
        std::filesystem::create_directories(g_home / ".emerald" / "themes");
        // Review R2: seed the sandbox's local engine dir with the BUILT
        // engines (meson-forwarded path) — init_engine_list() scans
        // ~/.emerald/engines first, then the baked-in ENGINE_DIR (the
        // install location), which is empty on a dev box without an
        // install. Out-of-tree, the .so's are NOT under the source root,
        // so without this the scan finds nothing and the registry
        // assertions fail. Symlinks are enough for dlopen; names already
        // present in ENGINE_DIR dedupe (engine_is_unique).
        {
            std::filesystem::create_directories(
                g_home / ".emerald" / "engines");
            const std::filesystem::path engine_dir{EMERALDWF_TEST_ENGINE_DIR};
            std::error_code ec;
            if (std::filesystem::is_directory(engine_dir, ec))
            {
                for (const auto & entry :
                     std::filesystem::directory_iterator(engine_dir))
                {
                    const auto & p = entry.path();
                    // Review F2: `name` shadows the ostringstream above.
                    const std::string so_name = p.filename().string();
                    if ((so_name.rfind("lib", 0) != 0) ||
                        (p.extension().string() != ".so"))
                    {
                        continue;
                    }

                    std::filesystem::create_symlink(
                        p, g_home / ".emerald" / "engines" / so_name, ec);
                }
            }
        }
        {
            std::ofstream out(g_home / ".emerald" / "theme" / "theme.ini");
            out <<
                "[theme]\n"
                "name=regress\n"
                "version=0.8\n"
                "\n"
                "[engine]\n"
                "engine=vrunner\n"
                "\n"
                "[vrunner_settings]\n"
                "round_top_left=false\n";
        }
        setenv("HOME", g_home.c_str(), 1);
        EXPECT_STREQ(g_get_home_dir(), g_home.c_str());

        if (gtk_init_check(nullptr, nullptr) == FALSE)
        {
            g_no_display = true;
            return;
        }

        init_key_files();
        g_nvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        layout_engine_list(g_nvbox);
        init_engine_list(); // dlopens the installed engines
        init_settings(); // theme.ini -> shared global_theme_file
        set_apply(TRUE); // state after a theme was selected
    }

    void TearDown() override
    {
        if (g_nvbox)
        {
            gtk_widget_destroy(g_nvbox);
        }

        if (std::filesystem::exists(g_home))
        {
            std::filesystem::remove_all(g_home);
        }
    }
};

// Registered at static-init time; gtest runs it once around all tests.
struct EnginePaneEnvRegistrar
{
    EnginePaneEnvRegistrar()
    {
        ::testing::AddGlobalTestEnvironment(new EnginePaneEnv);
    }
};

static const EnginePaneEnvRegistrar env_registrar;
} // namespace

// Finding 46: the settings machinery must be one shared, live instance.
TEST(EnginePane, SharedRegistrySurvivesEngineScan)
{
    if (g_no_display)
    {
        GTEST_SKIP() << "no display available";
    }

    ASSERT_TRUE(module_mapped("libemeraldengine.so")) <<
        "libemeraldengine must be a loaded shared library";

    SettingItem *round = find_item("vrunner_settings", "round_top_left");
    ASSERT_NE(round, nullptr) <<
        "the engine scan must register the engine pane settings in the "
        "single shared SettingList (they are not in any dead per-.so "
        "copy)";
    // cppcheck-suppress dangerousTypeCast
    // GTK_IS_TOGGLE_BUTTON is a GTK type-check macro (safe idiom); cppcheck
    // can't see through the macro and flags the cast in its expansion.
    ASSERT_TRUE(GTK_IS_TOGGLE_BUTTON(round->widget));

    // 7 installed engines x dozens of settings + the themer's own items.
    int n = 0;
    for (GSList *l = get_setting_list(); l; l = l->next)
    {
        n++;
    }

    EXPECT_GE(n, 100) << "expected the full multi-engine registry";
}

// Finding 46 + 47: clicking an engine-pane checkbox must not crash and
// must persist through the shared write path.
TEST(EnginePane, EnginePaneTogglePersistsToThemeIni)
{
    if (g_no_display)
    {
        GTEST_SKIP() << "no display available";
    }

    SettingItem *round = find_item("vrunner_settings", "round_top_left");
    ASSERT_NE(round, nullptr);
    // cppcheck-suppress dangerousTypeCast
    // GTK_IS_TOGGLE_BUTTON is a GTK type-check macro (safe idiom); cppcheck
    // can't see through the macro and flags the cast in its expansion.
    ASSERT_TRUE(GTK_IS_TOGGLE_BUTTON(round->widget));

    // The click. (Buggy build: "toggled" dispatches cb_apply_setting in
    // the dlclose-unmapped engine module — SEGV_MAPERR.)
    // cppcheck-suppress dangerousTypeCast
    // cppcheck-suppress cstyleCast
    // GTK_TOGGLE_BUTTON is a GTK type-cast macro (safe downcast, validated
    // by the GTK_IS_TOGGLE_BUTTON check above); cppcheck can't see through
    // the macro and flags the C-style cast in its expansion.
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(round->widget), TRUE);

    // Run the loop past the 500 ms apply debounce.
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    g_timeout_add(700,
        +[] (gpointer p)->gboolean
    {
        g_main_loop_quit(static_cast<GMainLoop*>(p));
        return G_SOURCE_REMOVE;
    },
        loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);

    // The write path (shared global_theme_file + apply flag) persisted
    // the change to the sandbox theme.ini.
    std::ifstream ini(g_home / ".emerald" / "theme" / "theme.ini");
    ASSERT_TRUE(ini.good()) << "apply_settings must have written theme.ini";
    std::stringstream ss;
    ss << ini.rdbuf();
    EXPECT_NE(ss.str().find("round_top_left=true"), std::string::npos) <<
        "engine-pane edits must be persisted to theme.ini";
}
