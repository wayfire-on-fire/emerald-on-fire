// test_engine_dir.cpp — emeraldwf_engine_dir() (compat/engine-dir.c, linked
// into libemeraldengine) must resolve the engine directory from the
// library's OWN location at runtime. Build tree:
// build/libengine/libemeraldengine.so resolves to build/engines;
// installed, <libdir>/emeraldwf/libemeraldengine.so resolves to
// <libdir>/emeraldwf/engines.
// The baked compile-time prefix/libdir (the toolchain IMAGE's libdir in
// docker builds) must be a fallback only: a docker install on a host
// with a different libdir (Gentoo /usr/lib64, the image being Ubuntu's
// multiarch one) would otherwise scan an empty directory and the theme
// editor would list zero installed engines.
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

extern "C" const char *emeraldwf_engine_dir(void);

namespace fs = std::filesystem;

TEST(EngineDir, ResolvesAnExistingDirectory)
{
    const char *dir = emeraldwf_engine_dir();
    ASSERT_NE(dir, nullptr);
    std::string d{dir};
    ASSERT_FALSE(d.empty());
    std::error_code ec;
    EXPECT_TRUE(fs::is_directory(d, ec)) << "engine dir does not exist: " << d;
}

TEST(EngineDir, ContainsTheEngineSet)
{
    const char *dir = emeraldwf_engine_dir();
    ASSERT_NE(dir, nullptr);
    std::error_code ec;
    int versioned = 0;
    for (auto & e : fs::directory_iterator(dir, ec))
    {
        const std::string f = e.path().filename().string();
        if (e.is_regular_file(ec) && (f.rfind("lib", 0) == 0) &&
            (f.find(".so.") != std::string::npos))
        {
            versioned++;
        }
    }

    // the build tree's engine dir holds the 7 versioned engines
    // (lib<name>.so.<major.minor.patch>)
    EXPECT_GE(versioned, 7) << "no versioned engines in: " << dir;
}
