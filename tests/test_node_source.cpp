// test_node_source.cpp — source-structure test for the node TU (M9,
// issue #1).
//
// emerald-node.cpp cannot be linked headlessly (wayfire scene symbols),
// so the render_frame() ordering contract is verified textually: the
// rebuild decision (dirty flags + needs_rebuild()) must PRECEDE the
// per-frame work — the Pango title measurement, the DecorContext
// rebuild, and layout.build(). Pre-M9, all of that ran on every render
// pass (before the cache-hit check), so a page with continuous client
// damage (video) paid a full Pango layout pass + ctx mutation + layout
// rebuild at the client's framerate for a surface that was never
// re-rendered.
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

// The body of `void emerald_node_t::render_frame(...)` (bracket-matched
// from the first '{' after the signature), with // comments stripped
// (the fix's own comments quote the pre-fix call names).
std::string render_frame_body(const std::string &src)
{
    const size_t sig = src.find("void emerald_node_t::render_frame(");
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
    // Strip // line comments (no string in this body contains "//").
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

// M9 (issue #1): the rebuild decision must come FIRST; the Pango
// measurement, the DecorContext rebuild and layout.build() only run on
// a (re)build.
TEST(NodeSource, RebuildDecisionPrecedesPerFrameWork)
{
    const std::string src = read_file(
        std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/emerald-node.cpp");
    ASSERT_FALSE(src.empty()) << "src/emerald-node.cpp must be readable";

    const std::string body = render_frame_body(src);
    ASSERT_FALSE(body.empty())
        << "could not locate emerald_node_t::render_frame()";

    const size_t decision = body.find("needs_rebuild(");
    ASSERT_NE(decision, std::string::npos)
        << "render_frame() must compute the rebuild decision via "
           "needs_rebuild()";

    const size_t measure =
        body.find("pango_layout_get_pixel_size(");
    ASSERT_NE(measure, std::string::npos)
        << "the Pango title measurement must be present";
    EXPECT_GT(measure, decision)
        << "M9: the Pango measurement must run AFTER the rebuild "
           "decision (pre-fix: every render pass, even cache hits, paid "
           "a full Pango layout pass)";

    const size_t lbuild = body.find("layout.build(");
    ASSERT_NE(lbuild, std::string::npos)
        << "layout.build() must be present";
    EXPECT_GT(lbuild, decision)
        << "M9: layout.build() must run only on a rebuild";

    const size_t render = body.find("emeraldwf_render_frame(");
    ASSERT_NE(render, std::string::npos)
        << "the frame render must be present";
    EXPECT_GT(render, decision)
        << "M9: the frame render must run only on a rebuild";

    // M9 follow-up (ChatGPT): textual order alone does not prove the
    // calls are INSIDE the render branch (a refactor could keep the
    // order while moving them outside). Bracket-match the
    // `if (need_surface)` block (M10: the branch covers both key
    // rebuilds and scale-only renders) and require all three to be
    // contained in it.
    const size_t branch = body.find("if (need_surface)");
    ASSERT_NE(branch, std::string::npos)
        << "the render branch must exist";
    const size_t bopen = body.find('{', branch);
    ASSERT_NE(bopen, std::string::npos)
        << "the rebuild branch must have a body";
    int depth = 0;
    size_t bclose = std::string::npos;
    for (size_t i = bopen; i < body.size(); i++)
    {
        if (body[i] == '{')
        {
            depth++;
        } else if (body[i] == '}')
        {
            depth--;
            if (depth == 0)
            {
                bclose = i;
                break;
            }
        }
    }
    ASSERT_NE(bclose, std::string::npos)
        << "the rebuild branch body must be bracket-balanced";
    const std::vector<std::pair<const char *, size_t>> calls = {
        {"pango_layout_get_pixel_size(", measure},
        {"layout.build(", lbuild},
        {"emeraldwf_render_frame(", render}};
    for (const auto &call : calls)
    {
        EXPECT_LT(call.second, bclose)
            << std::string(call.first)
            << " must be INSIDE the if (need_surface) branch, not merely "
               "after the decision (pre-fix: every render pass ran it)";
    }
}

// M11 (issue #1): wlr_texture_from_pixels() returns NULL on a GL failure,
// and owned_texture_t wraps it UNCONDITIONALLY — the resulting texture_t is
// non-null but wraps a null wlr_texture. Recording that in the cache would
// make pick_frame_texture() report a cache hit forever (null blit → crash)
// and make the defensive-recreate path (entry.texture == nullptr)
// unreachable. Every texture creation in render_frame() must therefore be
// validated with get_wlr_texture() BEFORE the texture is recorded in the
// cache.
TEST(NodeSource, GlTextureCreationValidatedBeforeCache)
{
    const std::string src = read_file(
        std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/emerald-node.cpp");
    ASSERT_FALSE(src.empty()) << "src/emerald-node.cpp must be readable";

    const std::string body = render_frame_body(src);
    ASSERT_FALSE(body.empty())
        << "could not locate emerald_node_t::render_frame()";

    auto count = [&body] (const std::string &needle) -> size_t
    {
        size_t n = 0, pos = 0;
        while ((pos = body.find(needle, pos)) != std::string::npos)
        {
            n++;
            pos += needle.size();
        }
        return n;
    };

    const size_t creations = count("get_texture()");
    const size_t validations = count("get_wlr_texture()");
    ASSERT_GE(creations, 2u)
        << "expected the render-branch and defensive-recreate texture "
           "creations";
    EXPECT_GE(validations, creations)
        << "every GL texture creation must be validated with "
           "get_wlr_texture() != nullptr (pre-M11: a GL failure produced "
           "a non-null texture_t wrapping a null wlr_texture, recorded in "
           "the cache → permanent false cache hit → null blit crash)";

    const size_t first_val   = body.find("get_wlr_texture()");
    const size_t first_rec   = body.find("entry.texture = ");
    ASSERT_NE(first_val, std::string::npos)
        << "the GL validation must be present";
    ASSERT_NE(first_rec, std::string::npos)
        << "the cache entry must record its texture";
    EXPECT_LT(first_val, first_rec)
        << "the validation must precede the cache recording (a failed "
           "creation must NOT be recorded)";
}
} // namespace
