// test_makefile_docker.cpp — source-structure test for the Makefile's
// Docker build (M27, issue #1).
//
// The `binary` target compiles the plugin in the toolchain image and
// ships the artifacts in ./dist. The themer's rpath and libengine's
// ENGINE_DIR are baked from meson's `prefix` option at setup time.
// Pre-M27 the target ran `meson setup build-docker --buildtype=release`
// with no --prefix, so the artifacts carried /usr/local (meson's
// default) while the image's wayfire is installed into /usr (see the
// Dockerfile): the shipped themer could not find libemeraldengine.so
// or the engines under any other prefix. The Makefile must target the
// SAME prefix the Dockerfile installs wayfire into.
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

// The line containing `pos` (from its start to the next newline).
std::string line_at(const std::string &src, size_t pos)
{
    const size_t b = src.rfind('\n', pos);
    const size_t start = (b == std::string::npos) ? 0 : b + 1;
    const size_t e = src.find('\n', pos);
    const size_t end = (e == std::string::npos) ? src.size() : e;
    return src.substr(start, end - start);
}

// M27 (issue #1): the dist/ themer artifacts must be built against the
// prefix the Dockerfile installs wayfire into (/usr), not meson's
// /usr/local default - otherwise their baked rpath (/usr/local/lib/
// emeraldwf) and ENGINE_DIR point at a dir that does not exist in the
// image, and the themer cannot load libemeraldengine.so / the engines.
TEST(MakefileDocker, BinaryTargetsTheImagePrefix)
{
    const std::string makefile =
        read_file(std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/Makefile");
    ASSERT_FALSE(makefile.empty()) << "Makefile must be readable";
    const std::string dockerfile =
        read_file(std::string{EMERALDWF_TEST_SOURCE_ROOT} + "/Dockerfile");
    ASSERT_FALSE(dockerfile.empty()) << "Dockerfile must be readable";

    // (1) The Dockerfile installs wayfire into /usr.
    const size_t wf = dockerfile.find("WayfireWM/wayfire");
    ASSERT_NE(wf, std::string::npos) << "could not locate the wayfire build";
    const size_t wf_setup = dockerfile.find("meson setup", wf);
    ASSERT_NE(wf_setup, std::string::npos) << "no meson setup for wayfire";
    const std::string wf_line = line_at(dockerfile, wf_setup);
    EXPECT_NE(wf_line.find("--prefix=/usr"), std::string::npos)
        << "the Dockerfile must install wayfire into /usr (the reference "
           "prefix for the dist artifacts)";

    // (2) The Makefile's binary target must use the SAME prefix for the
    //     dist build.
    const size_t bin = makefile.find("binary: build");
    ASSERT_NE(bin, std::string::npos) << "could not locate the binary target";
    const size_t msetup = makefile.find("meson setup build-docker", bin);
    ASSERT_NE(msetup, std::string::npos)
        << "the binary target must set up a build-docker dir";
    const std::string m_line = line_at(makefile, msetup);
    EXPECT_NE(m_line.find("--prefix=/usr"), std::string::npos)
        << "the binary target must pass --prefix=/usr to meson setup "
           "(M27) — pre-fix it used meson's /usr/local default, so the "
           "dist themer's rpath/ENGINE_DIR pointed outside the image";
}
} // namespace
