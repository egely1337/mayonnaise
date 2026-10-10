#pragma once

// Internal platform abstraction. This is the only layer allowed to include SDL; everything above
// it talks in PlatformEvent / PlatformWindow so another backend can be dropped in later.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "mayonnaise/events.hpp"
#include "mayonnaise/geometry.hpp"

namespace mayo::platform {

using WindowId = std::uint32_t;

enum class HitRegion : std::uint8_t {
    Normal, Draggable,
    ResizeTop, ResizeBottom, ResizeLeft, ResizeRight,
    ResizeTopLeft, ResizeTopRight, ResizeBottomLeft, ResizeBottomRight,
};

struct WindowConfig {
    std::string title;
    int width = 800;   // points
    int height = 600;  // points
    int minWidth = 200;
    int minHeight = 120;
    bool resizable = true;
    bool transparent = true;
};

enum class EventType : std::uint8_t {
    None, Quit,
    WindowClose, WindowResized, WindowScaleChanged, WindowExposed,
    WindowFocusGained, WindowFocusLost, WindowMaximized, WindowRestored, WindowMinimized,
    MouseMove, MouseDown, MouseUp, MouseLeave, Scroll,
    KeyDown, KeyUp, TextInput,
    Wake,
};

struct PlatformEvent {
    EventType type = EventType::None;
    WindowId window = 0;
    Point position;            // window coordinates (points)
    Vec2 delta;                // scroll delta (points)
    bool precise = false;
    MouseButton button = MouseButton::None;
    int clicks = 0;
    Key key = Key::Unknown;
    Modifiers modifiers = Modifiers::None;
    bool repeat = false;
    std::string text;
};

class PlatformWindow {
public:
    explicit PlatformWindow(const WindowConfig& config);
    ~PlatformWindow();
    PlatformWindow(const PlatformWindow&) = delete;
    PlatformWindow& operator=(const PlatformWindow&) = delete;

    [[nodiscard]] WindowId id() const;
    [[nodiscard]] Size sizeInPoints() const;
    /// Framebuffer size in physical pixels.
    void pixelSize(int& w, int& h) const;
    /// Physical pixels per point.
    [[nodiscard]] float scale() const;
    [[nodiscard]] bool isMaximized() const;
    [[nodiscard]] bool isFullscreen() const;
    [[nodiscard]] bool isTransparent() const { return m_transparent; }

    void setTitle(const std::string& title);
    void show();
    void minimize();
    void toggleMaximize();
    void setMinimumSize(int w, int h);

    void makeCurrent();
    void swapBuffers();

    void setHitTest(std::function<HitRegion(Point)> callback);
    void setTextInputActive(bool active, Rect caretRect = {});

    /// Converts window coordinates reported by SDL into points.
    [[nodiscard]] float pointsPerWindowUnit() const;

    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
    bool m_transparent = false;
};

/// Process-wide platform services. Must be initialized before creating windows.
class Platform {
public:
    static bool init();
    static void shutdown();

    /// Blocks up to `timeoutSeconds` (negative = forever) for the next event.
    static bool waitEvent(PlatformEvent& out, double timeoutSeconds);
    static bool pollEvent(PlatformEvent& out);
    /// Wakes up a blocking waitEvent from any thread.
    static void wake();

    static double now();  // seconds, monotonic

    static void setCursor(CursorShape shape);
    static std::string clipboardText();
    static void setClipboardText(const std::string& text);
    static float doubleClickTime();  // seconds

    static PlatformWindow* windowFromId(WindowId id);
    /// Valid once the first window (and with it the shared GL context) exists.
    static void* glProcAddress(const char* name);
};

}  // namespace mayo::platform
