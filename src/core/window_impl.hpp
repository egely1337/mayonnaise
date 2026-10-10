#pragma once

#include <memory>
#include <string>

#include "../platform/platform.hpp"
#include "mayonnaise/window.hpp"

namespace mayo {

struct Window::Impl {
    Window* self = nullptr;
    std::unique_ptr<platform::PlatformWindow> platformWindow;
    std::string title;
    bool needsFrame = true;
    bool closed = false;

    explicit Impl(const platform::WindowConfig& config);
    void handleEvent(const platform::PlatformEvent& event);
    void render();
    [[nodiscard]] double nextDeadline() const { return needsFrame ? 0.0 : -1.0; }
};

}  // namespace mayo
