#pragma once

// One terminal session: a shell in a pseudo-terminal, the emulated screen, and the widget that draws it
// and turns keys, mouse and paste into input.

#include <mayonnaise/mayonnaise.hpp>

#include <array>
#include <functional>
#include <memory>
#include <string>

#include "profiles.hpp"
#include "pty.hpp"
#include "screen.hpp"

namespace term {

class TerminalView : public mayo::Widget {
public:
    TerminalView(mayo::App& app, std::shared_ptr<Config> config);
    ~TerminalView() override;

    /// Starts the shell in `cwd` (empty: home). Call once the view has a size.
    bool start(const std::string& cwd, std::string& error);
    /// Config or appearance changed (font, profile, cursor...).
    void applyConfig();

    /// OSC title if the program set one, otherwise the foreground program's name.
    [[nodiscard]] std::string title() const;
    [[nodiscard]] std::string currentDirectory() const { return m_pty.currentDirectory(); }
    [[nodiscard]] bool busy() const { return m_pty.busy(); }
    [[nodiscard]] bool exited() const { return m_exited; }
    [[nodiscard]] int cols() const { return m_screen.cols(); }
    [[nodiscard]] int rows() const { return m_screen.rows(); }

    void copySelection();
    void paste();
    void pasteText(const std::string& text);
    /// Bytes as if typed.
    void sendInput(std::string_view bytes) {
        send(bytes);
        userInput();
    }
    void selectAll();
    void clearScrollback();
    [[nodiscard]] bool hasSelection() const { return m_selection.active; }

    std::function<void()> onTitleChanged;
    std::function<void(int status)> onExit;
    std::function<void(TerminalView&, mayo::Point)> onContextMenu;
    std::function<void(const std::string& url)> onOpenUrl;

    // Widget ---------------------------------------------------------------------------------------
    mayo::Size measureContent(mayo::Size available) override { return available; }
    [[nodiscard]] bool expandsContent(mayo::Axis /*axis*/) const override { return true; }
    void arrangeContent(const mayo::Rect& content) override;
    void draw(mayo::Painter& p) override;
    [[nodiscard]] bool acceptsFocus() const override { return true; }
    [[nodiscard]] bool focusOnClick() const override { return true; }
    [[nodiscard]] bool isInteractive() const override { return true; }
    bool onKeyDown(const mayo::KeyEvent& e) override;
    bool onTextInput(const mayo::TextInputEvent& e) override;
    bool onPointerDown(const mayo::PointerEvent& e) override;
    void onPointerMove(const mayo::PointerEvent& e) override;
    void onPointerUp(const mayo::PointerEvent& e) override;
    bool onScroll(const mayo::ScrollEvent& e) override;
    void onFocusChanged(bool focused) override;
    void onThemeChanged() override { applyConfig(); }

    /// The URL under a window point, if any (for Ctrl+click and the context menu).
    [[nodiscard]] std::string urlAt(mayo::Point p) const;

private:
    struct Pos {
        std::int64_t line = 0;  // global line number (dropped + absolute index)
        int col = 0;
        auto operator<=>(const Pos&) const = default;
    };
    enum class SelectMode : std::uint8_t { Char, Word, Line };
    struct Selection {
        bool active = false;
        Pos anchor;
        Pos head;
        SelectMode mode = SelectMode::Char;
    };

    void output();
    void send(std::string_view bytes);
    void userInput();  // scrolls back to the live screen, restarts the cursor blink
    void updateMetrics(float scale);
    void updateGrid();
    [[nodiscard]] mayo::Color resolve(ColorRef c, bool foreground, std::uint16_t attrs) const;
    [[nodiscard]] Pos cellAt(mayo::Point p, bool clampToGrid = true) const;
    [[nodiscard]] std::int64_t topLine() const;  // global line shown in the first row
    [[nodiscard]] const Line* lineGlobal(std::int64_t line) const;
    [[nodiscard]] bool selected(std::int64_t line, int col) const;
    void selectionBounds(Pos& start, Pos& end) const;
    [[nodiscard]] std::string selectedText() const;
    void extendWord(Pos& start, Pos& end) const;
    void sendMouse(int button, bool press, bool motion, mayo::Point p, mayo::Modifiers mods);
    [[nodiscard]] bool reportsMouse(mayo::Modifiers mods) const;
    bool drawSpecial(mayo::Painter& p, char32_t cp, const mayo::Rect& cell, const mayo::Color& color) const;
    [[nodiscard]] std::string lineText(std::int64_t line, std::vector<int>* columns = nullptr) const;

    mayo::App& m_app;
    std::shared_ptr<Config> m_config;
    Screen m_screen;
    Pty m_pty;
    Profile m_profile;
    std::array<mayo::Color, 256> m_palette{};
    std::array<bool, 256> m_paletteOverridden{};
    std::array<mayo::Color, 256> m_overrides{};

    mayo::Font m_font;
    float m_scale = 0;      // scale the cell metrics were computed for (0 = stale)
    float m_drawScale = 1;  // device scale seen at the last draw
    float m_cellW = 8;
    float m_cellH = 16;
    float m_baseline = 12;
    mayo::Point m_origin;  // top-left of the grid, pixel aligned
    bool m_started = false;
    bool m_exited = false;

    int m_scrollOffset = 0;  // lines scrolled back into history (0 = live)
    float m_scrollAccum = 0;
    double m_lastScroll = -10;

    Selection m_selection;
    bool m_selecting = false;
    int m_mouseButton = -1;  // pressed button being reported to the program
    Pos m_lastMouseCell{-1, -1};

    double m_blinkStart = 0;
    mayo::Animated<float> m_bell{0.0f};
    std::string m_pendingAlt;  // Alt+key waiting to see whether AltGr produced a character instead
    mayo::Rect m_imeRect;
};

}  // namespace term
