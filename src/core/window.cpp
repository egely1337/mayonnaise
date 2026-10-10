#include "window_impl.hpp"

#include "../render/gl.hpp"
#include "mayonnaise/app.hpp"

namespace mayo {

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
    glViewport(0, 0, w, h);
    Color bg = App::current().theme().colors.windowBackground;
    glClearColor(bg.r, bg.g, bg.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    platformWindow->swapBuffers();
}

}  // namespace mayo
