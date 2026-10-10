#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../platform/platform.hpp"
#include "mayonnaise/app.hpp"
#include "mayonnaise/window.hpp"

namespace mayo {

namespace render {
class Renderer2D;
}

struct App::Impl {
    std::vector<std::unique_ptr<Window>> windows;
    Theme theme;
    Appearance appearance = Appearance::Light;
    Color accent = Theme::defaultAccent();
    bool running = false;
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
