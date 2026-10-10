#pragma once

#include <memory>
#include <string>

#include "geometry.hpp"

namespace mayo {

class Element;

/// A top-level window with client-side macOS-style chrome. Created through App::createWindow.
class Window {
public:
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void setContent(const Element& content);
    void setTitle(const std::string& title);
    [[nodiscard]] const std::string& title() const;
    [[nodiscard]] Size size() const;
    void close();

    struct Impl;
    [[nodiscard]] Impl& impl() { return *m_impl; }

private:
    friend class App;
    explicit Window(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

}  // namespace mayo
