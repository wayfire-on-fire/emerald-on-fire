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
std::string read_file(const std::string &path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The body of `void emeraldwf_plugin::fini()` (bracket-matched from the
// first '{' after the signature).
std::string fini_body(const std::string &src)
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
    ASSERT_FALSE(body.empty())
        << "could not locate emeraldwf_plugin::fini() in plugin.cpp";

    const size_t loop = body.find("remove_decoration");
    ASSERT_NE(loop, std::string::npos)
        << "fini() must remove the per-view decorations";

    const size_t unload = body.find("emerald_engine_unload()");
    ASSERT_NE(unload, std::string::npos)
        << "fini() must call emerald_engine_unload() (pre-fix: the "
           "engine .so stayed mapped for the life of the wayfire "
           "process)";
    EXPECT_GT(unload, loop)
        << "the engine is unloaded AFTER the decorations are removed "
           "(the decorators use the engine while they live)";
    EXPECT_EQ(body.find("emerald_engine_unload()", unload + 1),
        std::string::npos)
        << "the engine is unloaded exactly once in fini()";
}

// The body of a `emeraldwf_plugin::<fn>(...)` member (bracket-matched from
// the first '{' after the signature), with // comments stripped (position
// assertions must not match identifiers quoted in comments — the M20 fix's
// own comment mentions adjust_new_decorations()).
std::string fn_body(const std::string &src, const std::string &sig)
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

    int depth = 0;
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
        if (body[i] == '/' && i + 1 < body.size() && body[i + 1] == '/')
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
    EXPECT_NE(toggle_body.find("get_or_create_shade("), std::string::npos)
        << "toggle_shade()'s CSD branch must use the shared "
           "get_or_create_shade() helper (M19) — pre-fix it had its own "
           "inline get-or-create + shadow-export registration";

    const std::string scroll_body =
        fn_body(src, "void emeraldwf_plugin::scroll_shade(");
    ASSERT_FALSE(scroll_body.empty()) << "could not locate scroll_shade()";
    EXPECT_NE(scroll_body.find("get_or_create_shade("), std::string::npos)
        << "scroll_shade()'s CSD branch must use the shared "
           "get_or_create_shade() helper (M19) — pre-fix the Review S1 "
           "regression lived in exactly this duplicated branch";

    EXPECT_EQ(src.find("std::make_shared<wf::emeraldwf::emerald_shade_t>"),
        std::string::npos)
        << "plugin.cpp must not create the shade transformer directly "
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
    ASSERT_NE(guard, std::string::npos)
        << "recreate() must skip the views should_decorate_view() "
           "REJECTS (M20) — pre-fix it decorated every toplevel, so CSD / "
           "ignore_views-matched views got emerald frames on an option "
           "change";

    const size_t cont = body.find("continue", guard);
    ASSERT_NE(cont, std::string::npos)
        << "the rejection branch must continue (skip the view)";

    const size_t adjust = body.find("adjust_new_decorations(");
    ASSERT_NE(adjust, std::string::npos)
        << "recreate() must (re)create the decoration on accepted views "
           "(that is what propagates the new option values)";
    EXPECT_LT(cont, adjust)
        << "the rejection branch must skip BEFORE any "
           "adjust_new_decorations() call (M20)";
}
} // namespace
