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

# ASan + UBSan build (the suppression file hides GL-driver leaks, see Gotchas)
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DMAYONNAISE_SANITIZE=ON && cmake --build build-asan -j
LSAN_OPTIONS=suppressions=$PWD/tools/lsan.supp MAYO_SCREENSHOT=/tmp/x.png ./build-asan/examples/gallery/gallery

# run examples
./build/examples/hello/hello
./build/examples/gallery/gallery
./build/examples/settings_app/settings_app
./build/examples/photos/photos <images or folders...>
./build/examples/video_player/video_player <media files...>   # only built when FFmpeg dev libs exist
./build/examples/task_manager/task_manager                    # Linux only
./build/examples/finder/finder [folder]                         # Linux only
./build/examples/terminal/terminal [directory]                  # Linux only

# tests
ctest --test-dir build --output-on-failure

# headless-ish visual check: render N frames, save a PNG of the window, exit
MAYO_SCREENSHOT=/tmp/shot.png MAYO_SCREENSHOT_FRAMES=10 ./build/examples/gallery/gallery
```

Useful environment variables:
- `MAYO_SCREENSHOT=<file.png>` / `MAYO_SCREENSHOT_FRAMES=<n>` / `MAYO_SCREENSHOT_DELAY=<s>` — save the first window (`MAYO_SCREENSHOT_WINDOW=last`: the newest, e.g. a Settings window) after n frames (and at least s seconds, default 0.4) and quit. Screenshot runs never write settings.
- `MAYO_DEMO=picker|menu|popover|<page>` (gallery) / `MAYO_DEMO=scroll` (settings_app) /
  `MAYO_DEMO=viewer:N|zoom:N|favorite|open` (photos) / `MAYO_DEMO=seek:<s>` (video_player) — drive the
  examples at startup for screenshots. The finder takes comma-separated steps, including file operations
  (`MAYO_DEMO="list,select:a.txt,duplicate:a.txt,wait:0.3,undo"`, full list in its main.cpp); point it at a
  scratch folder, and it never launches other programs in demo mode.
- `MAYO_VERBOSE=2` — also print frames per second (find widgets that never stop requesting frames).
- `MAYO_TEST_SCENE=1|text` — renderer / typography test scenes instead of the widget tree.
- `MAYO_APPEARANCE=dark|light` — force appearance at startup.
- `MAYO_SCALE=<float>` — override the UI scale factor (HiDPI testing).
- `MAYO_NO_TRANSPARENCY=1` — opaque, square-cornered windows (X11 without compositor).
- `MAYO_VERBOSE=1` — print GL version/renderer and whether KHR_debug output is on.
- `SDL_VIDEO_DRIVER=x11|wayland` — force SDL backend.
- `MAYO_EGL_ALL_VENDORS=1` — load every glvnd EGL vendor (see the hybrid-GPU gotcha).
- `XDG_CONFIG_HOME=<dir>` — where settings live (`<dir>/mayonnaise/<app>/settings.conf`); point it at a scratch
  directory when testing so runs don't touch your real preferences.

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
- [x] M9 Polish — HiDPI verified (MAYO_SCALE=2), accent colours, gallery + settings_app, README + screenshots,
      Clang/GCC warning-free, ASan/UBSan clean, KHR_debug clean on Wayland + X11, idle CPU ~0, install() verified

Verified on: Arch-based (CachyOS) with GCC 16 / Clang 22, Mesa radeonsi, Wayland + XWayland. Not yet verified
on a stock Ubuntu 24.04 / Fedora 40 machine (README lists the packages; the FetchContent path for SDL3,
FreeType and doctest was verified here with `-DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON -DCMAKE_DISABLE_FIND_PACKAGE_Freetype=ON`).

Known issues / next steps:
- No kerning or shaping (HarfBuzz; system HarfBuzz is common, make it optional).
- Interactive behaviours (drag/resize via hit test, real touchpad momentum feel, IME) were only smoke-tested
  by hand-free runs; unit tests cover the physics and editing logic, not the feel.
- No client-side window shadow (would need an input-shape margin; SDL can't express it).
- Popups have a fade-in but no fade-out (they're removed immediately on dismiss).
- Menus open below their button; macOS pop-up buttons overlay the selected row on the button.
- Single-line TextField only; no multi-line text editor.

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
- **Root content fills the window.** The window always arranges its content at the full content rect, so a root
  stack's `.align()` positions its children in the window and a root `.background()` covers the window.
- **Two clips per primitive**: the plain intersection of every pushed rect plus the innermost rounded rect.
  Cheap (no stencil, no batch breaks) and correct for the UI case of rect clips nested in rounded panels.
- **Overlays (menus, popovers) are measured at their ideal size** (`{inf, inf}`), then clamped to the window, so
  expanding content (spacers) doesn't stretch them to full width.
- **Fonts beyond Inter** (`<mayonnaise/font.hpp>`): `Font::family` (0 = Inter) and `Font::italic`;
  `systemFontFamily("monospace")` resolves installed files through a dlopen()ed libfontconfig (no build
  dependency). Missing bold/italic faces are synthesized (outline embolden / shear). A character no
  registered family has triggers a fontconfig `charset=` lookup once and the found font joins the fallbacks,
  so CJK, symbols and Nerd Font icons render anywhere (Finder file names too). Inter's tracking applies only
  to Inter glyphs.
- **Terminal example**: `Screen` (examples/terminal/screen.cpp) is a UI-free xterm emulator tested by
  tests/test_terminal_screen.cpp; `Pty` reads on a worker thread with back-pressure (4 MB) and never blocks
  the UI on writes; `TerminalView` snaps cells to whole device pixels and draws box drawing, blocks, braille
  and powerline glyphs as geometry so they join between cells.
- **Settings** (`mayo::Settings`, `App::settings()`): one `key = value` file per app in
  `~/.config/mayonnaise/<app>/settings.conf`, written atomically 0.5 s after the last change and on exit.
  The App stores `appearance` (system/light/dark), `accent` (system or #RRGGBB) and `window.<name>.*` for
  windows created with `autosaveName`. Apps are named explicitly (`App app("finder")`); `App()` uses the
  executable name. Every example has a Settings window (Ctrl+,) built from `AppearanceSettings(app)` plus
  its own `FormSection`s.
- **Follows the desktop**: appearance via `SDL_GetSystemTheme` + `SDL_EVENT_SYSTEM_THEME_CHANGED` (portal
  `color-scheme`); the accent colour via the portal's `accent-color`, read with a dlopen()ed libdbus (no build
  dependency) at startup and whenever a window gains focus, then snapped to the macOS palette
  (`nearestAccent`).
- **Finder example** keeps all views on one shared `Browser` state (folder, entries, selection, hooks into
  `main.cpp`); `FileView` holds the Finder selection rules, rubber band, type-select, inline rename and drag
  and drop so the icon grid, list rows and browser columns only provide geometry. Column view nests vertical
  ScrollViews in a horizontal one (scroll chaining handles the axes).
- **Photos example** uses custom `Widget` subclasses (grid, viewer, page container) rather than new library
  widgets: it doubles as the reference for writing custom widgets. A custom detail container opts into
  running under the toolbar with `extendsIntoTitleBar()`.
- **Extra components beyond the brief**: `Form`/`FormSection`/`FormRow` (System Settings cards), `Picker`,
  `ToolbarGroup`, `ButtonStyle::Glass`, `App::after/post` timers, `MAYO_SCREENSHOT` capture for visual checks.

## Gotchas

- **Hybrid GPUs load every EGL vendor.** glvnd's libEGL maps the NVIDIA stack (~90 MB RSS, can wake the dGPU)
  even when Mesa drives the display. `Platform::init` restricts EGL to Mesa on Wayland when all connected
  screens hang off non-NVIDIA GPUs, and unsets the variable once the context exists so launched programs
  don't inherit it. PRIME offload (`__NV_PRIME_RENDER_OFFLOAD`) and `MAYO_EGL_ALL_VENDORS=1` opt out.
- **GPU memory on integrated GPUs is system RAM** (GTT, not in RSS: read `drm-resident-gtt` in
  /proc/<pid>/fdinfo). Instances stream through a ring buffer (`Renderer2D::flush`); orphaning the whole
  buffer on every batch made Mesa allocate and cache a buffer per batch (~20 MB more with many thumbnails).
  The glyph atlas is cleared through an FBO instead of uploading zeros.
- **ImageData drops its CPU pixels after upload** unless `setRetainPixels(true)`; `scaledToFit()` must run
  before the first draw. Dynamic images (`create()`/`update()`) keep theirs.
- **Idle heap trimming**: after 1 s of idleness following work, `App::run` calls `malloc_trim(0)` so memory
  freed by decoder threads (glibc per-thread arenas) goes back to the OS.
- **One FreeType face per (weight, optical size)**, resized on demand; a face per pixel size cost ~70 KB each.
- **Finder file operations are real.** Test them in a scratch folder; items trashed on another file system
  go to `$topdir/.Trash-$uid` (e.g. /tmp/.Trash-1000 for tmpfs), as the spec requires.
- **Screenshot runs never write settings** (read-only `Settings`), but they do read them: use `XDG_CONFIG_HOME`
  for reproducible shots. Live system-theme switching was implemented against SDL's event but not exercised
  by an automated run (it would flip the desktop's theme).
- **Ctrl+W only closes a window when nothing handled it** (`EventDispatcher::keyDown` returns whether the key
  was used): in a terminal it deletes a word. `Window::onCloseRequested` can veto user-initiated closes.
- **Alt vs AltGr**: SDL reports both as Alt. The terminal defers Alt+key to the next loop iteration and lets
  a following text-input event win when it is a different character (AltGr+Q = @ on Turkish keyboards).
- **Bracketed paste swallows newlines**: shells don't execute a pasted "\n", so demo commands are pasted and
  then a "\r" is typed.
- **Designated initializers and WindowOptions**: new fields need a default member initializer (`{}`), or GCC's
  `-Wmissing-field-initializers` fires in every example using `{.title = ...}`.
- **Windows popped up by screenshot runs take keyboard focus**, so typing elsewhere during a run lands in the
  app (type-to-select in the finder). Don't trust a surprising selection in a screenshot without a trace.

- **Task manager sampling**: one pass over /proc takes ~10 ms for ~400 processes, done on a worker thread;
  the uid comes from stat()ing /proc/<pid> (cheaper than parsing status), names from argv[0] because comm is
  truncated to 15 chars — but Chromium/Electron rewrite argv[0] into one space-separated title, so cut at
  the first space. Disk stats use whole devices that have /sys/block/<dev>/device (partitions, zram, loop and
  dm would double count). Never let a demo path call kill(): the quit demo only opens the confirmation.

- **Video player threading**: demux, video decode and audio decode threads; seeking bumps a serial number so
  every stage drops stale packets/frames without locks across stages. Audio is the clock
  (`audioEnd - queuedSeconds`), the wall clock is used for silent files. Frames are converted to RGBA on the
  decode thread and uploaded with `ImageData::update` (copy-free move); 4K is downscaled to 2560 wide.
- **Hardware decoding**: `setupHardware` picks the first device type (VAAPI, CUDA, Vulkan, VDPAU) that a decoder
  for the stream supports — the default AV1 decoder is libdav1d (software only), so it iterates decoders for
  one with a hw config. `get_format` falls back to software when the GPU refuses a stream (seen with a
  portrait VP9 file); `decoderDescription()` reports it. `extra_hw_frames` covers the display queue, and the
  frame on screen is kept alive so its surface isn't reused under the texture.
- **Zero-copy**: VAAPI → `av_hwframe_map(DRM_PRIME)` → two single-plane EGL images (R8/GR88, or R16/GR1616 for
  10-bit) bound with `glEGLImageTargetTexture2DOES`. Needs an EGL context, so SDL is forced to EGL on X11 too
  (`MAYO_GLX=1` restores GLX; then playback uses copy-back). EGL/DRM constants are defined locally so the build
  needs no EGL headers. The EGLImage is destroyed right after binding; the texture keeps the buffer.
- **YUV matrix guessing**: untagged streams use BT.709 at ≥720p, BT.601 below (like mpv). FFmpeg's own test
  encodes are BT.601 but untagged, so their colour bars look slightly off — that's the file, not the shader
  (verified against FFmpeg's bt709 conversion within ±2).
- **TSan on the video player** reports races inside FFmpeg's frame threads and PipeWire (uninstrumented
  atomics); none involve MediaPlayer's own state.
- **Widget::setOpacity is a no-op when unchanged** — fades call it every frame from draw(), and an
  unconditional invalidate() kept the window rendering forever.
- **Full screen is applied asynchronously by the compositor**; track the requested state yourself instead of
  reading `isFullscreen()` right after `setFullscreen()`.

- **File dialogs answer on SDL's terms** (portal reply during event pumping, zenity on a helper thread);
  `App::openFiles` always hops back with `post()`, which is why `post()`/`after()` are mutex-protected.
- **ImageData is CPU-only until first draw.** Decode on any thread, create textures only on the UI thread;
  viewer-sized images call `setRetainPixels(false)` so the CPU copy is freed after upload.
- **stb_image ignores EXIF orientation**; `render::decodeImage` parses the JPEG APP1 tag itself. Phone photos
  come out sideways without it.
- **ThreadSanitizer** reports dozens of races inside uninstrumented glib (SDL_Init → IBus/DBus) and Mesa's
  shader threads. Filter for our symbols (Loader, post, decode) — those paths are clean.
- **There is no "drag left the window" event**, so the drop highlight times out 0.3 s after the last
  DROP_POSITION.

- **Measure overlays with infinite space.** Proposing the window size made a popover containing a `Spacer`
  as wide as the window.
- **Nested clips must not inherit the outer radius onto the inner rect** — it rounded the corners of a text
  clip and ate the first glyph ("Lock" rendered as "' ock"). That's why there are two clip rects per primitive.
- **`theme()` works without an App** (falls back to the default light theme) so layout tests can run headless.
- **WidgetHost::time() is the wall clock outside frames.** Animations started from input handlers after an
  idle period would otherwise start "in the past" (the last frame time) and finish instantly.
- **Screenshot mode needs a time floor** (`MAYO_SCREENSHOT_DELAY`): without vsync throttling (hidden/occluded
  windows) N frames can finish before `App::after` timers fire.
- **Backdrop materials ignore the colour opacity stack by construction** (they output the blurred scene), so
  group opacity is passed separately (`clip2.y`) — otherwise fading popovers pop in at full opacity.
- **Wheel vs touchpad**: SDL3 gives no device type; fractional deltas are treated as touchpad (1:1 + momentum),
  whole notches as a wheel (animated glide). Touchpad gesture end = no events for 70 ms.
- **LeakSanitizer** reports ~311 bytes in `<unknown module>` frames: Mesa allocations in driver modules that
  `SDL_Quit` unloads, reproducible with a bare SDL3 + GL program. Run examples with
  `LSAN_OPTIONS=suppressions=$PWD/tools/lsan.supp`.
- **Canvas callbacks get local coordinates.** Capturing the canvas' own handle in its callback to read its frame
  creates a shared_ptr cycle (a real leak ASan caught in settings_app).
- **Static installs must export the glad library** (`mayonnaise_glad`); wrapping it in `$<BUILD_INTERFACE:>`
  produced undefined `glad_gl*` symbols in installed consumers.
- **SDL hit-test DRAGGABLE regions never deliver the button press to the app** (the compositor takes over), so
  title-bar double-click-to-zoom isn't available; traffic lights and toolbar controls must report
  `isInteractive()` to stay clickable.
