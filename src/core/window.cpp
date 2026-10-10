#include "window_impl.hpp"

#include <cstdlib>

#include "../render/gl.hpp"
#include "../render/renderer2d.hpp"
#include "app_impl.hpp"
#include "mayonnaise/app.hpp"
#include "mayonnaise/painter.hpp"
#include <string>

namespace mayo {

void drawRendererTestScene(render::Renderer2D& r, Size size, const Theme& theme);
void drawTextTestScene(Painter& p, Size size, const Theme& theme);

Window::Window(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) { m_impl->self = this; }
Window::~Window() = default;

void Window::setTitle(const std::string& title) {
    m_impl->title = title;
    m_impl->platformWindow->setTitle(title);
    m_impl->needsFrame = true;
}
const std::string& Window::title() const { return m_impl->title; }
Size Window::size() const { return m_impl->platformWindow->sizeInPoints(); }
void Window::close() { m_impl->closed = true; }

Window::Impl::Impl(const platform::WindowConfig& config)
    : platformWindow(std::make_unique<platform::PlatformWindow>(config)), title(config.title) {}

void Window::Impl::handleEvent(const platform::PlatformEvent& event) {
    using platform::EventType;
    switch (event.type) {
        case EventType::WindowClose: closed = true; break;
        case EventType::WindowResized:
        case EventType::WindowExposed:
        case EventType::WindowScaleChanged: needsFrame = true; break;
        default: break;
    }
}

void Window::Impl::render() {
    needsFrame = false;
    platformWindow->makeCurrent();
    int w = 0;
    int h = 0;
    platformWindow->pixelSize(w, h);
    auto& app = App::current();
    const Theme& theme = app.theme();
    auto& r = *app.impl().renderer;
    const float scale = static_cast<float>(w) / std::max(1.0f, platformWindow->sizeInPoints().width);
    r.beginFrame(w, h, scale);

    Size size = platformWindow->sizeInPoints();
    const bool rounded = platformWindow->isTransparent() && !platformWindow->isMaximized() &&
                         !platformWindow->isFullscreen();
    const float radius = rounded ? theme.metrics.windowRadius : 0.0f;
    Rect bounds{0, 0, size.width, size.height};
    r.fillRoundedRect(bounds, radius, theme.colors.windowBackground);
    r.pushClip(bounds, radius);
    Painter painter(r, *app.impl().text);
    static const std::string testScene = std::getenv("MAYO_TEST_SCENE") ? std::getenv("MAYO_TEST_SCENE") : "";
    if (testScene == "text") drawTextTestScene(painter, size, theme);
    else if (!testScene.empty()) drawRendererTestScene(r, size, theme);
    r.popClip();
    // Hairline border + inner highlight, like a macOS window edge.
    if (rounded) {
        r.strokeRoundedRect(bounds, radius, theme.colors.windowBorder, 1.0f / scale);
        r.strokeRoundedRect(bounds.inset(1.0f / scale), radius - 1.0f / scale, theme.colors.windowHighlight,
                            1.0f / scale);
    }
    r.endFrame();
    platformWindow->swapBuffers();
}

}  // namespace mayo
