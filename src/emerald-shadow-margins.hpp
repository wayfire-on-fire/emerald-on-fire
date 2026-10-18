// emerald-shadow-margins.hpp — Part 05 (D12): export the
// `wf-decoration-shadow-margin` custom data so a standalone wayfire shadow
// plugin can shadow CSD windows and shaded windows.
//
// The custom-data type is the same shape as pixdecor's
// `wf_shadow_margin_t` (pixdecor/src/deco-subsurface.hpp:12-27). The pure
// computation `shadow_margins_for()` is factored out so it can be tested
// headlessly (spec 05 Tests); `export_shadow_margins()` is a free function
// callable from the plugin, including for CSD views that have no
// `emerald_decorator_t` (spec 05 §4).
#pragma once

#include <wayfire/object.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/view.hpp>

namespace wf
{
namespace emeraldwf
{

// Custom data key — identical to pixdecor so an installed shadow plugin
// (wayfire-contrib `shadow` etc.) that reads this key keeps working.
constexpr char SHADOW_MARGIN_DATA_KEY[] = "wf-decoration-shadow-margin";

/**
 * Shadow margins, same shape as pixdecor's `wf_shadow_margin_t`.
 * A standalone shadow plugin reads this from the view's custom data.
 */
class wf_shadow_margin_t : public wf::custom_data_t
{
  public:
    wf::decoration_margins_t get_margins() const
    {
        return margins;
    }

    void set_margins(wf::decoration_margins_t m)
    {
        this->margins = m;
    }

  private:
    wf::decoration_margins_t margins = {0, 0, 0, 0};
};

/**
 * Pure shadow-margin computation (spec 05 Tests):
 *   left   = thickness
 *   right  = thickness
 *   top    = thickness
 *   bottom = thickness + (client_h − thickness − titlebar) × progress
 *
 * `progress` is the shade progression (0 = unshaded, 1 = fully shaded).
 * `min_client_h` (finding 53) caps the collapse so the shadow bottom never
 * implies a client below the toplevel's min-size hint (same cap as
 * margins_for — X11 servers clamp to the hint and wayfire re-derives the
 * frame from the clamped client).
 * For CSD views without a decorator, call with `thickness = 0` (the
 * plugin exposes the option `emeraldwf/shadow_pad` for non-zero values).
 */
wf::decoration_margins_t shadow_margins_for(int client_h, int thickness,
                                            int titlebar, double progress,
                                            int min_client_h = 0);

/**
 * Store/update the `wf_shadow_margin_t` on the view under
 * `SHADOW_MARGIN_DATA_KEY`. Free function so the plugin can call it for
 * CSD views (which have no `emerald_decorator_t`).
 *
 * @param view       live toplevel view (any view type; CSD ok)
 * @param client_h   pending client height
 * @param thickness  emerald shadow/border thickness (0 for CSD)
 * @param titlebar   titlebar height (csd_titlebar_height for CSD)
 * @param progress   shade progression (0 if no shade animation in flight)
 * @param min_client_h toplevel min-size hint (client height; finding 53)
 */
void export_shadow_margins(wayfire_view view, int client_h, int thickness,
                           int titlebar, double progress,
                           int min_client_h = 0);

} // namespace emeraldwf
} // namespace wf
