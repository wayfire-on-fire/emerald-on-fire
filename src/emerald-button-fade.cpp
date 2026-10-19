// emerald-button-fade.cpp — button fade state machine (see the header for
// the semantics: faithful port of upstream emerald's counters[]/pulsating[]
// machine, src/main.c:1388-1607).
#include "emerald-button-fade.hpp"

#include <algorithm>
#include <cmath>

namespace wf
{
namespace emeraldwf
{
static int fade_num(const ButtonFadeConfig & cfg)
{
    return std::max(1, cfg.num_steps);
}

static bool pulse_on(const ButtonFadeConfig & cfg)
{
    return cfg.pulse && cfg.pulse_len_steps > 0;
}

void button_fade_on_press(ButtonFadeState & st)
{
    // main.c:1553-1559 — pressed button stops its fade; the pulsating flag
    // is cleared by the same rule that clears it on "not hovered"
    // (a pressed button is not hovered, main.c:1581-1586).
    st.counter   = 0;
    st.pulsating = false;
}

void button_fade_on_hover(const ButtonFadeConfig & cfg,
    ButtonFadeState & st, bool now_hovered)
{
    const int num = fade_num(cfg);
    if (now_hovered)
    {
        if (st.counter < 0)
        {
            st.counter = 1 - st.counter; // main.c:1569-1574: out → in
        } else if (st.counter == 0)
        {
            st.counter = 1; // main.c:1575-1580: start fade in
        }
    } else if (st.counter > 0)
    {
        st.counter = 1 -
            std::min(st.counter, num + 1); // main.c:1561-1568: in → out
    }

    if (!now_hovered)
    {
        st.pulsating = false; // main.c:1581-1586: stop pulse
    }
}

ButtonFadeTickResult button_fade_tick(const ButtonFadeConfig & cfg,
    ButtonFadeState & st)
{
    const int num = fade_num(cfg);
    ButtonFadeTickResult r;

    // main.c:1423-1425 — an idle button (counter 0, not pulsating) is
    // skipped entirely: it stays drawn in the non-hovered look.
    if (!st.pulsating && (st.counter == 0))
    {
        return r;
    }

    const int before   = st.counter;
    const bool pulsing = pulse_on(cfg);

    // main.c:1427-1438 — pulse reversals.
    if (pulsing && (st.counter != 0) && st.pulsating)
    {
        if (st.counter == -num + cfg.pulse_len_steps)
        {
            st.counter = 1 - st.counter;
        }

        if (st.counter == num + 1 + cfg.pulse_wait_steps)
        {
            st.counter = 1 - std::min(st.counter, num + 1);
        }
    }

    // main.c:1439-1441 — start pulse at full-fade-in (counter == num).
    if (pulsing && (st.counter == num))
    {
        st.pulsating = true;
    }

    // main.c:1443-1449 — advance one step, holding at the max fade (or at
    // the pulse max while pulsating).
    if ((st.counter != num + 1) ||
        (pulsing && (st.counter != num + 1 + cfg.pulse_wait_steps)))
    {
        st.counter++;
    }

    r.changed = st.counter != before;
    // main.c:1491-1499 — the timer keeps running while a counter is moving
    // or a pulse is active.
    r.animating = st.pulsating || (st.counter != 0 && st.counter < num + 1);
    return r;
}

ButtonFadeAlphas button_fade_alphas(const ButtonFadeState & st, int num_steps)
{
    const int num = std::max(1, num_steps);
    ButtonFadeAlphas a;

    const int c  = st.counter;
    double hover = 0.0;
    if (c > 0)
    {
        hover = (std::min(c, num + 1) - 1) / (double)num; // main.c:1454-1456
    } else if (c < 0)
    {
        hover = -c / (double)num; // main.c:1457-1458
    }

    a.hover = hover;
    // main.c:1460-1467 — the non-hovered layer (pow gamma) is drawn while
    // the fade is not at max.
    a.normal = (c < num + 1) ? std::pow(1.0 - hover, 0.4) : 0.0;
    return a;
}
} // namespace emeraldwf
} // namespace wf
