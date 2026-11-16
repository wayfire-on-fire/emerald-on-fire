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
std::string read_file(const std::string & path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The body of a `void emerald_node_t::<fn>(...)` member (bracket-matched
// from the first '{' after the signature), with // comments stripped
// (the fixes' own comments quote pre-fix call names).
std::string node_fn_body(const std::string & src, const std::string & sig)
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
    // Strip // line comments (no string in these bodies contains "//").
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

// The body of `void emerald_node_t::render_frame(...)` (bracket-matched
// from the first '{' after the signature), with // comments stripped
// (the fix's own comments quote the pre-fix call names).
std::string render_frame_body(const std::string & src)
{
    return node_fn_body(src, "void emerald_node_t::render_frame(");
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
    ASSERT_FALSE(body.empty()) <<
        "could not locate emerald_node_t::render_frame()";

    const size_t decision = body.find("needs_rebuild(");
    ASSERT_NE(decision, std::string::npos) <<
        "render_frame() must compute the rebuild decision via "
        "needs_rebuild()";

    const size_t measure =
        body.find("pango_layout_get_pixel_size(");
    ASSERT_NE(measure, std::string::npos) <<
        "the Pango title measurement must be present";
    EXPECT_GT(measure, decision) <<
        "M9: the Pango measurement must run AFTER the rebuild "
        "decision (pre-fix: every render pass, even cache hits, paid "
        "a full Pango layout pass)";

    const size_t lbuild = body.find("layout.build(");
    ASSERT_NE(lbuild, std::string::npos) <<
        "layout.build() must be present";
    EXPECT_GT(lbuild, decision) <<
        "M9: layout.build() must run only on a rebuild";

    const size_t render = body.find("emeraldwf_render_frame(");
    ASSERT_NE(render, std::string::npos) <<
        "the frame render must be present";
    EXPECT_GT(render, decision) <<
        "M9: the frame render must run only on a rebuild";

    // M9 follow-up (ChatGPT): textual order alone does not prove the
    // calls are INSIDE the render branch (a refactor could keep the
    // order while moving them outside). Bracket-match the
    // `if (need_surface)` block (M10: the branch covers both key
    // rebuilds and scale-only renders) and require all three to be
    // contained in it.
    const size_t branch = body.find("if (need_surface)");
    ASSERT_NE(branch, std::string::npos) <<
        "the render branch must exist";
    const size_t bopen = body.find('{', branch);
    ASSERT_NE(bopen, std::string::npos) <<
        "the rebuild branch must have a body";
    int depth     = 0;
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

    ASSERT_NE(bclose, std::string::npos) <<
        "the rebuild branch body must be bracket-balanced";
    const std::vector<std::pair<const char*, size_t>> calls = {
        {"pango_layout_get_pixel_size(", measure},
        {"layout.build(", lbuild},
        {"emeraldwf_render_frame(", render}};
    for (const auto & call : calls)
    {
        EXPECT_LT(call.second, bclose) <<
            std::string(call.first) <<
            " must be INSIDE the if (need_surface) branch, not merely "
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
    ASSERT_FALSE(body.empty()) <<
        "could not locate emerald_node_t::render_frame()";

    auto count = [&body] (const std::string & needle) -> size_t
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
    ASSERT_GE(creations, 2u) <<
        "expected the render-branch and defensive-recreate texture "
        "creations";
    EXPECT_GE(validations, creations) <<
        "every GL texture creation must be validated with "
        "get_wlr_texture() != nullptr (pre-M11: a GL failure produced "
        "a non-null texture_t wrapping a null wlr_texture, recorded in "
        "the cache → permanent false cache hit → null blit crash)";

    const size_t first_val = body.find("get_wlr_texture()");
    const size_t first_rec = body.find("entry.texture = ");
    ASSERT_NE(first_val, std::string::npos) <<
        "the GL validation must be present";
    ASSERT_NE(first_rec, std::string::npos) <<
        "the cache entry must record its texture";
    EXPECT_LT(first_val, first_rec) <<
        "the validation must precede the cache recording (a failed "
        "creation must NOT be recorded)";
}

// M13 (issue #1): every input handler gates on view_of() before
// dereferencing deco. handle_pointer_axis was the only one that wasn't:
// it called layout.in_titlebar(deco->ctx(), ...) with no guard — not a
// live bug (deco is never null while the node lives), but the first
// crash site if the invariant is ever violated.
TEST(NodeSource, PointerAxisHandlerGatesOnViewOf)
{
    const std::string src = read_file(
        std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/emerald-node.cpp");
    ASSERT_FALSE(src.empty()) << "src/emerald-node.cpp must be readable";

    const std::string body =
        node_fn_body(src, "void emerald_node_t::handle_pointer_axis(");
    ASSERT_FALSE(body.empty()) <<
        "could not locate emerald_node_t::handle_pointer_axis()";

    // The guard must be the EARLY-RETURN pattern (a bare view_of()
    // mention elsewhere — e.g. assigned to a variable that is never
    // checked — would not guard the dereference).
    const size_t guard = body.find("if (view_of() == nullptr)");
    ASSERT_NE(guard, std::string::npos) <<
        "handle_pointer_axis must early-return on view_of() == "
        "nullptr like every other handler (pre-M13: the only "
        "handler dereferencing deco unguarded)";

    const size_t first_deco = body.find("deco->");
    ASSERT_NE(first_deco, std::string::npos) <<
        "the handler must use the decorator (deco->ctx())";
    EXPECT_LT(guard, first_deco) <<
        "the view_of() guard must precede the first deco dereference";
}

// Minor (issue #1): emerald-node.cpp included <wayfire/scene-operations.hpp>
// TWICE (harmless — the include guard makes the second a no-op — but a
// merge/leftover smell). No translation unit in src/ may include the same
// header twice.
TEST(NodeSource, NoDuplicateIncludes)
{
    const std::string src = read_file(
        std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/emerald-node.cpp");
    ASSERT_FALSE(src.empty()) << "src/emerald-node.cpp must be readable";

    std::vector<std::string> includes;
    std::stringstream ss(src);
    std::string line;
    while (std::getline(ss, line))
    {
        const std::string t = line; // trim leading whitespace
        size_t start = t.find_first_not_of(" \t");
        if (start == std::string::npos)
        {
            continue;
        }

        const std::string trimmed = t.substr(start);
        if (trimmed.rfind("#include", 0) == 0)
        {
            includes.push_back(trimmed);
        }
    }

    for (size_t i = 0; i < includes.size(); i++)
    {
        for (size_t j = i + 1; j < includes.size(); j++)
        {
            EXPECT_NE(includes[i], includes[j]) <<
                "duplicate #include: " << includes[i];
        }
    }
}

// Minor (issue #1): handle_pointer_button() bound the left-press DecorContext
// with `auto ctx = deco->ctx();` — a full by-value copy of the (sizeable)
// DecorContext on every left press, although every consumer
// (layout.hit_button / resize_edges / in_titlebar / double_click_action)
// takes it as `const DecorContext&`. Bind by reference instead.
TEST(NodeSource, LeftPressDoesNotCopyDecorContext)
{
    const std::string src = read_file(
        std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/emerald-node.cpp");
    ASSERT_FALSE(src.empty()) << "src/emerald-node.cpp must be readable";

    const std::string body =
        node_fn_body(src, "void emerald_node_t::handle_pointer_button(");
    ASSERT_FALSE(body.empty()) <<
        "could not locate emerald_node_t::handle_pointer_button()";

    // The by-value copy must be gone...
    EXPECT_EQ(body.find("auto ctx = deco->ctx()"), std::string::npos) <<
        "the left-press DecorContext must not be copied by value";
    // ...and bound by const reference.
    EXPECT_NE(body.find("const auto & ctx = deco->ctx()"),
        std::string::npos) <<
        "the left-press DecorContext must be bound by const reference";
}

// Minor (issue #1): emerald_node_t::set_title() and the title_dirty flag were
// dead. The title is sourced fresh from view->get_title() on every render
// (render_frame), so set_title() (which set cache.title + title_dirty) was
// never called, and title_dirty was therefore always false. Remove both.
TEST(NodeSource, NoDeadSetTitleOrTitleDirty)
{
    const std::string src = read_file(
        std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/emerald-node.cpp");
    ASSERT_FALSE(src.empty()) << "src/emerald-node.cpp must be readable";

    EXPECT_EQ(src.find("emerald_node_t::set_title"), std::string::npos) <<
        "the dead set_title() definition must be removed";
    EXPECT_EQ(src.find("title_dirty"), std::string::npos) <<
        "the dead title_dirty flag must be removed";

    const std::string hpp = read_file(
        std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/emerald-node.hpp");
    ASSERT_FALSE(hpp.empty()) << "src/emerald-node.hpp must be readable";

    EXPECT_EQ(hpp.find("set_title"), std::string::npos) <<
        "the dead set_title() declaration must be removed";
    EXPECT_EQ(hpp.find("title_dirty"), std::string::npos) <<
        "the dead title_dirty member must be removed";
}

// Minor (issue #1): emerald-types.hpp declared a second
// emerald_state_from_view(wayfire_toplevel_view) overload that was never
// defined and never called — the live mapping is
// emerald_state_from_view(const wf::toplevel_state_t&, bool)
// (emerald-decorator.hpp/.cpp), used by the decorator and the node. The
// dead overload was the ONLY consumer of the `wf::wayfire_toplevel_view`
// placeholder alias, which in turn only existed to gate the
// EMERALDWF_HAVE_WAYFIRE_VIEW macro in the wayfire TUs. Remove the dead
// overload, the placeholder alias it depended on, and the now-dead macro.
TEST(NodeSource, NoDeadStateFromViewOverload)
{
    const std::string types = read_file(
        std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/emerald-types.hpp");
    ASSERT_FALSE(types.empty()) << "src/emerald-types.hpp must be readable";

    EXPECT_EQ(types.find("emerald_state_from_view(wayfire_toplevel_view"),
        std::string::npos) <<
        "the dead emerald_state_from_view(wayfire_toplevel_view) "
        "overload declaration must be removed";
    EXPECT_EQ(types.find(
        "using wayfire_toplevel_view = class wayfire_toplevel_view_t"),
        std::string::npos) <<
        "the placeholder wayfire_toplevel_view alias (whose only "
        "consumer was the dead overload) must be removed";
    EXPECT_EQ(types.find("EMERALDWF_HAVE_WAYFIRE_VIEW"), std::string::npos) <<
        "the EMERALDWF_HAVE_WAYFIRE_VIEW gate (whose only purpose was "
        "the placeholder) must be removed";

    // The macro must not linger in the wayfire TUs either.
    for (const char *f :
     {"src/emerald-decorator.hpp", "src/emerald-node.cpp",
         "src/emerald-node.hpp"})
    {
        const std::string tu = read_file(
            std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/" + f);
        ASSERT_FALSE(tu.empty()) << f << " must be readable";
        EXPECT_EQ(tu.find("EMERALDWF_HAVE_WAYFIRE_VIEW"), std::string::npos) <<
            "the dead EMERALDWF_HAVE_WAYFIRE_VIEW define must be "
            "removed from " << f;
    }
}

// Minor (issue #1): the parent→frame-local coordinate transform was
// duplicated. node.cpp hand-rolled the frame origin in node_offset_of()
// ({-margins.left, -margins.top}) and subtracted it in to_local_of(), while
// emerald-node-pure.cpp already derives the SAME origin in the unit-tested
// frame_box_from_pending() (whose header documents it as "the pure core of
// emerald_node_t::to_local()"). Route to_local_of() through the pure core
// (keeping the final subtraction in doubles so fractional pointer
// coordinates are preserved) and drop the redundant node_offset_of().
TEST(NodeSource, CoordTransformReusesPureCore)
{
    const std::string src = read_file(
        std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/emerald-node.cpp");
    ASSERT_FALSE(src.empty()) << "src/emerald-node.cpp must be readable";

    EXPECT_EQ(src.find("node_offset_of"), std::string::npos) <<
        "node_offset_of() re-derives the frame origin that the pure "
        "frame_box_from_pending() already computes; it must be removed";
    EXPECT_NE(src.find("frame_box_from_pending"), std::string::npos) <<
        "to_local_of() must reuse the pure frame_box_from_pending() "
        "origin derivation instead of hard-coding {-margins.left, "
        "-margins.top}";
}

// Minor (issue #1): the header doc for emerald_node_t::resize() claimed it
// "recompute[s] ctx sizes, layout, region", but the implementation only sets
// size_dirty = true and damages — the actual recompute is deferred to
// render_frame() (where size_dirty joins the rebuild key). The doc is stale;
// it must describe the real behavior (mark size_dirty + damage).
TEST(NodeSource, ResizeDocMatchesImplementation)
{
    const std::string hpp = read_file(
        std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/emerald-node.hpp");
    ASSERT_FALSE(hpp.empty()) << "src/emerald-node.hpp must be readable";

    EXPECT_EQ(hpp.find("recompute ctx sizes, layout, region"),
        std::string::npos) <<
        "the stale resize() doc (claims it recomputes ctx/layout/region) "
        "must be removed";
    // The resize() declaration must now name the real effect (size_dirty).
    const size_t decl = hpp.find("void resize();");
    ASSERT_NE(decl, std::string::npos) << "resize() declaration missing";
    const std::string tail = hpp.substr(decl, 80);
    EXPECT_NE(tail.find("size_dirty"), std::string::npos) <<
        "the resize() doc must state it marks size_dirty (the recompute "
        "is deferred to render_frame)";
}
} // namespace
