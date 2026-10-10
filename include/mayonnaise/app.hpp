#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "color.hpp"
#include "theme.hpp"

namespace mayo {

class Window;

struct WindowOptions {
    std::string title = "Mayonnaise";
    int width = 800;    ///< points
    int height = 600;   ///< points
    int minWidth = 320;
    int minHeight = 200;
    bool resizable = true;
};

struct FileFilter {
    std::string name;     ///< "Images"
    std::string pattern;  ///< "png;jpg;jpeg" (semicolon separated extensions, "*" for all)
};

/// Owns the event loop, the windows, and the global appearance. Create exactly one.
class App {
public:
    App();
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    /// The returned reference stays valid until the window is closed.
    Window& createWindow(const WindowOptions& options = {});

    /// Runs until the last window closes or quit() is called. Returns the process exit code.
    int run();
    void quit(int exitCode = 0);

    /// Runs `fn` on the UI thread at the next loop iteration. Safe to call from any thread.
    void post(std::function<void()> fn);
    /// Runs `fn` on the UI thread after `seconds`.
    void after(double seconds, std::function<void()> fn);

    /// Native "Open" dialog. `done` runs on the UI thread with the chosen paths (empty if cancelled).
    void openFiles(const std::vector<FileFilter>& filters, bool multiple,
                   std::function<void(std::vector<std::string>)> done, Window* parent = nullptr);
    void openFolder(std::function<void(std::vector<std::string>)> done, Window* parent = nullptr);

    void setAppearance(Appearance appearance);
    [[nodiscard]] Appearance appearance() const;
    void setAccentColor(Color accent);
    [[nodiscard]] Color accentColor() const;

    [[nodiscard]] const Theme& theme() const;

    /// The live App; only valid between construction and destruction.
    static App& current();

    struct Impl;
    [[nodiscard]] Impl& impl() { return *m_impl; }

private:
    std::unique_ptr<Impl> m_impl;
};

/// Shorthand for `App::current().theme()`.
const Theme& theme();

}  // namespace mayo
