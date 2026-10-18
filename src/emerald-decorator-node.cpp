// emerald-decorator-node.cpp — Part 04: node attach/detach/updates for
// emerald_decorator_t.
//
// Kept in a separate TU from emerald-decorator.cpp so the headless margins
// test (which compiles the latter) does not link against the scene node.
#include "emerald-decorator.hpp"

#include <memory>

#include <wayfire/scene-operations.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/util/log.hpp>

#include "emerald-node.hpp"

namespace wf
{
namespace emeraldwf
{

void emerald_decorator_t::attach_node()
{
    if (view == nullptr)
        return;
    if (is_csd())
    {
        static bool logged_csd = false;
        if (!logged_csd)
        {
            LOGI("emeraldwf: CSD window detected — skipping emerald frame "
                 "(use always_decorate to force one)");
            logged_csd = true;
        }
        return; // CSD: no emerald frame (D6)
    }

    if (node == nullptr)
    {
        // A non-owning shared_ptr: the decorator outlives the node (the
        // toplevel owns the decorator; the node is destroyed on detach).
        node = std::make_shared<emerald_node_t>(
            std::shared_ptr<emerald_decorator_t>(this, [](emerald_decorator_t *) {}));
        // Offset {-thickness, -titlebar_top} = {-margins.left, -margins.top}
        // (emerald's shadow lives INSIDE the extents, so the node's bounding
        // box sits at that offset — spec 04 §1, contrast pixdecor). The
        // offset is derived from get_margins() in node_bounding_box()/
        // get_bounding_box(), so no per-node offset member is needed.
        // Add behind the view's surface root so the client renders on top.
        // Keep a shared_ptr to it (finding 16): detach_node() must verify
        // node->parent() is still THIS live node before remove_child(), and
        // the shared_ptr keeps the parent alive for that check.
        this->sroot = view->get_surface_root_node();
        wf::scene::add_back(this->sroot, node);
        node->resize();
        // Finding 28: watch the view's activation state so the titlebar's
        // active/inactive colors update on focus changes (pixdecor parity).
        on_view_activated = [this] (wf::view_activated_state_signal *)
        {
            on_activated_changed();
        };
        view->connect(&on_view_activated);
        static bool logged_attach = false;
        if (!logged_attach)
        {
            auto m = get_margins(view->toplevel()->pending());
            LOGI("emeraldwf: attached scene node to '%s' (margins "
                 "%d,%d,%d,%d, theme engine '%s')",
                 view->get_title().c_str(), m.left, m.right, m.bottom, m.top,
                 theme_ref() ? theme_ref()->engine().c_str() : "?");
            logged_attach = true;
        }
    }
}

void emerald_decorator_t::detach_node()
{
    on_view_activated.disconnect();
    if (node != nullptr)
    {
        // part 06 finding 16: only remove_child() when the node's parent is
        // the surface root we attached to AND that surface root is still
        // alive. In the view-destruction order where the view's scene nodes
        // die before its data map, node->parent() is an already-destroyed
        // surface root; remove_child() on a dead parent is a use-after-free
        // (and the node is out of the graph anyway, so there is nothing to
        // remove — the scene already dropped it). The `sroot` shared_ptr
        // keeps the parent alive so parent() is valid to compare.
        if (detach_should_remove_child(node->parent(), sroot.get()))
        {
            wf::scene::remove_child(node);
        }
        node.reset();
    }
    sroot.reset();
}

void emerald_decorator_t::on_geometry_changed()
{
    if (node == nullptr)
        return;
    // Margins may change with the new geometry (tiled/fullscreen). The node
    // derives its bounding-box offset from get_margins() on demand, so just
    // mark the size dirty and re-render.
    node->resize();
}

void emerald_decorator_t::on_activated_changed()
{
    if (node == nullptr)
        return;
    ctx().active = view ? view->activated : false;
    node->set_active(ctx().active);
}

void emerald_decorator_t::on_theme_changed()
{
    if (node == nullptr)
        return;
    node->set_theme_dirty();
}

} // namespace emeraldwf
} // namespace wf
