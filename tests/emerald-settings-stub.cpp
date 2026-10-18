// emerald-settings-stub.cpp — Part 01 test-only ThemeSettings definitions.
//
// emerald-settings.cpp (part 02) owns the real ThemeSettings. It is in
// flight and not yet buildable, so the decorator test target needs
// *some* definitions of the members it references (valid(), ws(),
// fs_active(), fs_inactive(), engine()). This file provides them against
// the frozen §5.3 interface. Part 02's real emerald-settings.cpp replaces
// this in the plugin build.
#include "emerald-settings.hpp"

namespace wf
{
namespace emeraldwf
{

ThemeSettings::ThemeSettings()
{
    // window_settings is a plain C struct; `new T{}` value-initializes it
    // (every member zeroed, floats to 0.0) — the portable C++ form of the
    // old `new T()` + `memset(ws_, 0, sizeof(*ws_))` (cppcheck: memsetClassFloat).
    ws_ = new window_settings{};
}

ThemeSettings::~ThemeSettings()
{
    delete ws_;
}

bool ThemeSettings::load(const std::string &settings_ini, const std::string &theme_dir,
                         const std::string &engine_override)
{
    (void)engine_override;
    settings_path_ = settings_ini;
    theme_dir_     = theme_dir;

    // Part 02: real settings.ini + theme.ini parsing. For the Part 01
    // skeleton use emerald's default geometry.
    ws_->win_extents     = {6, 6, 4, 6};
    ws_->extra_extents   = {0, 0, 0, 0};
    ws_->titlebar_height = 17;

    valid_ = true;
    return true;
}

window_settings *ThemeSettings::ws() const
{
    return ws_;
}

frame_settings *ThemeSettings::fs_active() const
{
    return fs_act_;
}

frame_settings *ThemeSettings::fs_inactive() const
{
    return fs_inact_;
}

const std::string &ThemeSettings::engine() const
{
    return engine_;
}

bool ThemeSettings::valid() const
{
    return valid_;
}

} // namespace emeraldwf
} // namespace wf
