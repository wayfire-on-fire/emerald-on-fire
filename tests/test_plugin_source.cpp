// test_plugin_source.cpp — source-structure test for the plugin TU
// (M4, issue #1).
//
// plugin.cpp cannot be linked headlessly (wayfire core symbols), so the
// fini() contract is verified textually: locate the body of
// emeraldwf_plugin::fini() and assert the engine-unload call is present
// and ordered after the decoration-removal loop.
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "test-paths.h"

namespace
{
std::string read_file(const std::string & path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The body of `void emeraldwf_plugin::fini()` (bracket-matched from the
// first '{' after the signature).
std::string fini_body(const std::string & src)
{
    const size_t sig = src.find("void emeraldwf_plugin::fini()");
    if (sig == std::string::npos)
    {
        return {};
    }

    const size_t open = src.find('{', sig);
    if (open == std::string::npos)
    {
        return {};
    }

    int depth = 0;
    for (size_t i = open; i < src.size(); i++)
    {
        if (src[i] == '{')
        {
            depth++;
        } else if (src[i] == '}')
        {
            depth--;
            if (depth == 0)
            {
                return src.substr(open + 1, i - open - 1);
            }
        }
    }

    return {};
}

// M4 (issue #1): fini() never called emerald_engine_unload(), so the
// engine .so (and libemeraldengine) stayed mapped for the life of the
// wayfire process on plugin unload. It must be called AFTER the
// decoration-removal loop — the decorators/nodes use the engine's draw
// callback while they live.
TEST(PluginSource, FiniUnloadsEngineAfterRemovingDecorations)
{
    const std::string src =
        read_file(std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/plugin.cpp");
    ASSERT_FALSE(src.empty()) << "src/plugin.cpp must be readable";

    const std::string body = fini_body(src);
    ASSERT_FALSE(body.empty()) <<
        "could not locate emeraldwf_plugin::fini() in plugin.cpp";

    const size_t loop = body.find("remove_decoration");
    ASSERT_NE(loop, std::string::npos) <<
        "fini() must remove the per-view decorations";

    const size_t unload = body.find("emerald_engine_unload()");
    ASSERT_NE(unload, std::string::npos) <<
        "fini() must call emerald_engine_unload() (pre-fix: the "
        "engine .so stayed mapped for the life of the wayfire "
        "process)";
    EXPECT_GT(unload, loop) <<
        "the engine is unloaded AFTER the decorations are removed "
        "(the decorators use the engine while they live)";
    EXPECT_EQ(body.find("emerald_engine_unload()", unload + 1),
        std::string::npos) <<
        "the engine is unloaded exactly once in fini()";
}

// The body of a `emeraldwf_plugin::<fn>(...)` member (bracket-matched from
// the first '{' after the signature), with // comments stripped (position
// assertions must not match identifiers quoted in comments — the M20 fix's
// own comment mentions adjust_new_decorations()).
std::string fn_body(const std::string & src, const std::string & sig)
{
    const size_t at = src.find(sig);
    if (at == std::string::npos)
    {
        return {};
    }

    const size_t open = src.find('{', at);
    if (open == std::string::npos)
    {
        return {};
    }

    int depth  = 0;
    size_t end = open;
    for (size_t i = open; i < src.size(); i++)
    {
        if (src[i] == '{')
        {
            depth++;
        } else if (src[i] == '}')
        {
            depth--;
            if (depth == 0)
            {
                end = i;
                break;
            }
        }
    }

    std::string body = src.substr(open + 1, end - open - 1);
    std::string out;
    for (size_t i = 0; i < body.size(); i++)
    {
        if ((body[i] == '/') && (i + 1 < body.size()) && (body[i + 1] == '/'))
        {
            while (i < body.size() && body[i] != '\n')
            {
                i++;
            }

            if (i < body.size())
            {
                out += '\n';
            }

            continue;
        }

        out += body[i];
    }

    return out;
}

// M19 (issue #1): the CSD shade-transformer get-or-create sequence
// (get_transformer → make_shared → add_transformer, plus the per-tick
// shadow-export registration) was triplicated: toggle_shade(),
// scroll_shade() here, and emerald_decorator_t::toggle_shade() in
// emerald-decorator-shade.cpp. The codebase already regressed on this
// duplication once (Review S1: the shadow-export callback was missing at
// one creation branch, leaving a wheel-shaded window with pre-shade shadow
// margins for the whole animation). All creation must go through the
// single shared helper get_or_create_shade() (emerald-shade.hpp).
TEST(PluginSource, CsdShadeCreationUsesSharedHelper)
{
    const std::string src =
        read_file(std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/plugin.cpp");
    ASSERT_FALSE(src.empty()) << "src/plugin.cpp must be readable";

    const std::string toggle_body =
        fn_body(src, "void emeraldwf_plugin::toggle_shade(");
    ASSERT_FALSE(toggle_body.empty()) << "could not locate toggle_shade()";
    EXPECT_NE(toggle_body.find("get_or_create_shade("), std::string::npos) <<
        "toggle_shade()'s CSD branch must use the shared "
        "get_or_create_shade() helper (M19) — pre-fix it had its own "
        "inline get-or-create + shadow-export registration";

    const std::string scroll_body =
        fn_body(src, "void emeraldwf_plugin::scroll_shade(");
    ASSERT_FALSE(scroll_body.empty()) << "could not locate scroll_shade()";
    EXPECT_NE(scroll_body.find("get_or_create_shade("), std::string::npos) <<
        "scroll_shade()'s CSD branch must use the shared "
        "get_or_create_shade() helper (M19) — pre-fix the Review S1 "
        "regression lived in exactly this duplicated branch";

    EXPECT_EQ(src.find("std::make_shared<wf::emeraldwf::emerald_shade_t>"),
        std::string::npos) <<
        "plugin.cpp must not create the shade transformer directly "
        "(M19) — creation is consolidated in get_or_create_shade()";
}

// M20 (issue #1): recreate() (option-change driven: maximized_borders /
// maximized_shadows) must only (re)decorate views should_decorate_view()
// accepts. Pre-fix it ran remove_decoration + adjust_new_decorations for
// EVERY toplevel, so a WCM checkbox / ini edit / config sync gave CSD
// windows and ignore_views-matched views full emerald frames, persisting
// until the next matcher change.
TEST(PluginSource, RecreateSkipsViewsTheMatcherRejects)
{
    const std::string src =
        read_file(std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/plugin.cpp");
    ASSERT_FALSE(src.empty()) << "src/plugin.cpp must be readable";

    const std::string body = fn_body(src, "void emeraldwf_plugin::recreate()");
    ASSERT_FALSE(body.empty()) << "could not locate recreate()";

    // ChatGPT follow-up: pin the NEGATED guard and the rejection branch's
    // continue — a bare should_decorate_view() call before an
    // adjust_new_decorations() call would satisfy a positional check
    // without fixing the bug.
    const size_t guard = body.find("!should_decorate_view(");
    ASSERT_NE(guard, std::string::npos) <<
        "recreate() must skip the views should_decorate_view() "
        "REJECTS (M20) — pre-fix it decorated every toplevel, so CSD / "
        "ignore_views-matched views got emerald frames on an option "
        "change";

    const size_t cont = body.find("continue", guard);
    ASSERT_NE(cont, std::string::npos) <<
        "the rejection branch must continue (skip the view)";

    const size_t adjust = body.find("adjust_new_decorations(");
    ASSERT_NE(adjust, std::string::npos) <<
        "recreate() must (re)create the decoration on accepted views "
        "(that is what propagates the new option values)";
    EXPECT_LT(cont, adjust) <<
        "the rejection branch must skip BEFORE any "
        "adjust_new_decorations() call (M20)";
}

// Strip `//` line comments from the whole source (same rule as fn_body),
// so the assertions below match CODE, not comment prose.
std::string strip_comments(const std::string & src)
{
    std::string out;
    for (size_t i = 0; i < src.size(); i++)
    {
        if ((src[i] == '/') && (i + 1 < src.size()) && (src[i + 1] == '/'))
        {
            while (i < src.size() && src[i] != '\n')
            {
                i++;
            }

            if (i < src.size())
            {
                out += '\n';
            }

            continue;
        }

        out += src[i];
    }

    return out;
}

// M29 (issue #1): the active wayfire config file was watched by mtime
// polling (find_wayfire_config_file() + /proc/self/cmdline parsing). The
// running wayfire already watches its own config file (inotify,
// workarounds/auto_reload_config) and emits wf::reload_config_signal
// after re-reading it — and the reload re-sets every registered option,
// firing the plugin's set_callback() handlers. The plugin must use that
// signal for the wayfire config; the mtime poller stays ONLY for the
// emerald-owned files (settings.ini, theme.ini, theme dir).
//
// ChatGPT review (M29): the signal is also emitted for PROGRAMMATIC
// option changes (wayfire IPC, ipc-rules set_config_options) — so the
// handler must NOT re-read the file and re-apply its values (that would
// revert a legitimate runtime change); it only arms the debounced theme
// reload. The file re-apply helper (M21) is removed with its last caller.
TEST(PluginSource, WayfireConfigUsesReloadSignalNotMtimePoll)
{
    const std::string src =
        read_file(std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/plugin.cpp");
    ASSERT_FALSE(src.empty()) << "src/plugin.cpp must be readable";
    const std::string code = strip_comments(src);

    // 1. The cmdline-based config-file discovery is gone (in code, not
    // just in comments).
    EXPECT_EQ(code.find("find_wayfire_config_file"), std::string::npos) <<
        "find_wayfire_config_file() (the /proc/self/cmdline parsing) "
        "must be replaced by wf::reload_config_signal (M29)";
    EXPECT_EQ(code.find("/proc/self/cmdline"), std::string::npos) <<
        "the plugin must not parse /proc/self/cmdline to locate the "
        "config file (M29)";

    // 2. The plugin declares a reload_config_signal connection and
    // connects it in init().
    EXPECT_NE(src.find("connection_t<wf::reload_config_signal>"),
        std::string::npos) <<
        "the plugin must hold a "
        "wf::signal::connection_t<wf::reload_config_signal> member (M29)";

    const std::string init = fn_body(src, "void emeraldwf_plugin::init()");
    ASSERT_FALSE(init.empty()) << "could not locate emeraldwf_plugin::init()";
    EXPECT_NE(init.find("connect(&on_config_reload)"), std::string::npos) <<
        "init() must connect the reload_config_signal (M29)";

    // 3. The handler arms the debounced theme reload and re-applies the
    // emeraldwf options through the lenient sync (M21) using the
    // WAYFIRE_CONFIG_FILE env var that wayfire's config backend sets
    // at init.
    const size_t handler = init.find("on_config_reload = [");
    ASSERT_NE(handler, std::string::npos) <<
        "init() must assign the on_config_reload handler (M29)";
    const size_t open = init.find('{', handler);
    ASSERT_NE(open, std::string::npos);

    int depth    = 0;
    size_t close = std::string::npos;
    for (size_t i = open; i < init.size(); i++)
    {
        if (init[i] == '{')
        {
            depth++;
        } else if (init[i] == '}')
        {
            depth--;
            if (depth == 0)
            {
                close = i;
                break;
            }
        }
    }

    ASSERT_NE(close, std::string::npos) << "unbalanced handler lambda";
    const std::string handler_body =
        init.substr(open + 1, close - open - 1);

    EXPECT_NE(handler_body.find("arm_reload()"), std::string::npos) <<
        "the reload_config_signal handler must arm the debounced "
        "theme reload (M29)";
    // ChatGPT review (M29): the signal is NOT file-specific — wayfire IPC
    // (ipc-rules set_config_options) emits it for programmatic option
    // changes too. Re-reading the file and re-applying its values would
    // revert a legitimate runtime change to the file's value. The option
    // manager stays authoritative; the handler must NOT re-read the file.
    EXPECT_EQ(handler_body.find("sync_emeraldwf_options_from_config"),
        std::string::npos) <<
        "the handler must not re-apply file values over the in-memory "
        "option manager (it would revert IPC-made runtime changes) "
        "(M29, ChatGPT review)";
    EXPECT_EQ(handler_body.find("WAYFIRE_CONFIG_FILE"), std::string::npos) <<
        "the handler must not read the config file path (the handler "
        "must not re-read the config file) (M29, ChatGPT review)";
    EXPECT_EQ(code.find("sync_emeraldwf_options_from_config"),
        std::string::npos) <<
        "the file re-apply helper (M21) is dead weight once the signal "
        "replaces the mtime poll — removed (M29, ChatGPT review)";
    EXPECT_EQ(code.find("emerald-config-sync.hpp"), std::string::npos) <<
        "plugin.cpp must no longer include the config-sync header "
        "(M29, ChatGPT review)";

    // 4. The wayfire config file is no longer in the mtime-watched set,
    // and poll_tick() has no config-change branch. The emerald-owned
    // files stay watched.
    const std::string wpaths = fn_body(src,
        "void emeraldwf_plugin::emerald_watched_paths(std::vector<std::string> & out)");
    ASSERT_FALSE(wpaths.empty()) <<
        "could not locate emerald_watched_paths()";
    EXPECT_EQ(wpaths.find("wayfire_config_file"), std::string::npos) <<
        "the mtime poller must watch only the emerald-owned files, not "
        "the wayfire config (M29)";
    EXPECT_NE(wpaths.find("last_settings_path"), std::string::npos) <<
        "settings.ini must stay mtime-watched (M29)";
    EXPECT_NE(wpaths.find("theme.ini"), std::string::npos) <<
        "theme.ini must stay mtime-watched (M29)";

    const std::string tick = fn_body(src, "bool emeraldwf_plugin::poll_tick()");
    ASSERT_FALSE(tick.empty()) << "could not locate poll_tick()";
    EXPECT_EQ(tick.find("config_changed"), std::string::npos) <<
        "poll_tick() must no longer special-case the wayfire config "
        "(M29)";
    EXPECT_EQ(tick.find("sync_emeraldwf_options_from_config"),
        std::string::npos) <<
        "the config sync belongs to the signal handler, not to the "
        "mtime poller (M29)";

    // 5. The emerald-files poller itself stays.
    EXPECT_NE(init.find("start_poller()"), std::string::npos) <<
        "init() must still start the emerald-files mtime poller (M29)";

    // 6. fini() disconnects the signal like every other connection, so a
    // config reload during plugin unload cannot re-enter the half-
    // destroyed plugin — and cancels the armed reload debounce, so a
    // pending 250 ms timer cannot fire on the destroyed plugin.
    const std::string fini = fn_body(src, "void emeraldwf_plugin::fini()");
    ASSERT_FALSE(fini.empty()) << "could not locate emeraldwf_plugin::fini()";
    EXPECT_NE(fini.find("on_config_reload.disconnect()"), std::string::npos) <<
        "fini() must disconnect on_config_reload (M29)";
    EXPECT_NE(fini.find("reload_timer.disconnect()"), std::string::npos) <<
        "fini() must cancel the reload debounce timer (M29, ChatGPT "
        "review: explicit signal disconnection does not cancel an "
        "already-armed timer)";
}

// Minor (issue #1): remove_decoration() erased the SHADOW_MARGIN_DATA_KEY
// unconditionally. A view that ends up UNDECORATED (CSD) with shadow_pad >
// 0 must keep its shadow — a standalone shadow plugin reads that key. The
// erase dropped the CSD margins without re-exporting them, so the CSD window
// lost its shadow. After the erase, an undecorated view with shadow_pad > 0
// must re-export the CSD shadow margins.
TEST(PluginSource, RemoveDecorationReexportsCsdShadowMargins)
{
    const std::string src =
        read_file(std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/plugin.cpp");
    ASSERT_FALSE(src.empty()) << "src/plugin.cpp must be readable";

    const std::string body =
        fn_body(src, "void emeraldwf_plugin::remove_decoration(");
    ASSERT_FALSE(body.empty()) <<
        "could not locate emeraldwf_plugin::remove_decoration()";

    // The erase of the key is still there...
    EXPECT_NE(body.find("SHADOW_MARGIN_DATA_KEY"), std::string::npos) <<
        "remove_decoration() must still erase the shadow-margin key for "
        "decorated views";
    // ...but an undecorated (CSD) view with shadow_pad > 0 must re-export
    // its CSD shadow margins (the erase dropped them).
    EXPECT_NE(body.find("export_csd_shadow_margins"), std::string::npos) <<
        "remove_decoration() must re-export the CSD shadow margins for "
        "undecorated views with shadow_pad > 0";
}

// Minor (issue #1): the global scroll-shade gesture honored
// no_scroll_shade for SSD views (the SSD path checks the theme's
// titlebar_no_scroll_shade inside emerald_decorator_t::scroll_shade) but NOT
// for CSD views (the CSD branch of the plugin's scroll_shade() never checked
// it) — an undocumented divergence. Check the option once up front in
// scroll_shade() so both SSD and CSD behave identically.
TEST(PluginSource, ScrollShadeHonorsNoScrollShadeForCsd)
{
    const std::string src =
        read_file(std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/plugin.cpp");
    ASSERT_FALSE(src.empty()) << "src/plugin.cpp must be readable";

    const std::string body =
        fn_body(src, "void emeraldwf_plugin::scroll_shade(");
    ASSERT_FALSE(body.empty()) <<
        "could not locate emeraldwf_plugin::scroll_shade()";

    EXPECT_NE(body.find("no_scroll_shade"), std::string::npos) <<
        "scroll_shade() must honor no_scroll_shade for CSD views (pre-fix "
        "only the SSD path checked titlebar_no_scroll_shade)";
}

// Minor (issue #1): reload_theme() logged "engine changed ov -> X" when
// new_theme->engine() != ov, comparing against the OVERRIDE. The override is
// empty when no override is set, so the log fired spuriously on every reload
// (e.g. "engine changed  -> legacy"). Compare against the PREVIOUS engine
// (theme->engine()) so the log only fires on a real engine change.
TEST(PluginSource, ReloadEngineChangeLogComparesPreviousEngine)
{
    const std::string src =
        read_file(std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/plugin.cpp");
    ASSERT_FALSE(src.empty()) << "src/plugin.cpp must be readable";

    const std::string body =
        fn_body(src, "void emeraldwf_plugin::reload_theme(");
    ASSERT_FALSE(body.empty()) <<
        "could not locate emeraldwf_plugin::reload_theme()";

    EXPECT_NE(body.find("theme->engine()"), std::string::npos) <<
        "reload_theme() must compare the new engine against the previous "
        "engine (theme->engine()), not the override";
    EXPECT_EQ(body.find("!= ov"), std::string::npos) <<
        "the engine-change comparison must not be against the override (ov) "
        "(empty when unset - the source of the spurious log)";
}

// Minor (issue #1): the last_stamp map (file stamps for the watched paths)
// was never pruned. When the theme_dir option changes, the watched set swaps
// (emerald_watched_paths() derives the paths from last_theme_dir), but the
// old paths' stamps stay in the map forever - stale entries accumulate.
// Prune last_stamp to the currently-watched paths in poll_tick().
TEST(PluginSource, LastStampPrunedToWatchedPaths)
{
    const std::string src =
        read_file(std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/plugin.cpp");
    ASSERT_FALSE(src.empty()) << "src/plugin.cpp must be readable";

    const std::string body =
        fn_body(src, "bool emeraldwf_plugin::poll_tick(");
    ASSERT_FALSE(body.empty()) <<
        "could not locate emeraldwf_plugin::poll_tick()";

    EXPECT_NE(body.find("last_stamp.erase"), std::string::npos) <<
        "poll_tick() must prune last_stamp entries for paths no longer "
        "watched (a theme_dir change swaps the watched set; the old entries "
        "were never removed)";
}

// Minor (issue #1): export_csd_shadow_margins() is called from several sites
// that iterate ALL views (init, adjust_new_decorations, the
// csd_titlebar_height / shadow_pad option callbacks). For an SSD view (one
// with a decorator) it overwrote the decorator's theme-derived shadow margins
// with the CSD margins (thickness = shadow_pad) - a spurious standalone
// shadow with shadow_pad > 0. The function must skip views that have a
// decorator.
TEST(PluginSource, ExportCsdShadowMarginsSkipsSsdViews)
{
    const std::string src =
        read_file(std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/plugin.cpp");
    ASSERT_FALSE(src.empty()) << "src/plugin.cpp must be readable";

    const std::string body =
        fn_body(src, "void emeraldwf_plugin::export_csd_shadow_margins(");
    ASSERT_FALSE(body.empty()) <<
        "could not locate emeraldwf_plugin::export_csd_shadow_margins()";

    EXPECT_NE(body.find("get_data<emerald_decorator_t>"), std::string::npos) <<
        "export_csd_shadow_margins() must skip SSD views (those with a "
        "decorator) so it does not overwrite the decorator's theme-derived "
        "shadow margins";
}
} // namespace
