// emerald-button-fade.hpp — button fade state machine (pure).
//
// Faithful port of upstream emerald's button fade (src/main.c:1388-1607,
// `draw_buttons_timer_func` + `draw_buttons_with_fade`). Upstream's X11
// timer is run here on the scene node (one-shot wl_timer per step,
// rescheduled while any button animates).
//
// Semantics: the button itself NEVER fades out — it is always drawn from
// the theme's button surfaces. What fades is a CROSSFADE between the
// button's non-hovered surface and its hovered (highlight) surface:
//
//   counter c < 0          → hover alpha = -c / num           (fading out
//                            back toward the non-hovered look)
//   counter c == 0         → hover alpha = 0 (idle: only the non-hovered
//                            surface, at full opacity)
//   counter c > 0          → hover alpha = (min(c, num+1)-1)/num
//                            (fading in toward the hovered look)
//   counter c == num+1     → hover alpha = 1.0 (steady "at max fade")
//
// The non-hovered layer is drawn at pow(1 - hover_alpha, 0.4) (upstream
// gamma), so both layers are always present while mid-fade.
//
// Events (applied by the node on pointer/press state changes):
//   press                    → counter = 0 (fade stops; pressed look)
//   hover starts             → counter 0 → 1; counter < 0 → 1 - counter
//   hover ends               → counter > 0 → 1 - min(counter, num+1)
//
// Pulse (`use_button_fade_pulse`): while the button is HOVERED, once the
// counter reaches num it starts pulsating: the counter keeps moving past
// the max (holding at num+1+wait for `wait` steps), then reverses to
// -num and climbs back, reversing at -num+pulse_len — the highlight
// breathes between full and the "pulse min opacity" floor. The pulse
// stops when the button is no longer hovered.
#pragma once

namespace wf
{
namespace emeraldwf
{

struct ButtonFadeConfig
{
    bool enabled = false;   // use_button_fade && use_pixmap_buttons
    int num_steps = 1;      // ≥ 1 (clamped by the callers)
    bool pulse = false;     // button_fade_pulse_len_steps > 0
    int pulse_len_steps = 0;
    int pulse_wait_steps = 0;
    int step_ms = 50;       // one fade step (button_fade_step_duration)
};

struct ButtonFadeState
{
    int counter = 0;         // upstream fade_info->counters
    bool pulsating = false;  // upstream fade_info->pulsating
};

// Per-button draw alphas (upstream main.c:1452-1467).
struct ButtonFadeAlphas
{
    double normal = 1.0; // non-hovered surface
    double hover  = 0.0; // hovered surface
};

// Upstream event handling (main.c:1547-1587). now_hovered == false applies
// the "moved out" rule; both also stop a running pulse when not hovered.
void button_fade_on_press(ButtonFadeState &st);
void button_fade_on_hover(const ButtonFadeConfig &cfg,
                          ButtonFadeState &st, bool now_hovered);

// One timer tick (upstream draw_buttons_timer_func, main.c:1420-1468).
// animating: the timer must keep running (a counter is moving, or a
// pulse is active — a pulse never settles while the button is hovered).
struct ButtonFadeTickResult
{
    bool changed = false;
    bool animating = false;
};
ButtonFadeTickResult button_fade_tick(const ButtonFadeConfig &cfg,
                                      ButtonFadeState &st);

ButtonFadeAlphas button_fade_alphas(const ButtonFadeState &st, int num_steps);

} // namespace emeraldwf
} // namespace wf
