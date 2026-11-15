// emerald-shadow-margins.cpp — Part 05 (D12) implementation.
//
// Kept in a wayfire-free TU (only wf::view.hpp / wf::object.hpp are needed)
// so the headless shade test can link `shadow_margins_for()` and
// `export_shadow_margins()` without wayfire core / scene symbols.
#include "emerald-shadow-margins.hpp"

namespace wf
{
namespace emeraldwf
{
wf::decoration_margins_t shadow_margins_for(int client_h, int thickness,
    int titlebar, double progress,
    int min_client_h)
{
    const double t = static_cast<double>(thickness);
    double bottom  = t;

    if ((progress > 0.0) && (client_h > 0))
    {
        const double collapse =
            static_cast<double>(client_h) - t - static_cast<double>(titlebar);
        if (collapse > 0.0)
        {
            // Finding 53: same cap as margins_for — the collapsed client
            // must never go below the min-size hint (X11 clamps, wayfire
            // re-derives the frame from the clamped client).
            double delta = collapse * progress;
            const double max_delta =
                static_cast<double>(client_h) - min_client_h;
            if (max_delta < delta)
            {
                delta = max_delta;
            }

            if (delta > 0.0)
            {
                bottom = t + delta;
            }
        }
    }

    return {
        .left   = t,
        .right  = t,
        .bottom = bottom,
        .top    = t,
    };
}

void export_shadow_margins(wayfire_view view, int client_h, int thickness,
    int titlebar, double progress, int min_client_h)
{
    if (view == nullptr)
    {
        return;
    }

    auto margins = shadow_margins_for(client_h, thickness, titlebar, progress,
        min_client_h);
    auto data = view->get_data<wf_shadow_margin_t>(SHADOW_MARGIN_DATA_KEY);
    if (data != nullptr)
    {
        data->set_margins(margins);
        return;
    }

    // R3 (issue #1): no data — OR a FOREIGN custom-data type stored
    // under the shared key. has_data() is type-agnostic, but
    // get_data<T>() is a dynamic_cast: any type other than
    // wf_shadow_margin_t (e.g. pixdecor's own, or another shadow
    // plugin's) casts to nullptr, and dereferencing it was a
    // segfault. The key is deliberately shared, so replace the
    // foreign entry (store_data overwrites) and store ours — with the
    // margins already set, so no second lookup is needed.
    auto new_data = std::make_unique<wf_shadow_margin_t>();
    new_data->set_margins(margins);
    view->store_data(std::move(new_data), SHADOW_MARGIN_DATA_KEY);
}
} // namespace emeraldwf
} // namespace wf
