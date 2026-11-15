// test_shade_source.cpp — source-structure test for the shade TU (M17,
// issue #1).
//
// emerald-shade.cpp cannot be linked headlessly (wayfire scene + output
// symbols), so the output re-derivation contract is verified textually:
// the tick hook must not be bound to the construction-time output only.
// init_animation() and tick() must re-derive the output from the view
// (via sync_output()) so a view that moves to another output mid-animation
// keeps its animation driven (and the transformer removable when it
// settles). Pre-M17 the hook stayed on the stale output: the animation
// froze, the transformer was never removed, and tick_installed_ stayed true
// so init_animation() could never re-install → leaked transformer + frozen
// shadow margins.
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

// The body of a `emerald_shade_t::<fn>(...)` member (bracket-matched from
// the first '{' after the signature), with // comments stripped (the fix's
// own comments quote pre-fix identifiers).
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
    // Strip // line comments (no string in these bodies contains "//").
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
} // namespace

// M17 (issue #1): the tick hook must be re-bound to the view's CURRENT
// output, not the construction-time one. init_animation() and tick() must
// both re-derive it (via sync_output()), and sync_output() must re-derive
// from the view and migrate the hook (rem from the old output, add to the
// new one).
TEST(ShadeSource, InitAnimationAndTickResyncOutput)
{
    const std::string src = read_file(
        std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/emerald-shade.cpp");
    ASSERT_FALSE(src.empty()) << "src/emerald-shade.cpp must be readable";

    const std::string init_body =
        fn_body(src, "void emerald_shade_t::init_animation(");
    ASSERT_FALSE(init_body.empty()) << "could not locate init_animation()";
    EXPECT_NE(init_body.find("sync_output()"), std::string::npos)
        << "init_animation() must re-derive the output from the view (M17) — "
           "pre-fix it used the construction-time output, so a view that "
           "moved (or mapped while unattached) had no driver";

    const std::string tick_body = fn_body(src, "void emerald_shade_t::tick()");
    ASSERT_FALSE(tick_body.empty()) << "could not locate tick()";
    EXPECT_NE(tick_body.find("sync_output()"), std::string::npos)
        << "tick() must re-derive the output (the view may have moved "
           "mid-animation) — pre-fix the hook stayed on the stale output";

    const std::string sync_body =
        fn_body(src, "wf::output_t *emerald_shade_t::sync_output()");
    ASSERT_FALSE(sync_body.empty())
        << "sync_output() must exist (M17) — pre-fix the output was set only "
           "in the constructor";
    EXPECT_NE(sync_body.find("get_output()"), std::string::npos)
        << "sync_output() must re-derive the output from the view";
    EXPECT_NE(sync_body.find("rem_effect"), std::string::npos)
        << "sync_output() must migrate the hook (remove it from the old "
           "output)";
    EXPECT_NE(sync_body.find("add_effect"), std::string::npos)
        << "sync_output() must migrate the hook (add it to the new output)";

    // M17 follow-up (ChatGPT): the view's set-output signal must trigger
    // sync_output() IMMEDIATELY when the view moves — not only on the next
    // tick()/init_animation(). If the old output stops rendering (no
    // damage) after the move, tick() never runs again, so the migration
    // must happen on the output change itself.
    const std::string ctor_body =
        fn_body(src, "emerald_shade_t::emerald_shade_t(");
    ASSERT_FALSE(ctor_body.empty()) << "could not locate the constructor";
    EXPECT_NE(ctor_body.find("view_set_output_signal"), std::string::npos)
        << "the constructor must subscribe to the view's set-output signal "
           "(M17 follow-up) so the hook migrates when the view moves";
    EXPECT_NE(ctor_body.find("sync_output()"), std::string::npos)
        << "the set-output handler must call sync_output()";
}

// M18 (issue #1): the strip-height setters and the constructor must clamp
// their input (std::clamp). strip_h is fed by the WCM-editable
// csd_titlebar_height option for CSD views (runtime-editable, no validation
// upstream): a NEGATIVE value makes the shaded CSD window render nothing
// (all draw_band calls have dh <= 0 → the window vanishes but stays in the
// scene, hit-testing finds no node); a huge value maps the whole client
// into the "strip" band. The constructor's member-init bypasses the setter,
// so it must clamp strip_h_arg too.
TEST(ShadeSource, StripHeightSettersAndCtorClamp)
{
    const std::string src = read_file(
        std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/emerald-shade.cpp");
    ASSERT_FALSE(src.empty()) << "src/emerald-shade.cpp must be readable";

    // ChatGPT follow-up: pin the actual BOUNDS (0 and 1 << 20), not merely
    // the presence of std::clamp.
    const std::string strip_body =
        fn_body(src, "void emerald_shade_t::set_strip_h(");
    ASSERT_FALSE(strip_body.empty()) << "could not locate set_strip_h()";
    EXPECT_NE(strip_body.find("std::clamp"), std::string::npos)
        << "set_strip_h() must clamp its input (M18) — pre-fix it stored "
           "the raw value (negative → shaded CSD window renders nothing; "
           "huge → whole client in the strip band)";
    EXPECT_NE(strip_body.find("1 << 20"), std::string::npos)
        << "the clamp upper bound must be 1 << 20 (M18)";

    const std::string bottom_body =
        fn_body(src, "void emerald_shade_t::set_bottom_strip_h(");
    ASSERT_FALSE(bottom_body.empty()) << "could not locate set_bottom_strip_h()";
    EXPECT_NE(bottom_body.find("std::clamp"), std::string::npos)
        << "set_bottom_strip_h() must clamp its input (M18)";
    EXPECT_NE(bottom_body.find("1 << 20"), std::string::npos)
        << "the clamp upper bound must be 1 << 20 (M18)";

    // The member-init list sits BETWEEN the signature and the body's '{'
    // (fn_body would miss it) — check that region directly.
    const size_t at = src.find("emerald_shade_t::emerald_shade_t(");
    ASSERT_NE(at, std::string::npos) << "could not locate the constructor";
    const size_t open = src.find('{', at);
    ASSERT_NE(open, std::string::npos);
    const std::string init = src.substr(at, open - at);
    EXPECT_NE(init.find("std::clamp"), std::string::npos)
        << "the constructor must clamp strip_h_arg (M18) — the member-init "
           "bypasses set_strip_h()";
    EXPECT_NE(init.find("1 << 20"), std::string::npos)
        << "the ctor clamp upper bound must be 1 << 20 (M18)";
}

// M19 (issue #1): the shade-transformer get-or-create sequence was
// triplicated (plugin.cpp toggle_shade/scroll_shade +
// emerald-decorator-shade.cpp toggle_shade) and the codebase already
// regressed on that duplication once (Review S1: a creation branch missing
// the shadow-export registration). All creation must go through the single
// shared helper get_or_create_shade() in emerald-shade.hpp, which
// registers the shadow-export callback on creation.
TEST(ShadeSource, ShadeTransformerCreationConsolidatedInHelper)
{
    const std::string hpp = read_file(
        std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/src/emerald-shade.hpp");
    ASSERT_FALSE(hpp.empty()) << "src/emerald-shade.hpp must be readable";

    // The helper itself: get-or-create + shadow-export registration.
    const std::string helper =
        fn_body(hpp, "get_or_create_shade(");
    ASSERT_FALSE(helper.empty())
        << "get_or_create_shade() must exist in emerald-shade.hpp (M19) — "
           "pre-fix the get-or-create was inlined at all three call sites";
    EXPECT_NE(helper.find("get_transformer"), std::string::npos)
        << "the helper must look up the existing transformer";
    EXPECT_NE(helper.find("make_shared<emerald_shade_t>"), std::string::npos)
        << "the helper must create the transformer when absent";
    EXPECT_NE(helper.find("add_transformer"), std::string::npos)
        << "the helper must register the transformer on the view";
    EXPECT_NE(helper.find("set_shadow_update"), std::string::npos)
        << "the helper must register the shadow-export callback on "
           "creation (the S1 regression was a branch missing it)";

    // The decorator-shade call site (SSD path; no shadow callback — the
    // decorator drives the shadow export itself).
    const std::string ds = read_file(
        std::string{EMERALDWF_TEST_SOURCE_ROOT}
        + "/src/emerald-decorator-shade.cpp");
    ASSERT_FALSE(ds.empty())
        << "src/emerald-decorator-shade.cpp must be readable";
    EXPECT_NE(ds.find("get_or_create_shade("), std::string::npos)
        << "emerald_decorator_t::toggle_shade() must use the shared "
           "get_or_create_shade() helper (M19)";
    EXPECT_EQ(ds.find("std::make_shared<emerald_shade_t>"), std::string::npos)
        << "emerald-decorator-shade.cpp must not create the transformer "
           "directly (M19) — creation is consolidated in the helper";
}
