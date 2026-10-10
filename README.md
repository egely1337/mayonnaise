# Mayonnaise

A C++20 UI library that renders macOS-style interfaces (the "Liquid Glass" look: translucent blurred
materials, soft shadows, big continuous radii, spring animations) entirely on its own with OpenGL 3.3.
No GTK, no Qt — just SDL3 for windows and input. Linux (X11 and Wayland) for now.

![Settings example, light](docs/screenshots/settings-light.png)

| Gallery (light) | Gallery (dark) |
|---|---|
| ![](docs/screenshots/gallery-light.png) | ![](docs/screenshots/gallery-dark.png) |
| **Menus over content** | **Content scrolling under the toolbar** |
| ![](docs/screenshots/menu.png) | ![](docs/screenshots/settings-dark.png) |

## Hello World

```cpp
#include <mayonnaise/mayonnaise.hpp>

int main() {
    mayo::App app;

    auto& window = app.createWindow({ .title = "Hello", .width = 800, .height = 520 });

    int count = 0;
    auto label = mayo::Label("Clicked 0 times").font(mayo::TextStyle::Title2);

    window.setContent(
        mayo::VStack({
            label,
            mayo::Button("Click me").style(mayo::ButtonStyle::Prominent)
                .onClick([&] { label.setText("Clicked " + std::to_string(++count) + " times"); }),
            mayo::Toggle("Dark mode").onChange([&](bool on) {
                app.setAppearance(on ? mayo::Appearance::Dark : mayo::Appearance::Light);
            }),
        }).spacing(12).padding(24).align(mayo::Alignment::Center)
    );

    return app.run();
}
```

## Features

- **Rendering**: one batched, instanced SDF renderer — anti-aliased rounded rects with per-corner radii,
  analytic Gaussian box shadows, gradients, lines/arcs, rounded clipping, and dual-Kawase backdrop blur for
  materials. Redraws only on demand (idle CPU ≈ 0) and only while animations run.
- **Text**: FreeType + embedded [Inter](https://rsms.me/inter/) variable font (weight and optical-size axes),
  ¼-pixel subpixel positioning, Inter's dynamic tracking, the macOS type scale
  (`LargeTitle … Caption`), word wrapping.
- **Window chrome**: client-side title bar with traffic lights, unified toolbar with glass button groups,
  compositor-driven dragging and resizing (works on Wayland), rounded transparent window corners.
- **Widgets**: `Label`, `Button` (Default / Prominent / Borderless / Destructive / Glass), `Toggle`,
  `Checkbox`, `Slider`, `TextField` (+ secure and search styles), `SegmentedControl`, `ProgressBar`
  (determinate / indeterminate / spinner), `Picker`, `Menu`, `Popover`, `Sidebar`, `SplitView`,
  `ScrollView` (wheel glide, touchpad momentum, rubber banding, overlay scrollbars), `Toolbar`,
  `Form` / `FormSection` / `FormRow`, `Image` (fit / fill / stretch), `Canvas`, and the `VStack` / `HStack` / `ZStack` / `Spacer` /
  `Divider` layout primitives.
- **Theming**: light/dark appearance, configurable accent colour, semantic colours
  (`label`, `secondaryLabel`, `separator`, `controlBackground`, …), all from one `mayo::Theme`.
- **States**: every control handles hover, pressed, keyboard focus (accent focus ring, Tab / Shift+Tab),
  and disabled, in both appearances.
- **HiDPI**: layout in points; follows the display scale (override with `MAYO_SCALE=2`).

## Building

Requirements: a C++20 compiler (GCC 12+ or Clang 15+), CMake ≥ 3.21, git, and the OpenGL / X11 / Wayland
development headers that SDL3 needs. SDL3, FreeType and doctest are taken from the system when present and
otherwise fetched and built automatically.

Ubuntu 24.04:

```sh
sudo apt install build-essential cmake git pkg-config \
  libgl1-mesa-dev libegl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
  libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libfreetype-dev
```

Fedora 40:

```sh
sudo dnf install gcc-c++ cmake git pkgconf-pkg-config \
  mesa-libGL-devel mesa-libEGL-devel libX11-devel libXext-devel libXrandr-devel libXcursor-devel \
  libXi-devel libXScrnSaver-devel libXtst-devel libxkbcommon-devel wayland-devel wayland-protocols-devel \
  libdecor-devel freetype-devel
```

Then:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/examples/hello/hello
./build/examples/gallery/gallery
./build/examples/settings_app/settings_app
ctest --test-dir build --output-on-failure
```

| CMake option | Default | |
|---|---|---|
| `MAYONNAISE_BUILD_EXAMPLES` | `ON` (top level) | build `examples/` |
| `MAYONNAISE_BUILD_TESTS` | `ON` (top level) | build the doctest suite |
| `MAYONNAISE_USE_SYSTEM_DEPS` | `OFF` | require system SDL3 / FreeType / doctest instead of falling back to fetching |
| `MAYONNAISE_SANITIZE` | `OFF` | AddressSanitizer + UndefinedBehaviorSanitizer |

## Using it in your project

```cmake
include(FetchContent)
FetchContent_Declare(mayonnaise GIT_REPOSITORY <url-of-this-repo> GIT_TAG main)
FetchContent_MakeAvailable(mayonnaise)        # or: add_subdirectory(mayonnaise)

target_link_libraries(app PRIVATE mayonnaise::mayonnaise)
```

When SDL3 and FreeType come from the system, `cmake --install build` also installs a package config, so
`find_package(mayonnaise)` works.

Public headers never include SDL, OpenGL, or FreeType, so your code only sees `<mayonnaise/...>`.

## API in five minutes

Widgets are cheap handles to shared nodes: copy them, capture them in lambdas, mutate them later.
Modifiers chain and return the widget:

```cpp
auto name = mayo::TextField("Name").onSubmit([](const std::string& s) { /* ... */ });
auto save = mayo::Button("Save").style(mayo::ButtonStyle::Prominent).disabled();
name.onChange([save](const std::string& s) mutable { save.setDisabled(s.empty()); });

mayo::HStack({ name, save }).spacing(8).padding(12)
    .background(mayo::Material::Glass).cornerRadius(14);
```

- Layout: stacks measure their children, then share free space among expanding children (`Spacer`,
  `TextField`, `Slider`, `.fillWidth()`, `.flex(2)`). `.frame(w, h)`, `.width()`, `.minWidth()`,
  `.maxWidth()`, `.padding()` constrain any widget. Stacks default to top-leading alignment.
- Common modifiers: `.padding()`, `.frame()`, `.foreground()`, `.background(Color | Material)`,
  `.cornerRadius()`, `.disabled()`, `.hidden()`, `.opacity()`, `.tooltip()`.
- App: `app.setAppearance()`, `app.setAccentColor()`, `app.after(seconds, fn)`, `app.post(fn)`.
- Window: `setContent()`, `setToolbar(mayo::Toolbar({...}))`, `setTitle()`.
- Custom drawing: `mayo::Canvas([](mayo::Painter& p, mayo::Rect r) { p.fillCircle(r.center(), 8, c); })`.
- Custom widgets: subclass `mayo::Widget` (override `measureContent`, `arrangeContent`, `draw`, and the
  `onPointer…` / `onKey…` handlers), then use it with `mayo::Custom<MyWidget>(args...)` to get the
  modifier API. See `include/mayonnaise/widget.hpp`.

## Environment variables

| Variable | Effect |
|---|---|
| `MAYO_APPEARANCE=dark\|light` | appearance at startup |
| `MAYO_SCALE=2` | override the UI scale (HiDPI testing) |
| `MAYO_NO_TRANSPARENCY=1` | opaque, square windows (X11 without a compositor) |
| `MAYO_SCREENSHOT=out.png` | render, save the window as PNG and exit (`MAYO_SCREENSHOT_FRAMES`, `MAYO_SCREENSHOT_DELAY`) |
| `MAYO_VERBOSE=1` | print the GL version/renderer and whether KHR_debug output is active |
| `SDL_VIDEO_DRIVER=x11\|wayland` | choose the SDL backend |

## Status and limitations

This is an MVP. Known gaps: no kerning or complex shaping (HarfBuzz is a natural next step), no
accessibility APIs, IME composition preview is minimal, single-line text fields only, no client-side window
shadow (the compositor doesn't draw one for borderless windows), and only Linux is implemented — platform
code is isolated in `src/platform/` for future macOS/Windows backends.

## Licensing notes

Mayonnaise evokes the macOS design language but ships no Apple assets: text uses Inter (SIL Open Font
License, see `assets/fonts/Inter-LICENSE.txt`) and all icons are drawn from vector primitives in
`src/widgets/icons.cpp`. Vendored: glad (generated, MIT/Apache-2.0 headers), stb_image /
stb_image_write (public domain).
