// test_engine_loader.cpp — headless tests for the engine dlopen bridge
// (M1–M3, issue #1).
//
// The bridge's state (engine_handle / e_draw / active_engine_name) is
// process-global, so the tests exercise it in order and each test
// starts from a clean emerald_engine_unload(). HOME is pointed at an
// empty temp dir so the loader's first search path
// (~/.emerald/engines) is empty and only the fake-engine system_dir is
// searched.
#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "emerald-engine.hpp"
#include "test-paths.h"

// The draw-callback bridge (extern "C" in emerald-engine.cpp): routes to
// the loaded engine's engine_draw_frame, no-op when none is loaded.
extern "C" void call_engine_draw_frame(void *decor, void *cr);

namespace
{
using
namespace wf::emeraldwf;

const std::string &fake_engine_dir()
{
    static const std::string dir{EMERALDWF_TEST_FAKE_ENGINE_DIR};
    return dir;
}

class EngineLoaderTest : public testing::Test
{
  protected:
    void SetUp() override
    {
        tmp_home_ = std::filesystem::temp_directory_path() /
            ("emeraldwf-loader-test-" + std::to_string(::getpid()));
        std::filesystem::create_directories(tmp_home_);
        setenv("HOME", tmp_home_.c_str(), 1);
        emerald_engine_unload();
    }

    void TearDown() override
    {
        emerald_engine_unload();
    }

    std::filesystem::path tmp_home_;
};

// M1 (issue #1): a failed reload must not discard the working engine.
// Pre-fix, emerald_engine_load() finis'd + dlclose'd the old engine
// BEFORE dlopen'ing the replacement; when both the primary engine and
// the legacy fallback failed (ThemeSettings::load's "keeping previous
// theme" branch), the kept theme's engine was already dead —
// engine_handle == nullptr, engine frame silently lost until a
// successful reload. Post-fix the candidate is dlopen'd and checked
// first; a failed dlopen leaves the current engine untouched.
TEST_F(EngineLoaderTest, FailedReloadKeepsWorkingEngine)
{
    ASSERT_TRUE(emerald_engine_load("fakeok", fake_engine_dir()))
        << "the fake-ok engine must load from the system_dir";
    EXPECT_EQ(emerald_engine_active_name(), "fakeok");
    EXPECT_TRUE(emerald_engine_is_loaded());

    // A load that must fail: no libno-such-engine.so anywhere.
    EXPECT_FALSE(emerald_engine_load("no-such-engine", fake_engine_dir()));

    // The working engine must survive the failed swap.
    EXPECT_EQ(emerald_engine_active_name(), "fakeok")
        << "a failed load must not replace the active engine name";
    EXPECT_TRUE(emerald_engine_is_loaded())
        << "a failed dlopen must leave the previous engine loaded "
           "(pre-fix: engine_handle was nulled after the old engine "
           "was retired)";

    // The same-engine fast path still works after the failed attempt.
    EXPECT_TRUE(emerald_engine_load("fakeok", fake_engine_dir()));
    EXPECT_TRUE(emerald_engine_is_loaded());
}

// M2 (issue #1): a .so that dlopen's fine but is missing the REQUIRED
// engine_draw_frame symbol must fail cleanly — and must not leave a
// half-published bridge. Pre-fix, engine_handle + active_engine_name
// were published before the dlsym validation, so:
//   * the next load of the same name hit the same-engine fast path and
//     returned TRUE with e_draw still null (the engine "loaded" forever,
//     drew nothing), and
//   * the candidate handle was leaked (never dlclose'd).
// Post-fix the symbols are resolved into locals and validated BEFORE
// anything is published; the candidate is dlclose'd on failure and the
// previous engine is untouched.
TEST_F(EngineLoaderTest, FailedSymbolResolutionKeepsWorkingEngine)
{
    ASSERT_TRUE(emerald_engine_load("fakeok", fake_engine_dir()));
    EXPECT_TRUE(emerald_engine_is_loaded());

    // libfakenodraw.so exists and dlopen's, but has no
    // engine_draw_frame.
    EXPECT_FALSE(emerald_engine_load("fakenodraw", fake_engine_dir()));

    // No phantom engine: the active name is unchanged and the working
    // engine is still drawable.
    EXPECT_EQ(emerald_engine_active_name(), "fakeok")
        << "a failed symbol resolution must not publish the candidate "
           "name (pre-fix: active_engine_name was set before dlsym)";
    EXPECT_TRUE(emerald_engine_is_loaded())
        << "the previous engine must survive a candidate that lacks "
           "engine_draw_frame";

    // And a RETRY must not fast-path into a false success (pre-fix:
    // same name + live handle -> return true with e_draw == null).
    EXPECT_FALSE(emerald_engine_load("fakenodraw", fake_engine_dir()));
    EXPECT_EQ(emerald_engine_active_name(), "fakeok");
    EXPECT_TRUE(emerald_engine_is_loaded());

    // And the survivor must be OPERATIONAL, not just marked loaded:
    // the draw callback routes to the live engine (fakeok logs its
    // draws; pre-fix, e_draw was null after the phantom publish, so
    // this call was a silent no-op).
    const std::string log_path = (tmp_home_ / "nodraw-draw.log").string();
    setenv("EMERALDWF_FAKE_ENGINE_LOG", log_path.c_str(), 1);
    call_engine_draw_frame(nullptr, nullptr);
    std::string log;
    {
        std::ifstream in(log_path);
        std::string line;
        while (std::getline(in, line))
        {
            log += line + "\n";
        }
    }
    EXPECT_NE(log.find("fakeok.draw"), std::string::npos)
        << "the surviving engine must still DRAW after a rejected "
           "candidate (log: " << log << ")";
}

// M1/M2 (issue #1): a SUCCESSFUL swap between two distinct engines is
// transactional in the other direction — the old engine is finalized
// (exactly once, at the retire point) and only then is the new engine
// inited. The fake engines log init/fini to a file.
TEST_F(EngineLoaderTest, SuccessfulSwapFinalizesOldBeforeNewInit)
{
    const std::string log_path =
        (tmp_home_ / "engine-calls.log").string();
    setenv("EMERALDWF_FAKE_ENGINE_LOG", log_path.c_str(), 1);

    // The bridge's e_fini is gated on e_ws (the ws passed to
    // init_engine), so init with a real ws as production does
    // (ThemeSettings::load passes its own ws_).
    window_settings *ws = new window_settings{};

    ASSERT_TRUE(emerald_engine_load("fakeok", fake_engine_dir()));
    emerald_engine_init_engine(ws);

    ASSERT_TRUE(emerald_engine_load("fakealt", fake_engine_dir()))
        << "a full swap to a distinct engine must succeed";
    emerald_engine_init_engine(ws);

    std::string log;
    {
        std::ifstream in(log_path);
        std::string line;
        while (std::getline(in, line))
        {
            log += line + "\n";
        }
    }

    const size_t ok_init = log.find("fakeok.init");
    const size_t ok_fini = log.find("fakeok.fini");
    const size_t alt_init = log.find("fakealt.init");
    ASSERT_NE(ok_init, std::string::npos) << "fakeok must be inited";
    ASSERT_NE(ok_fini, std::string::npos)
        << "the old engine must be finalized on swap (exactly once)";
    ASSERT_NE(alt_init, std::string::npos)
        << "the new engine must be inited after the swap";
    EXPECT_LT(ok_init, ok_fini)
        << "old engine: init before fini";
    EXPECT_LT(ok_fini, alt_init)
        << "the old engine must be retired BEFORE the new engine is "
           "inited (transactional swap)";
    EXPECT_EQ(log.find("fakeok.fini\n", ok_fini + 1), std::string::npos)
        << "the old engine is finalized exactly once";

    delete ws;
}
} // namespace
