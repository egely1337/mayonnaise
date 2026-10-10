#pragma once

#include <memory>
#include <string>

#include "../platform/platform.hpp"
#include "event_dispatcher.hpp"
#include "mayonnaise/widget.hpp"
#include "mayonnaise/window.hpp"

namespace mayo {

class Painter;
class WindowRootView;

struct Window::Impl final : WidgetHost {
    Window* self = nullptr;
    std::unique_ptr<platform::PlatformWindow> platformWindow;
    EventDispatcher dispatcher;
    std::shared_ptr<WindowRootView> root;
    std::string title;
    bool needsFrame = true;
    bool needsLayout = true;
    bool closed = false;
    bool keyWindow = true;
    double frameTime = 0;
    double wakeAt = -1;

    // Tooltip tracking.
    std::weak_ptr<Widget> tooltipWidget;
    double tooltipSince = 0;
    Point pointer;

    explicit Impl(const platform::WindowConfig& config);
    ~Impl() override;
    void handleEvent(const platform::PlatformEvent& event);
    void layout();
    void render();
    void themeChanged();
    [[nodiscard]] double nextDeadline() const;
    [[nodiscard]] platform::HitRegion hitRegion(Point p);
    void drawTooltip(Painter& painter);
    void updateCursor();

    // WidgetHost
    void requestRedraw() override { needsFrame = true; }
    void requestLayout() override {
        needsLayout = true;
        needsFrame = true;
    }
    [[nodiscard]] double time() const override { return frameTime; }
    void scheduleFrame(double t) override;
    void setFocus(Widget* widget, bool visible) override { dispatcher.setFocus(widget, visible); }
    [[nodiscard]] Widget* focusedWidget() const override { return dispatcher.focused(); }
    [[nodiscard]] bool focusVisible() const override { return dispatcher.focusVisible(); }
    [[nodiscard]] bool isKeyWindow() const override { return keyWindow; }
    void setCursor(CursorShape shape) override;
    void showOverlay(std::shared_ptr<Widget> overlay) override;
    void dismissOverlay(Widget* overlay) override;
    [[nodiscard]] std::string clipboardText() const override;
    void setClipboardText(const std::string& text) override;
    void setTextInput(bool active, Rect caret) override;
    [[nodiscard]] Size windowSize() const override { return platformWindow->sizeInPoints(); }
    [[nodiscard]] float titleBarHeight() const override;
};

}  // namespace mayo
