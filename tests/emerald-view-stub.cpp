// emerald-view-stub.cpp — test-only stubs for wayfire view methods + part-04
// node hooks + part-05 shade hooks.
//
// The margin/state tests compile emerald-decorator.cpp, which calls
// view->toplevel(), view->should_be_decorated(), view.activated, and
// view.sticky. Those live in libwayfire (not linked into a headless test),
// so provide trivial stubs here.
//
// Part 04 added attach_node()/detach_node()/on_*() to emerald_decorator_t
// (implemented in emerald-decorator-node.cpp, which the headless test does
// NOT compile, since it would pull in wayfire scene symbols).
//
// Part 05 added on_shade_progress()/toggle_shade()/shade() (implemented in
// emerald-decorator-shade.cpp, which also pulls in scene symbols). Provide
// stubs here so emerald-decorator.cpp links. The margins test never
// constructs a live decorator, so these are never executed.
#include <memory>
#include <string>

#include <wayfire/object.hpp>
#include <wayfire/signal-provider.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view.hpp>

#include "emerald-decorator.hpp"
#include "emerald-shade.hpp"

namespace wf
{

// Test stub: the headless test never connects signals, so the (libwayfire)
// disconnect() that connection_base_t's destructor calls is a no-op here.
// (Finding 28 added a connection_t member to emerald_decorator_t, which
// needs this symbol at link time.)
void signal::connection_base_t::disconnect()
{
}

const std::shared_ptr<toplevel_t>&
toplevel_view_interface_t::toplevel() const
{
    static std::shared_ptr<toplevel_t> dummy;
    return dummy;
}

bool toplevel_view_interface_t::should_be_decorated()
{
    return false;
}

// Stub the wayfire object-base data methods (libwayfire not linked into
// the headless test). `emerald-shadow-margins.cpp` calls has_data /
// get_data / store_data on the view; these stubs are never executed by the
// pure-computation tests (which only call shadow_margins_for).
bool object_base_t::has_data(std::string)
{
    return false;
}

void object_base_t::erase_data(std::string)
{}

custom_data_t *object_base_t::_fetch_data(std::string)
{
    return nullptr;
}

void object_base_t::_store_data(std::unique_ptr<custom_data_t>, std::string)
{}

namespace emeraldwf
{

void emerald_decorator_t::attach_node()
{}

void emerald_decorator_t::detach_node()
{}

void emerald_decorator_t::on_geometry_changed()
{}

void emerald_decorator_t::on_activated_changed()
{}

void emerald_decorator_t::on_theme_changed()
{}

void emerald_decorator_t::on_shade_progress(double progress)
{
    (void)progress;
}

void emerald_decorator_t::toggle_shade()
{}

std::shared_ptr<emerald_shade_t> emerald_decorator_t::shade() const
{
    return nullptr;
}

} // namespace emeraldwf
} // namespace wf
