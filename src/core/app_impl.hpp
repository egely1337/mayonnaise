#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../platform/platform.hpp"
#include "../render/renderer2d.hpp"
#include "../render/text_system.hpp"
#include "mayonnaise/app.hpp"
#include "mayonnaise/window.hpp"

namespace mayo {

struct App::Impl {
    std::vector<std::unique_ptr<Window>> windows;
    Theme theme;
    Appearance appearance = Appearance::Light;
    Color accent = Theme::defaultAccent();
    bool running = false;
    std::unique_ptr<render::Renderer2D> renderer;  // shared by all windows (one GL context)
    std::unique_ptr<render::TextSystem> text;
    bool quitRequested = false;
    int exitCode = 0;



    // MAYO_SCREENSHOT support: dump the first window after N frames and quit.
    std::string screenshotPath;
    int screenshotFrames = 0;
    int framesRendered = 0;

    Impl();
    ~Impl();
    void rebuildTheme();
    void dispatch(const platform::PlatformEvent& event);
    [[nodiscard]] double nextDeadline() const;  // absolute time; <0 = none
    void renderDueWindows();
    void reapClosedWindows();
};

}  // namespace mayo
