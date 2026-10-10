#pragma once

#include <functional>
#include <memory>
#include <string>

#include "mayonnaise/widget.hpp"
#include "popup.hpp"

namespace mayo {

class TitleBarView;

/// Root of every window: background, content, and the client-side title bar with traffic lights
/// and optional unified toolbar.
class WindowRootView final : public Widget {
public:
    WindowRootView();

    void setContent(std::shared_ptr<Widget> content);
    void setToolbar(std::shared_ptr<Widget> toolbar);
    void setTitle(const std::string& title);
    void setWindowState(bool maximized, bool key, bool transparent);
    [[nodiscard]] float windowRadius() const;
    [[nodiscard]] float titleBarHeight() const;
    [[nodiscard]] bool isKey() const { return m_key; }

    Size measureContent(Size available) override;
    void arrangeContent(const Rect& content) override;
    void draw(Painter& painter) override;
    /// Hairline border and highlight, drawn after overlays so it sits on top of everything.
    void paintWindowFrame(Painter& painter);

    std::function<void()> onClose;
    std::function<void()> onMinimize;
    std::function<void()> onZoom;

private:
    std::shared_ptr<Widget> m_content;
    std::shared_ptr<TitleBarView> m_titleBar;
    bool m_maximized = false;
    bool m_key = true;
    bool m_transparent = true;
};

}  // namespace mayo
