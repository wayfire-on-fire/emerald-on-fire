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
#include <functional>
#include <map>
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
{}

const std::shared_ptr<toplevel_t>& toplevel_view_interface_t::toplevel() const
{
    static std::shared_ptr<toplevel_t> dummy;
    return dummy;
}

bool toplevel_view_interface_t::should_be_decorated()
{
    return false;
}

// The wayfire object-base data methods (libwayfire not linked into the
// headless test). R3 (issue #1) needs REAL storage: export_shadow_margins()
// is tested against a live view, including the "foreign type stored under
// the shared key" path, so the store must keep the data and
// get_data<T>()'s dynamic_cast must see the actual stored type.
// (The pure-computation tests never touch these.)
class object_base_t::obase_impl
{
  public:
    std::map<std::string, std::unique_ptr<custom_data_t>> data;
};

object_base_t::object_base_t() :
    obase_priv(std::make_unique<obase_impl>())
{}

object_base_t::~object_base_t()
{}

bool object_base_t::has_data(std::string name)
{
    return obase_priv != nullptr &&
        obase_priv->data.count(name) > 0;
}

void object_base_t::erase_data(std::string name)
{
    if (obase_priv)
    {
        obase_priv->data.erase(name);
    }
}

custom_data_t*object_base_t::_fetch_data(std::string name)
{
    if (obase_priv)
    {
        const auto it = obase_priv->data.find(name);
        if (it != obase_priv->data.end())
        {
            return it->second.get();
        }
    }

    return nullptr;
}

void object_base_t::_store_data(std::unique_ptr<custom_data_t> data,
    std::string name)
{
    // wayfire's _store_data overwrites an existing entry (std::map::
    // operator[]); mirror that.
    if (obase_priv)
    {
        obase_priv->data[name] = std::move(data);
    }
}

custom_data_t*object_base_t::_fetch_erase(std::string name)
{
    if (!obase_priv)
    {
        return nullptr;
    }

    const auto it = obase_priv->data.find(name);
    if (it == obase_priv->data.end())
    {
        return nullptr;
    }

    auto out = std::move(it->second);
    obase_priv->data.erase(it);
    return out.release();
}

void object_base_t::_clear_data()
{
    if (obase_priv)
    {
        obase_priv->data.clear();
    }
}

void object_base_t::_warn_wrong_type(std::string)
{}

std::string object_base_t::to_string() const
{
    return "test-object";
}

uint32_t object_base_t::get_id() const
{
    return 0;
}

// signal::provider_t (libwayfire not linked): the headless test never
// connects or emits signals; only the ctor/dtor are needed for a live
// view to construct.
class signal::provider_t::impl
{};

signal::provider_t::provider_t() :
    priv(std::make_unique<impl>())
{}

signal::provider_t::~provider_t()
{}

void signal::provider_t::disconnect(connection_base_t*)
{}

void signal::provider_t::connect_base(std::type_index,
    connection_base_t*)
{}

void signal::provider_t::for_each_connection(std::type_index,
    std::function<void(connection_base_t*)>)
{}

void signal::provider_t::disconnect_other_side(connection_base_t*)
{}

// view_interface_t (libwayfire not linked): vtable + ctor/dtor so a test
// view can be constructed headlessly. Every virtual is a no-op (the tests
// exercise the object-base data methods, not view behaviour).
class view_interface_t::view_priv_impl
{};

view_interface_t::view_interface_t() :
    priv(std::make_unique<view_priv_impl>())
{}

view_interface_t::~view_interface_t()
{}

void view_interface_t::set_role(view_role_t)
{}

void view_interface_t::set_output(wf::output_t*)
{}

wf::output_t*view_interface_t::get_output()
{
    return nullptr;
}

void view_interface_t::close()
{}

void view_interface_t::ping()
{}

wf::geometry_t view_interface_t::get_bounding_box()
{
    return {};
}

bool view_interface_t::is_focusable() const
{
    return false;
}

void view_interface_t::damage()
{}

void view_interface_t::take_snapshot(wf::auxilliary_buffer_t&)
{}

wlr_surface *view_interface_t::get_wlr_surface()
{
    return nullptr;
}

void view_interface_t::emit_view_map()
{}

void view_interface_t::emit_view_unmap()
{}

void view_interface_t::emit_view_pre_unmap()
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
