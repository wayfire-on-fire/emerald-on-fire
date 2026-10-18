# emeraldwf — Emerald window decorator for wayfire

**READ THIS FIRST.** Every part spec (`01-*.md` … `06-*.md`) assumes the
contracts and decisions pinned here. If you are implementing one part, read
this file fully, then only the file for your part.

## 1. What we are building

A wayfire decorator plugin named **emeraldwf** that reproduces what emerald
does on Compiz: emerald's themes, emerald's theme engines (truglass, vrunner,
oxygen, zootreeves, pixmap, legacy, line), emerald's ini-based configuration
(`~/.emerald/settings.ini`, `~/.emerald/theme/`), and emerald's button layout
model — delivered through wayfire's scene-graph decorator API instead of the
X11/Compiz decoration protocol.

Sources of truth (all local, read-only):

- Emerald:  `/projects/contrib/emerald`      (C, autotools; X11/compiz)
- pixdecor: `/projects/contrib/pixdecor`     (C++/wayfire decorator to copy the skeleton from)
- wayfire:  `/projects/contrib/wayfire`      (reference compositor tree; `>= 0.11` scene API)

## 2. Global decisions (pinned — do not renegotiate in a part)

| # | Decision |
|---|----------|
| D1 | Project root: `/projects/contrib/emerald-wf`. Language: C++17 for plugin code, C (unchanged) for emerald engines. Build: meson. Tests: gtest via `meson test`. |
| D2 | Emerald sources are **vendored, unmodified** into `third_party/emerald/{include,engines,libengine,defaults}`. A `compat/` shim provides the one missing header (`decoration.h` → defines `decor_extents_t` only) so engines compile without GNOME's libdecoration. Engines are compiled as `.so` files and loaded by name exactly like emerald does (dlopen). |
| D3 | Rendering pipeline: one `cairo_image_surface` (ARGB32) per toplevel per state, produced by a **pure function** `emeraldwf_render_frame(ThemeSettings, DecorContext) -> cairo_surface_t*` (Part 2). The scene node (Part 4) uploads it as `owned_texture_t` and composites. This is the only surface backend difference vs emerald (emerald drew into X pixmaps; cairo is backend-agnostic, so engines are untouched). |
| D4 | Window state: emerald's `WnckWindowState`/`WnckWindowActions` bit values are **preserved verbatim** (libwnck values: MAXIMIZED_H=1, MAXIMIZED_V=2, SHADED=4, ABOVE=8, STICKY=16; actions CLOSE=1, MAXIMIZE=2, MINIMIZE=4, SHADE=8, ABOVE=16, STICK=32, FULLSCREEN=64). Wayfire state is mapped into these values (trivial static_cast, documented in `emerald-types.hpp`). Engines keep reading `WnckWindowState` via original headers — no engine changes. |
| D5 | Per-toplevel custom data: `emerald_decorator_t` (Part 1) holds theme reference, `DecorContext`, layout, margins. Scene node: `emerald_node_t` (Part 4) is a `wf::scene::node_t` + `pointer_interaction_t` + `touch_interaction_t`, attached via `wf::scene::add_back(view->get_surface_root_node(), node)` with offset `(-thickness, -titlebar_top)`. |
| D6 | CSD (self-decorating) windows, e.g. Firefox on Wayland: wayfire core already answers `view->should_be_decorated()` == false for them (xdg-decorations protocol). emeraldwf **skips them**: no node, margins `{0,0,0,0}`. `always_decorate` view-matcher forces decoration (double chrome, by design). X11/xwayland windows are handled by wayfire core the same way (MWM-based). |
| D7 | Frame size formula (ported from emerald `src/main.c:3336-3342`):<br>`frame_w = left_space + max(client_w,1) + right_space`<br>`frame_h = top_space + bottom_space + titlebar_height + (stretch_sides ? normal_top_corner_space + bottom_corner_space + 2 : max(client_h,1))` |
| D8 | Margins contract (Part 1, pure function):<br>`thickness = win_extents.X + extra_extents.X` (per side)<br>normal:  `{thickness, thickness, thickness, thickness + titlebar_height}`<br>fullscreen/CSD: `{0,0,0,0}`<br>maximized (tiled) with `maximized_borders=true` (default, emerald look): same as normal<br>maximized with `maximized_borders=false`: `{0, 0, 0, titlebar_visible ? thickness+titlebar_height : thickness}`<br>Shade (Part 5): bottom margin shrinks with shade progress (formula in part 05). Emerald's shadow lives *inside* the extents (unlike pixdecor's separate shadow margin), so no shadow margin option is needed for the frame itself. |
| D9 | Wayfire options (prefix `emeraldwf/`) — everything else stays in the emerald ini files:<br>`engine` (string, default `truglass`) · `theme_dir` (string, default `~/.emerald/theme`) · `maximized_borders` (bool, **true**) · `maximized_shadows` (bool, true) · `ignore_views` (view matcher, `none`) · `always_decorate` (view matcher, `none`) · `enable_shade` (bool, true) · `shade_modifier` (key, `<super>+<ctrl>`) · `csd_titlebar_height` (int, 32) · `shade_duration` (animation, `1000ms sigmoid`) |
| D10 | Config paths: `~/.emerald/settings.ini` (core: buttons, titlebars, decorations sections) and `~/.emerald/theme/` (`theme.ini` + PNGs + per-engine sections). Identical to emerald ⇒ `emerald-theme-manager` keeps working unmodified as the theme editor. Live reload via `g_file_monitor` on both files (Part 6). |
| D11 | Actions mapping (Part 3 defines enum, Part 4 executes): CLOSE→`view->close()`; MAXIMIZE/RESTORE→`default_wm->tile_request(view, TILED_EDGES_ALL / 0)`; MINIMIZE→`default_wm->minimize_request(view, true)`; SHADE/UNSHADE→Part 5 transformer; SUPERMAXIMIZE→toggle `toplevel->pending().fullscreen` via transaction; STICK/UNSTICK→same API as wayfire `wm-actions` plugin; ABOVE/UNABOVE, HELP, MENU→no-op (log once). Titlebar double-click/middle-click use `settings.ini` `double_click_action` / `middle_click_action` (0 shade, 1 maximize, 2 minimize, 3 send-to-back→no-op, 4 close, 5 none). |
| D12 | Custom data key `wf-decoration-shadow-margin` (same `wf_shadow_margin_t` shape as pixdecor) is exported by the decorator so a standalone wayfire shadow plugin can shadow CSD windows and shaded windows. |
| D13 | What emerald has that does **not** exist on wayfire — out of scope everywhere: window-switcher decoration, titlebar blur, Compiz window-menu/force-quit atoms, `_NET_FRAME_WINDOW`/`_DECOR_*` protocol. |

## 3. Project layout (final state after all parts)

```
emerald-wf/
├── meson.build                  # 01 scaffold, 06 final
├── src/
│   ├── emerald-types.hpp        # 02 (shared: state/action enums, DecorContext)
│   ├── emerald-decorator.hpp/.cpp  # 01 (custom data, margins, lifecycle)
│   ├── emerald-settings.hpp/.cpp   # 02 (ThemeSettings: ini/theme loaders)
│   ├── emerald-engine.hpp/.cpp     # 02 (engine dlopen + render_frame bridge)
│   ├── emerald-layout.hpp/.cpp     # 03 (pure layout + hit testing)
│   ├── emerald-node.hpp/.cpp       # 04 (scene node, input, actions)
│   ├── emerald-shade.hpp/.cpp      # 05 (shade view transformer)
│   └── plugin.cpp                  # 01, extended by 04/05
├── compat/decoration.h         # 02 (shim)
├── third_party/emerald/        # 02 (vendored, unmodified: include, engines,
│   └── {include,engines,libengine,defaults}     #  libengine, defaults})
├── engines/  libengine/        # 02 (build rules only; sources in third_party)
├── tests/
│   ├── test_margins.cpp        # 01
│   ├── test_settings.cpp       # 02
│   ├── test_render.cpp         # 02 (golden PNGs in tests/golden/)
│   ├── test_layout.cpp         # 03
│   ├── test_render_node.cpp    # 04
│   └── test_shade.cpp          # 05
├── metadata/emeraldwf.xml      # 06 (wcm)
├── e2e/                        # 06 (run.sh + golden screenshots + checklist)
└── README.md                   # 06
```

## 4. Part map

| Part | File | Deliverable (one sentence) | Depends on | Tests |
|------|------|---------------------------|-----------|-------|
| 01 | `01-skeleton.md` | Plugin skeleton: options, signal wiring, per-toplevel decorator data, full margins function; CSD gate; zero visual output yet. | overview | `test_margins` (headless) |
| 02 | `02-theme-bridge.md` | Vendored emerald builds; `ThemeSettings` loads emerald ini/themes; pure `emeraldwf_render_frame()` renders any emerald theme to a cairo surface. | overview | `test_settings`, `test_render` (headless, goldens) |
| 03 | `03-layout-input.md` | Pure `EmeraldLayout`: port of emerald's titlebar layout parser + button/resize hit testing. | overview (types from 02) | `test_layout` (headless) |
| 04 | `04-scene-node.md` | Scene node that renders Part 2's surface and delivers Part 3's input to wayfire WM actions — first complete visible emerald frame on wayfire. | 01, 02, 03 | `test_render_node` + manual integration |
| 05 | `05-shade-shadows.md` | Emerald's scroll-to-shade as a wayfire view transformer; `wf-decoration-shadow-margin` export; CSD edge cases. | 01, 04 | `test_shade` + manual |
| 06 | `06-integration-e2e.md` | Final packaging, wcm metadata, live settings reload, E2E suite, docs. | all | `e2e/run.sh` + full `meson test` |

Execution order for an orchestrator: **01 → (02 ∥ 03) → 04 → 05 → 06**.
Parts 02 and 03 are independent of each other (both only need `emerald-types.hpp`
from the overview contract, which 02 produces — 03 may stub-include it).

## 5. Shared contracts

### 5.1 `emerald-types.hpp` (owned by Part 02; frozen interface for all)

```cpp
namespace wf::emeraldwf {

// Bit values identical to libwnck (see D4) so the bridge can static_cast
// into original emerald WnckWindowState/WnckWindowActions fields.
enum EmeraldWindowState : uint32_t {
    EWS_MAXIMIZED_HORIZONTALLY = 1 << 0,
    EWS_MAXIMIZED_VERTICALLY   = 1 << 1,
    EWS_SHADED                 = 1 << 2,
    EWS_ABOVE                  = 1 << 3,
    EWS_STICKY                 = 1 << 4,
    EWS_FULLSCREEN             = 1 << 5,
};
enum EmeraldWindowActions : uint32_t {
    EWA_CLOSE      = 1 << 0,
    EWA_MAXIMIZE   = 1 << 1,
    EWA_MINIMIZE   = 1 << 2,
    EWA_SHADE      = 1 << 3,
    EWA_ABOVE      = 1 << 4,
    EWA_STICK      = 1 << 5,
    EWA_FULLSCREEN = 1 << 6,
};

// Maps a wayfire toplevel view to emerald state (Part 01 implements).
// contract: fullscreen => EWS_FULLSCREEN, tiled_edges all => EWS_MAXIMIZED_*,
// stick/above/sticky from view flags.
EmeraldWindowState emerald_state_from_view(wayfire_toplevel_view);

// Mirrors emerald's `struct _decor` (include/emerald.h:298-338) minus the
// X11/GTK-owned fields (event_windows, button_windows, surfaces, layout,
// name, icon*, force_quit_dialog, draw, prop_xid, button_fade_info).
// Field names/types are verbatim from emerald.h so ported code compiles.
struct DecorContext {
    int client_width = 0, client_height = 0;
    int width = 0, height = 0;           // full frame size (D7)
    bool decorated = true;
    bool active = false;
    uint32_t state = 0;                   // EmeraldWindowState
    uint32_t actions = EWA_CLOSE | EWA_MAXIMIZE | EWA_MINIMIZE;
    frame_settings *fs = nullptr;         // active or inactive (set by bridge)
    int button_states[9] = {0};
    int tobj_pos[3] = {0, 0, 0};
    int tobj_size[3] = {0, 0, 0};
    int tobj_item_pos[11] = {0};
    int tobj_item_state[11] = {3};        // 3 = not present
    int tobj_item_width[11] = {0};
    button_region_t button_region[9] = {};
    button_region_t button_region_inact[9] = {};
    rectangle_t min_drawn_buttons_region = {0, 0, 0, 0};
    bool draw_only_buttons_region = false;
    int button_last_drawn_state[9] = {0};
    bool only_change_active = false;
};

// Frame-size math (D7).
inline int frame_width(const window_settings &ws, int client_w);
inline int frame_height(const window_settings &ws, int client_w, int client_h);
}
```

(`window_settings`, `frame_settings`, `button_region_t`, `rectangle_t`,
`decor_extents_t`, `B_T_COUNT`, `TBT_COUNT` etc. come from the original
`emerald.h` — Part 02 arranges include paths so all parts see them.)

### 5.2 `emerald-decorator.hpp` (owned by Part 01)

```cpp
namespace wf::emeraldwf {
class emerald_node_t;            // fwd (Part 04)
class ThemeSettings;             // fwd (Part 02)

class emerald_decorator_t : public wf::custom_data_t {
  public:
    emerald_decorator_t(wayfire_toplevel_view view,
                        std::shared_ptr<const ThemeSettings> theme);
    ~emerald_decorator_t();

    // Full implementation in Part 01 (pure, testable).
    wf::decoration_margins_t get_margins(const wf::toplevel_state_t &state) const;
    int get_titlebar_height() const;
    bool is_csd() const;

    // Implemented by Part 04 (node attach/detach, redraw, geometry change).
    void attach_node();
    void detach_node();
    void on_geometry_changed();
    void on_activated_changed();
    void on_theme_changed();

    // Implemented by Part 05.
    void on_shade_progress(double progress);
    void toggle_shade();

    DecorContext &ctx();
  private:
    wayfire_toplevel_view view;
    std::shared_ptr<const ThemeSettings> theme;
    DecorContext ctx;
    std::shared_ptr<emerald_node_t> node;   // Part 04
};
}
```

Part 01 delivers everything except the 04/05 methods (stubs that assert,
clearly marked `// contract: implemented by part 04/05`).

### 5.3 `emerald-settings.hpp` (Part 02)

```cpp
namespace wf::emeraldwf {
class ThemeSettings {
  public:
    // Loads settings.ini (core sections) + theme_ini (engine sections +
    // button PNGs) into ORIGINAL emerald structs (window_settings,
    // frame_settings act/inact). Port of the loaders in emerald main.c
    // (load_*_setting calls + per-engine load_engine_settings).
    ThemeSettings();
    ~ThemeSettings();
    bool load(const std::string &settings_ini, const std::string &theme_dir);

    window_settings *ws() const;
    frame_settings *fs_active() const;
    frame_settings *fs_inactive() const;
    const std::string &engine() const;
    bool valid() const;
  private:
    window_settings *ws_ = nullptr;
    frame_settings *fs_act_ = nullptr, *fs_inact_ = nullptr;
    std::string engine_ = "legacy";
};
}
```

### 5.4 `emerald-engine.hpp` (Part 02)

```cpp
namespace wf::emeraldwf {
// dlopen bridge (port of src/engine_loader.c): searches
// ~/.emerald/engines/lib<name>.so then <install>/lib/emeraldwf/engines/.
bool emerald_engine_load(const std::string &name, const std::string &system_dir);
void emerald_engine_unload();

// Renders the COMPLETE frame (shadow background + engine frame + title
// text w/ halo + icon + button glows/backgrounds) into an ARGB32
// cairo_image_surface of size frame_width x frame_height (D7).
// Caller destroys the returned surface.
// Port of draw_window_decoration_real() + draw_shadow_background()
// (emerald main.c:1768-2050) with X pixmap -> image surface.
cairo_surface_t *emeraldwf_render_frame(const ThemeSettings &ts,
                                        const DecorContext &ctx);
}
```

### 5.5 `emerald-layout.hpp` (Part 03)

```cpp
namespace wf::emeraldwf {
// Pure port of emerald's layout_title_objects()/get_title_object_width()/
// update_button_regions() (main.c:1653, 2954-3060) + 3x3 cursor/resize
// band math (main.c:309-560). No wayfire dependency.
class EmeraldLayout {
  public:
    // Fills ctx.tobj_*, ctx.button_region* (both active & inactive sets).
    void build(const ThemeSettings &ts, DecorContext &ctx) const;

    // Frame-coord hit testing (x,y relative to frame top-left).
    int hit_button(int x, int y, bool active) const;      // btype or -1
    uint32_t resize_edges(int x, int y) const;            // WLR_EDGE_* | 0
    bool in_titlebar(int x, int y) const;
    int cursor_region(int x, int y) const;                // 0..8 (3x3 grid)

    int double_click_action() const;   // from settings.ini
    int middle_click_action() const;
  private:
    // ... cached parsed state
};
}
```

### 5.6 Scene node (Part 04) and shade (Part 05)

See the respective part files for full headers. The node is constructed
by `emerald_decorator_t::attach_node()` and owns one `render_instance_t`
plus pointer/touch interaction; it calls Part 02's `emeraldwf_render_frame`
and Part 03's `EmeraldLayout`.

## 6. Testing strategy

- Parts 01-05 tests are **headless** (no wayfire instance needed): the
  margin math, settings parsing, layout, and frame rendering are pure
  functions over plain structs. Render tests compare against committed
  golden PNGs (cairo is deterministic; generate goldens with
  `tests/make_goldens.cpp`, review them, commit).
- Part 04 additionally ships a manual integration checklist (real wayfire).
- Part 06 ships the scripted E2E (wayfire under lavapipe/headless, test
  windows via `weston-terminal`/xwayland, screenshots via `grim`).
- `meson test` must pass at every part's completion for that part's tests.

## 7. Definition of done (all parts combined)

1. `emeraldwf` loads under wayfire; SSD windows (e.g. xwayland apps, GTK
   apps with SSD) are decorated with emerald themes pixel-consistent with
   emerald-on-compiz for the same theme/settings.
2. CSD windows (Firefox) render without an emerald frame; `always_decorate`
   can force one.
3. All emerald buttons present in the theme work per D11; titlebar
   double-click/middle-click per settings.ini; scroll-to-shade works.
4. `emerald-theme-manager` edits `~/.emerald/*` live (no restart).
5. `meson test` green; E2E green; README documents install + config.
