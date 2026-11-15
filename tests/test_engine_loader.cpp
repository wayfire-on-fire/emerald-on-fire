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
#include <string>

#include "emerald-engine.hpp"
#include "test-paths.h"

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
} // namespace
