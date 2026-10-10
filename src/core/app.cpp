#include <cstdio>
#include <cstdlib>
#include <string>

#include "../render/gl.hpp"
#include "app_impl.hpp"
#include "window_impl.hpp"

namespace mayo {

namespace {
App* g_app = nullptr;
}

App::Impl::Impl() {
    if (const char* a = std::getenv("MAYO_APPEARANCE")) {
        appearance = std::string(a) == "dark" ? Appearance::Dark : Appearance::Light;
    }
    if (const char* s = std::getenv("MAYO_SCREENSHOT")) screenshotPath = s;
    if (const char* f = std::getenv("MAYO_SCREENSHOT_FRAMES")) screenshotFrames = std::atoi(f);
    if (!screenshotPath.empty() && screenshotFrames <= 0) screenshotFrames = 5;
    rebuildTheme();
}

App::Impl::~Impl() = default;

void App::Impl::rebuildTheme() {
    theme = Theme::make(appearance, accent);
    for (auto& w : windows) w->impl().needsFrame = true;
}

void App::Impl::dispatch(const platform::PlatformEvent& event) {
    using platform::EventType;
    if (event.type == EventType::Quit) {
        quitRequested = true;
        return;
    }
    for (auto& w : windows) {
        if (w->impl().platformWindow->id() == event.window) {
            w->impl().handleEvent(event);
            return;
        }
    }
}

double App::Impl::nextDeadline() const {
    double best = -1;
    for (const auto& w : windows) {
        double d = w->impl().nextDeadline();
        if (d >= 0 && (best < 0 || d < best)) best = d;
    }
    return best;
}

void App::Impl::renderDueWindows() {
    double now = platform::Platform::now();
    for (auto& w : windows) {
        double d = w->impl().nextDeadline();
        if (d >= 0 && d <= now + 0.001) w->impl().render();
    }
}

void App::Impl::reapClosedWindows() {
    std::erase_if(windows, [](const std::unique_ptr<Window>& w) { return w->impl().closed; });
}

App::App() : m_impl(std::make_unique<Impl>()) {
    if (g_app) {
        std::fprintf(stderr, "mayonnaise: only one mayo::App may exist at a time\n");
        std::abort();
    }
    g_app = this;
    if (!platform::Platform::init()) std::abort();
}

App::~App() {
    m_impl->windows.clear();

    platform::Platform::shutdown();
    g_app = nullptr;
}

App& App::current() { return *g_app; }
const Theme& theme() { return App::current().theme(); }

Window& App::createWindow(const WindowOptions& options) {
    platform::WindowConfig config;
    config.title = options.title;
    config.width = options.width;
    config.height = options.height;
    config.minWidth = options.minWidth;
    config.minHeight = options.minHeight;
    config.resizable = options.resizable;
    auto impl = std::make_unique<Window::Impl>(config);
    if (m_impl->windows.empty() && !gl::load()) {
        std::fprintf(stderr, "mayonnaise: failed to load OpenGL 3.3 functions\n");
        std::abort();
    }
    auto window = std::unique_ptr<Window>(new Window(std::move(impl)));
    Window& ref = *window;
    m_impl->windows.push_back(std::move(window));
    if (m_impl->running) ref.impl().platformWindow->show();
    return ref;
}

int App::run() {
    using platform::Platform;
    m_impl->running = true;
    // Render once before showing so the first visible frame isn't garbage.
    for (auto& w : m_impl->windows) {
        w->impl().render();
        w->impl().platformWindow->show();
        w->impl().needsFrame = true;
    }
    while (!m_impl->quitRequested && !m_impl->windows.empty()) {
        double deadline = m_impl->nextDeadline();
        double timeout = deadline < 0 ? -1.0 : std::max(0.0, deadline - Platform::now());
        platform::PlatformEvent event;
        if (Platform::waitEvent(event, timeout)) {
            m_impl->dispatch(event);
            while (Platform::pollEvent(event)) m_impl->dispatch(event);
        }
        m_impl->renderDueWindows();
        m_impl->reapClosedWindows();
    }
    m_impl->running = false;
    return m_impl->exitCode;
}

void App::quit(int exitCode) {
    m_impl->exitCode = exitCode;
    m_impl->quitRequested = true;
    platform::Platform::wake();
}

void App::setAppearance(Appearance appearance) {
    if (m_impl->appearance == appearance) return;
    m_impl->appearance = appearance;
    m_impl->rebuildTheme();
}
Appearance App::appearance() const { return m_impl->appearance; }
void App::setAccentColor(Color accent) {
    m_impl->accent = accent;
    m_impl->rebuildTheme();
}
Color App::accentColor() const { return m_impl->accent; }
const Theme& App::theme() const { return m_impl->theme; }

}  // namespace mayo
