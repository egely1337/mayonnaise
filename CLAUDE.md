# CLAUDE.md — Mayonnaise

## Project overview

Mayonnaise is a C++20 UI library that renders everything itself on top of OpenGL 3.3 Core and
looks and feels like modern macOS (the "Liquid Glass" design language: translucent blurred
materials, soft layered shadows, large continuous corner radii, vibrancy, short spring
animations). The MVP targets Linux (X11 + Wayland via SDL3) with all platform code isolated in
`src/platform/`. The public API is a retained-mode widget tree with a fluent builder API
(`mayo::VStack({...}).spacing(12)`) designed so a Hello World fits in ~15 lines.

## Build, run, test

```sh
# configure + build (library, examples, tests)
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j

# debug build (enables KHR_debug GL output)
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug -j

# ASan + UBSan build
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DMAYONNAISE_SANITIZE=ON && cmake --build build-asan -j

# run examples
./build/examples/hello/hello
./build/examples/gallery/gallery
./build/examples/settings_app/settings_app

# tests
ctest --test-dir build --output-on-failure

# headless-ish visual check: render N frames, save a PNG of the window, exit
MAYO_SCREENSHOT=/tmp/shot.png MAYO_SCREENSHOT_FRAMES=10 ./build/examples/gallery/gallery
```

Useful environment variables:
- `MAYO_SCREENSHOT=<file.png>` / `MAYO_SCREENSHOT_FRAMES=<n>` / `MAYO_SCREENSHOT_DELAY=<s>` — save the first window after n frames (and at least s seconds, default 0.4) and quit.
- `MAYO_DEMO=picker|menu|popover` (gallery only) — open a popup at startup for screenshots.
- `MAYO_TEST_SCENE=1|text` — renderer / typography test scenes instead of the widget tree.
- `MAYO_APPEARANCE=dark|light` — force appearance at startup.
- `MAYO_SCALE=<float>` — override the UI scale factor (HiDPI testing).
- `MAYO_NO_TRANSPARENCY=1` — opaque, square-cornered windows (X11 without compositor).
- `SDL_VIDEO_DRIVER=x11|wayland` — force SDL backend.

CMake options: `MAYONNAISE_BUILD_EXAMPLES` (ON), `MAYONNAISE_BUILD_TESTS` (ON),
`MAYONNAISE_USE_SYSTEM_DEPS` (OFF), `MAYONNAISE_SANITIZE` (OFF).

## Architecture map

```
include/mayonnaise/   public headers only (no SDL/GL/FreeType includes)
  mayonnaise.hpp      umbrella header
  geometry/color      Vec2, Size, Rect, Insets, CornerRadii, Color
  theme.hpp           Appearance, semantic colors, metrics, TextStyle type scale
  animation.hpp       Animated<T>, easing curves, springs
  painter.hpp         public 2D drawing API used by every widget (wraps Renderer2D)
  widget.hpp          Widget node base class, Element (type-erased handle), WidgetHandle CRTP modifiers
  layout.hpp          VStack/HStack/ZStack/Spacer/Divider
  widgets/*.hpp       controls and containers
  app.hpp window.hpp  App (event loop, appearance) and Window
src/platform/         SDL3 wrapper: windows, GL context, events -> PlatformEvent, clipboard, cursors, DPI
src/render/           GL RAII wrappers, Renderer2D (batched SDF quads), blur, FreeType fonts + glyph atlas, Painter impl
src/core/             widget tree, layout, event dispatcher (hover/press/capture/focus), animation clock, App/Window
src/widgets/          widget implementations
src/theme/            light/dark themes, type scale
shaders/              GLSL, embedded at build time (cmake/Embed.cmake -> generated .cpp)
assets/fonts/         InterVariable.ttf (OFL) + license, embedded at build time
third_party/          vendored glad (generated GL 3.3 core + KHR_debug) and stb single headers
examples/             hello, gallery, settings_app
tests/                doctest unit tests (geometry, color, theme, layout, event dispatch)
```

Frame flow: `App::run` blocks in `SDL_WaitEventTimeout` until an event or the next animation /
timer deadline → platform events are translated to `PlatformEvent` → `Window` routes them through
the `EventDispatcher` (hit test, hover enter/leave, pointer capture, focus, keyboard) → widgets
mutate state and call `invalidate()`/`invalidateLayout()` → for each dirty window: layout
(measure/arrange, cached) → draw into an offscreen scene FBO via `Painter`/`Renderer2D` (materials
flush the batch and blur the scene so far) → overlays (menus, tooltips) → blit to the default
framebuffer → swap. If any animation is still running, another frame is scheduled.

## Design rules

- All colors, radii, spacing and fonts come from `mayo::Theme` (`theme().colors.*`, `theme().metrics.*`,
  `TextStyle`). No hardcoded colors or sizes inside widgets.
- Every widget handles hover, pressed, focused (accent focus ring), and disabled states, in both light and dark.
- No Apple proprietary assets: no SF Pro, SF Symbols, or Apple images. Text uses Inter (OFL);
  icons are vector-drawn with Painter primitives (`src/widgets/icons.cpp`).
- Prefer fewer widgets that look excellent over many rough ones.

## API rules

- Namespace `mayo`, single umbrella header `<mayonnaise/mayonnaise.hpp>`.
- Widgets are cheap value-like handles (`std::shared_ptr` to a node) — capture them in lambdas and mutate later.
- Sensible defaults; every widget looks correct with zero configuration.
- Modifiers return `*this` for chaining (`.padding() .frame() .foreground() .background() .cornerRadius() .disabled() .tooltip()`).
- Callbacks are `std::function`; no inheritance for normal use. Custom widgets subclass `mayo::Widget` and draw with `mayo::Painter`.
- Public headers must never include SDL, GL, glad, FreeType or stb headers. Use pimpl / forward declarations.
- Users never touch raw OpenGL; `mayo::Canvas` exposes the Painter for custom drawing.

## Coding conventions

- C++20, RAII for every GL/SDL/FreeType resource, no naked `new`/`delete`, `std::span`/`std::string_view` where useful.
- `PascalCase` types, `camelCase` functions/variables, `m_` private members, `kConstant` constants.
- Format with `.clang-format`; lint with `.clang-tidy`. Must compile warning-free with `-Wall -Wextra -Wpedantic` on GCC 12+/Clang 15+.
- Small focused files; comment the *why*, not the *what*.
- After each milestone: build, run the relevant example (take a `MAYO_SCREENSHOT`), run tests, commit.

## Current status

- [x] M1 Skeleton — CMake, deps, SDL3 window + GL 3.3 core context
- [x] M2 Renderer2D — instanced SDF quads, shadows, gradients, lines/arcs, rounded clips, dual-Kawase backdrop blur
- [x] M3 Text — FreeType + InterVariable (wght/opsz), glyph atlas w/ 1/4 px subpixel positioning, type scale, wrapping
- [x] M4 Core — widget tree, measure/arrange stacks, event dispatcher (hover/capture/focus/Tab/overlays), Animated<T>
- [x] M5 Basic widgets — Label, Button (5 styles), Toggle, Checkbox, Slider, Divider, vector icons
- [x] M6 Window chrome — borderless transparent window, traffic lights, hit-test drag/resize, unified Toolbar
- [x] M7 Materials — glass Sidebar in SplitView, Menu / Picker / Popover, Form cards, Canvas, App::after timers
- [x] M8 TextField (+Search, secure), SegmentedControl, ProgressBar (+indeterminate, spinner), ScrollView (glide, momentum, rubber band, overlay bars)
- [ ] M9 Polish

Next: M9.

## Decisions

- **glad and stb are vendored** in `third_party/` (glad pre-generated for GL 3.3 core + `GL_KHR_debug`)
  instead of FetchContent: glad needs Python to generate, and both are tiny. SDL3, FreeType and doctest
  use `find_package` first and fall back to `FetchContent`; `MAYONNAISE_USE_SYSTEM_DEPS=ON` makes the
  system packages mandatory.
- **InterVariable.ttf** (one file, `wght` + `opsz` axes) instead of several static weights: smaller binary,
  and the optical-size axis gives Inter Display shapes for large titles automatically.
- **Window move/resize uses `SDL_SetWindowHitTest`** so the compositor performs the interactive move/resize
  (the only reliable way on Wayland, where clients cannot position themselves).
- **Transparent window surface** so client-drawn rounded window corners work; disabled with
  `MAYO_NO_TRANSPARENCY=1`. No client-side window shadow (would need an input-shape margin SDL can't express).
- **Absolute layout coordinates**: every widget's `frame()` is in window points; `ScrollView` re-arranges its
  content on scroll (measure results are cached, so this is cheap) instead of a transform stack.
- **Default stack alignment is leading/top** (not SwiftUI's center) because forms and lists are the common case;
  `.align(Alignment::Center)` centers both axes.
- **Premultiplied alpha everywhere** in the renderer (needed for a correct alpha channel on a transparent window).

## Gotchas

(Keep adding things learned the hard way.)
