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
} // namespace
