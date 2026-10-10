#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>

#include "platform.hpp"

namespace mayo::platform {

namespace {

SDL_GLContext g_context = nullptr;
std::unordered_map<WindowId, PlatformWindow*> g_windows;
std::array<SDL_Cursor*, 6> g_cursors{};
CursorShape g_currentCursor = CursorShape::Arrow;
Uint32 g_wakeEvent = 0;

float envScaleOverride() {
    if (const char* s = std::getenv("MAYO_SCALE")) {
        float v = std::strtof(s, nullptr);
        if (v > 0.25f && v < 8.0f) return v;
    }
    return 0.0f;
}

Modifiers translateMods(SDL_Keymod m) {
    Modifiers out = Modifiers::None;
    if (m & SDL_KMOD_SHIFT) out = out | Modifiers::Shift;
    if (m & SDL_KMOD_CTRL) out = out | Modifiers::Ctrl;
    if (m & SDL_KMOD_ALT) out = out | Modifiers::Alt;
    if (m & SDL_KMOD_GUI) out = out | Modifiers::Super;
    return out;
}

Key translateKey(SDL_Keycode k) {
    if (k >= SDLK_A && k <= SDLK_Z) return static_cast<Key>(static_cast<int>(Key::A) + static_cast<int>(k - SDLK_A));
    if (k >= SDLK_0 && k <= SDLK_9)
        return static_cast<Key>(static_cast<int>(Key::Num0) + static_cast<int>(k - SDLK_0));
    if (k >= SDLK_F1 && k <= SDLK_F12)
        return static_cast<Key>(static_cast<int>(Key::F1) + static_cast<int>(k - SDLK_F1));
    switch (k) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return Key::Enter;
        case SDLK_ESCAPE: return Key::Escape;
        case SDLK_BACKSPACE: return Key::Backspace;
        case SDLK_TAB: return Key::Tab;
        case SDLK_SPACE: return Key::Space;
        case SDLK_DELETE: return Key::Delete;
        case SDLK_INSERT: return Key::Insert;
        case SDLK_LEFT: return Key::Left;
        case SDLK_RIGHT: return Key::Right;
        case SDLK_UP: return Key::Up;
        case SDLK_DOWN: return Key::Down;
        case SDLK_HOME: return Key::Home;
        case SDLK_END: return Key::End;
        case SDLK_PAGEUP: return Key::PageUp;
        case SDLK_PAGEDOWN: return Key::PageDown;
        default: return Key::Unknown;
    }
}

MouseButton translateButton(Uint8 b) {
    switch (b) {
        case SDL_BUTTON_LEFT: return MouseButton::Left;
        case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
        case SDL_BUTTON_RIGHT: return MouseButton::Right;
        default: return MouseButton::None;
    }
}

SDL_HitTestResult toSdl(HitRegion r) {
    switch (r) {
        case HitRegion::Normal: return SDL_HITTEST_NORMAL;
        case HitRegion::Draggable: return SDL_HITTEST_DRAGGABLE;
        case HitRegion::ResizeTop: return SDL_HITTEST_RESIZE_TOP;
        case HitRegion::ResizeBottom: return SDL_HITTEST_RESIZE_BOTTOM;
        case HitRegion::ResizeLeft: return SDL_HITTEST_RESIZE_LEFT;
        case HitRegion::ResizeRight: return SDL_HITTEST_RESIZE_RIGHT;
        case HitRegion::ResizeTopLeft: return SDL_HITTEST_RESIZE_TOPLEFT;
        case HitRegion::ResizeTopRight: return SDL_HITTEST_RESIZE_TOPRIGHT;
        case HitRegion::ResizeBottomLeft: return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        case HitRegion::ResizeBottomRight: return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    }
    return SDL_HITTEST_NORMAL;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
struct PlatformWindow::Impl {
    SDL_Window* window = nullptr;
    std::function<HitRegion(Point)> hitTest;
    float scaleOverride = 0.0f;
    bool textInput = false;

    static SDL_HitTestResult hitTestThunk(SDL_Window* /*win*/, const SDL_Point* area, void* data) {
        auto* self = static_cast<PlatformWindow*>(data);
        auto& impl = *self->m_impl;
        if (!impl.hitTest) return SDL_HITTEST_NORMAL;
        float k = self->pointsPerWindowUnit();
        return toSdl(impl.hitTest(Point{static_cast<float>(area->x) * k, static_cast<float>(area->y) * k}));
    }
};

PlatformWindow::PlatformWindow(const WindowConfig& config) : m_impl(std::make_unique<Impl>()) {
    m_impl->scaleOverride = envScaleOverride();
    const char* noTransparency = std::getenv("MAYO_NO_TRANSPARENCY");
    m_transparent = config.transparent && !(noTransparency && *noTransparency && *noTransparency != '0');

    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, m_transparent ? 8 : 0);
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (config.resizable) flags |= SDL_WINDOW_RESIZABLE;
    if (m_transparent) flags |= SDL_WINDOW_TRANSPARENT;

    m_impl->window = SDL_CreateWindow(config.title.c_str(), config.width, config.height, flags);
    if (!m_impl->window && m_transparent) {
        // Some X11 setups have no ARGB visual; fall back to an opaque window.
        m_transparent = false;
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
        flags &= ~SDL_WINDOW_TRANSPARENT;
        m_impl->window = SDL_CreateWindow(config.title.c_str(), config.width, config.height, flags);
    }
    if (!m_impl->window) {
        SDL_Log("mayonnaise: SDL_CreateWindow failed: %s", SDL_GetError());
        std::abort();
    }

    // Requested size is in points; SDL sizes are in window units, which differ on X11 with scaling.
    float k = pointsPerWindowUnit();
    if (std::abs(k - 1.0f) > 0.01f) {
        SDL_SetWindowSize(m_impl->window, static_cast<int>(std::lround(config.width / k)),
                          static_cast<int>(std::lround(config.height / k)));
    }
    setMinimumSize(config.minWidth, config.minHeight);

    if (!g_context) {
        g_context = SDL_GL_CreateContext(m_impl->window);
        if (!g_context) {
            SDL_Log("mayonnaise: could not create an OpenGL 3.3 core context: %s", SDL_GetError());
            std::abort();
        }
        SDL_GL_MakeCurrent(m_impl->window, g_context);
    } else {
        SDL_GL_MakeCurrent(m_impl->window, g_context);
    }
    SDL_GL_SetSwapInterval(1);

    g_windows[id()] = this;
}

PlatformWindow::~PlatformWindow() {
    g_windows.erase(id());
    if (m_impl->textInput) SDL_StopTextInput(m_impl->window);
    SDL_DestroyWindow(m_impl->window);
}

WindowId PlatformWindow::id() const { return SDL_GetWindowID(m_impl->window); }

float PlatformWindow::scale() const {
    if (m_impl->scaleOverride > 0) return m_impl->scaleOverride;
    float s = SDL_GetWindowDisplayScale(m_impl->window);
    return s > 0 ? s : 1.0f;
}

float PlatformWindow::pointsPerWindowUnit() const {
    float density = SDL_GetWindowPixelDensity(m_impl->window);
    if (density <= 0) density = 1.0f;
    return density / scale();
}

Size PlatformWindow::sizeInPoints() const {
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(m_impl->window, &w, &h);
    float k = pointsPerWindowUnit();
    return {static_cast<float>(w) * k, static_cast<float>(h) * k};
}

void PlatformWindow::pixelSize(int& w, int& h) const { SDL_GetWindowSizeInPixels(m_impl->window, &w, &h); }

bool PlatformWindow::isMaximized() const { return (SDL_GetWindowFlags(m_impl->window) & SDL_WINDOW_MAXIMIZED) != 0; }
bool PlatformWindow::isFullscreen() const {
    return (SDL_GetWindowFlags(m_impl->window) & SDL_WINDOW_FULLSCREEN) != 0;
}

void PlatformWindow::setTitle(const std::string& title) { SDL_SetWindowTitle(m_impl->window, title.c_str()); }
void PlatformWindow::show() { SDL_ShowWindow(m_impl->window); }
void PlatformWindow::minimize() { SDL_MinimizeWindow(m_impl->window); }
void PlatformWindow::toggleMaximize() {
    if (isMaximized())
        SDL_RestoreWindow(m_impl->window);
    else
        SDL_MaximizeWindow(m_impl->window);
}
void PlatformWindow::setFullscreen(bool fullscreen) { SDL_SetWindowFullscreen(m_impl->window, fullscreen); }

void PlatformWindow::setMinimumSize(int w, int h) {
    float k = pointsPerWindowUnit();
    SDL_SetWindowMinimumSize(m_impl->window, static_cast<int>(static_cast<float>(w) / k),
                             static_cast<int>(static_cast<float>(h) / k));
}

void PlatformWindow::makeCurrent() { SDL_GL_MakeCurrent(m_impl->window, g_context); }
void PlatformWindow::swapBuffers() { SDL_GL_SwapWindow(m_impl->window); }

void PlatformWindow::setHitTest(std::function<HitRegion(Point)> callback) {
    m_impl->hitTest = std::move(callback);
    SDL_SetWindowHitTest(m_impl->window, m_impl->hitTest ? &Impl::hitTestThunk : nullptr, this);
}

void PlatformWindow::setTextInputActive(bool active, Rect caretRect) {
    if (active) {
        float k = pointsPerWindowUnit();
        SDL_Rect r{static_cast<int>(caretRect.x / k), static_cast<int>(caretRect.y / k),
                   static_cast<int>(caretRect.width / k), static_cast<int>(caretRect.height / k)};
        SDL_SetTextInputArea(m_impl->window, &r, 0);
        if (!m_impl->textInput) SDL_StartTextInput(m_impl->window);
    } else if (m_impl->textInput) {
        SDL_StopTextInput(m_impl->window);
    }
    m_impl->textInput = active;
}

// ---------------------------------------------------------------------------------------------
bool Platform::init() {
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    // Without this, the first click on an unfocused window is swallowed — macOS delivers it.
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("mayonnaise: SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifndef NDEBUG
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    g_wakeEvent = SDL_RegisterEvents(1);
    return true;
}

void Platform::shutdown() {
    for (auto*& c : g_cursors) {
        if (c) SDL_DestroyCursor(c);
        c = nullptr;
    }
    if (g_context) {
        SDL_GL_DestroyContext(g_context);
        g_context = nullptr;
    }
    SDL_Quit();
}

namespace {

bool translate(const SDL_Event& e, PlatformEvent& out) {
    out = PlatformEvent{};
    auto toPoints = [](SDL_WindowID wid, float x, float y) {
        auto it = g_windows.find(wid);
        float k = it != g_windows.end() ? it->second->pointsPerWindowUnit() : 1.0f;
        return Point{x * k, y * k};
    };
    switch (e.type) {
        case SDL_EVENT_QUIT: out.type = EventType::Quit; return true;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: out.type = EventType::WindowClose; break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: out.type = EventType::WindowResized; break;
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: out.type = EventType::WindowScaleChanged; break;
        case SDL_EVENT_WINDOW_EXPOSED: out.type = EventType::WindowExposed; break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED: out.type = EventType::WindowFocusGained; break;
        case SDL_EVENT_WINDOW_FOCUS_LOST: out.type = EventType::WindowFocusLost; break;
        case SDL_EVENT_WINDOW_MAXIMIZED: out.type = EventType::WindowMaximized; break;
        case SDL_EVENT_WINDOW_RESTORED: out.type = EventType::WindowRestored; break;
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: out.type = EventType::WindowResized; break;
        case SDL_EVENT_WINDOW_MINIMIZED: out.type = EventType::WindowMinimized; break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE: out.type = EventType::MouseLeave; break;
        case SDL_EVENT_MOUSE_MOTION:
            out.type = EventType::MouseMove;
            out.window = e.motion.windowID;
            out.position = toPoints(e.motion.windowID, e.motion.x, e.motion.y);
            out.modifiers = translateMods(SDL_GetModState());
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            out.type = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? EventType::MouseDown : EventType::MouseUp;
            out.window = e.button.windowID;
            out.position = toPoints(e.button.windowID, e.button.x, e.button.y);
            out.button = translateButton(e.button.button);
            out.clicks = e.button.clicks;
            out.modifiers = translateMods(SDL_GetModState());
            return true;
        case SDL_EVENT_MOUSE_WHEEL: {
            out.type = EventType::Scroll;
            out.window = e.wheel.windowID;
            out.position = toPoints(e.wheel.windowID, e.wheel.mouse_x, e.wheel.mouse_y);
            float dx = e.wheel.x;
            float dy = e.wheel.y;
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                dx = -dx;
                dy = -dy;
            }
            // Touchpads report fractional deltas; wheels report whole notches.
            out.precise = dx != std::floor(dx) || dy != std::floor(dy);
            out.delta = Vec2{dx, -dy};
            out.modifiers = translateMods(SDL_GetModState());
            return true;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            out.type = e.type == SDL_EVENT_KEY_DOWN ? EventType::KeyDown : EventType::KeyUp;
            out.window = e.key.windowID;
            out.key = translateKey(e.key.key);
            out.modifiers = translateMods(e.key.mod);
            out.repeat = e.key.repeat;
            return true;
        case SDL_EVENT_DROP_FILE:
        case SDL_EVENT_DROP_POSITION:
        case SDL_EVENT_DROP_COMPLETE:
            out.type = e.type == SDL_EVENT_DROP_FILE       ? EventType::DropFile
                       : e.type == SDL_EVENT_DROP_POSITION ? EventType::DropPosition
                                                           : EventType::DropComplete;
            out.window = e.drop.windowID;
            out.position = toPoints(e.drop.windowID, e.drop.x, e.drop.y);
            out.text = e.drop.data ? e.drop.data : "";
            return true;
        case SDL_EVENT_PINCH_UPDATE:
            out.type = EventType::Pinch;
            out.window = e.pinch.windowID;
            out.scale = e.pinch.scale;
            return true;
        case SDL_EVENT_TEXT_INPUT:
            out.type = EventType::TextInput;
            out.window = e.text.windowID;
            out.text = e.text.text ? e.text.text : "";
            return true;
        default:
            if (e.type == g_wakeEvent) {
                out.type = EventType::Wake;
                return true;
            }
            return false;
    }
    out.window = e.window.windowID;
    return true;
}

}  // namespace

bool Platform::waitEvent(PlatformEvent& out, double timeoutSeconds) {
    SDL_Event e;
    Sint32 ms = timeoutSeconds < 0 ? -1 : static_cast<Sint32>(std::ceil(timeoutSeconds * 1000.0));
    while (SDL_WaitEventTimeout(&e, ms)) {
        if (translate(e, out)) return true;
        ms = 0;  // swallowed an untranslated event; don't block again past the deadline
    }
    return false;
}

bool Platform::pollEvent(PlatformEvent& out) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (translate(e, out)) return true;
    }
    return false;
}

void Platform::wake() {
    SDL_Event e;
    SDL_zero(e);
    e.type = g_wakeEvent;
    SDL_PushEvent(&e);
}

double Platform::now() { return static_cast<double>(SDL_GetTicksNS()) / 1e9; }

void Platform::setCursor(CursorShape shape) {
    if (shape == g_currentCursor && g_cursors[static_cast<size_t>(shape)]) return;
    g_currentCursor = shape;
    auto& c = g_cursors[static_cast<size_t>(shape)];
    if (!c) {
        SDL_SystemCursor sys = SDL_SYSTEM_CURSOR_DEFAULT;
        switch (shape) {
            case CursorShape::Arrow: sys = SDL_SYSTEM_CURSOR_DEFAULT; break;
            case CursorShape::IBeam: sys = SDL_SYSTEM_CURSOR_TEXT; break;
            case CursorShape::PointingHand: sys = SDL_SYSTEM_CURSOR_POINTER; break;
            case CursorShape::ResizeHorizontal: sys = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
            case CursorShape::ResizeVertical: sys = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
            case CursorShape::NotAllowed: sys = SDL_SYSTEM_CURSOR_NOT_ALLOWED; break;
        }
        c = SDL_CreateSystemCursor(sys);
    }
    if (c) SDL_SetCursor(c);
}

std::string Platform::clipboardText() {
    char* text = SDL_GetClipboardText();
    std::string out = text ? text : "";
    SDL_free(text);
    return out;
}

void Platform::setClipboardText(const std::string& text) { SDL_SetClipboardText(text.c_str()); }

float Platform::doubleClickTime() { return 0.5f; }

namespace {
struct DialogRequest {
    std::vector<std::string> names;
    std::vector<std::string> patterns;
    std::vector<SDL_DialogFileFilter> filters;  // points into names/patterns; must outlive the dialog
    std::function<void(std::vector<std::string>)> done;
};

void SDLCALL dialogCallback(void* userdata, const char* const* files, int /*filter*/) {
    std::unique_ptr<DialogRequest> req(static_cast<DialogRequest*>(userdata));
    std::vector<std::string> out;
    if (!files) SDL_Log("mayonnaise: file dialog failed: %s", SDL_GetError());
    for (const char* const* f = files; f && *f; ++f) out.emplace_back(*f);
    if (req->done) req->done(std::move(out));
}
}  // namespace

void Platform::showOpenDialog(PlatformWindow* parent, const std::vector<DialogFilter>& filters, bool folders,
                              bool multiple, std::function<void(std::vector<std::string>)> done) {
    auto req = std::make_unique<DialogRequest>();
    req->done = std::move(done);
    for (const auto& f : filters) {
        req->names.push_back(f.name);
        req->patterns.push_back(f.pattern);
    }
    for (size_t i = 0; i < filters.size(); ++i) req->filters.push_back({req->names[i].c_str(), req->patterns[i].c_str()});
    SDL_Window* win = parent ? parent->impl().window : nullptr;
    DialogRequest* raw = req.release();  // owned by the callback from here on
    if (folders)
        SDL_ShowOpenFolderDialog(dialogCallback, raw, win, nullptr, multiple);
    else
        SDL_ShowOpenFileDialog(dialogCallback, raw, win, raw->filters.data(), static_cast<int>(raw->filters.size()),
                               nullptr, multiple);
}

PlatformWindow* Platform::windowFromId(WindowId id) {
    auto it = g_windows.find(id);
    return it != g_windows.end() ? it->second : nullptr;
}

void* Platform::glProcAddress(const char* name) {
    return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
}

}  // namespace mayo::platform
