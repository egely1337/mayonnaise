#pragma once

// The calculator window's content: the display and the keypad, drawn as one custom widget so the keys can
// be laid out as a grid that scales with the window. Keys flash when typed on the keyboard, the pending
// operator stays lit, and the clear key turns into ⌫ while a number is being typed.

#include <mayonnaise/mayonnaise.hpp>

#include <functional>
#include <string>
#include <vector>

#include "engine.hpp"

namespace calc {

enum class Mode : std::uint8_t { Basic, Scientific };

class CalculatorView final : public mayo::Widget {
public:
    CalculatorView();

    void setMode(Mode mode);
    [[nodiscard]] Mode mode() const { return m_mode; }
    void setDegrees(bool degrees);
    [[nodiscard]] bool degrees() const { return m_engine.degrees(); }
    void setNumberFormat(const NumberFormat& format);
    [[nodiscard]] Engine& engine() { return m_engine; }

    void copy();
    void paste();

    /// The mode key (bottom-left) was clicked.
    std::function<void()> onToggleMode;
    /// The Rad/Deg key changed the angle unit.
    std::function<void(bool degrees)> onDegreesChanged;
    std::function<void(mayo::Point)> onContextMenu;

    // Widget -----------------------------------------------------------------------------------------
    mayo::Size measureContent(mayo::Size available) override { return available; }
    [[nodiscard]] bool expandsContent(mayo::Axis /*axis*/) const override { return true; }
    [[nodiscard]] bool extendsIntoTitleBar() const override { return true; }
    void arrangeContent(const mayo::Rect& content) override;
    void draw(mayo::Painter& p) override;
    [[nodiscard]] bool acceptsFocus() const override { return true; }
    [[nodiscard]] bool isInteractive() const override { return true; }
    bool onPointerDown(const mayo::PointerEvent& e) override;
    void onPointerMove(const mayo::PointerEvent& e) override;
    void onPointerUp(const mayo::PointerEvent& e) override;
    void onPointerLeave() override;
    bool onKeyDown(const mayo::KeyEvent& e) override;
    bool onTextInput(const mayo::TextInputEvent& e) override;
    void onFocusChanged(bool focused) override;

private:
    enum class Style : std::uint8_t { Digit, Function, Operator, Scientific };
    enum class Action : std::uint8_t {
        Digit, Point, Operator, Equals, Clear, Negate, Percent, Function, Constant, OpenParen, CloseParen,
        MemoryClear, MemoryAdd, MemorySubtract, MemoryRecall, Second, RadDeg, Mode,
    };
    /// A key label with optional small raised/lowered parts: "x" + sup "2", "log" + sub "10", sup-prefix "3" + "√x".
    struct Label {
        std::string main{};
        std::string sup{};
        std::string sub{};
        std::string preSup{};
    };
    struct Pad {
        Action action = Action::Digit;
        int value = 0;   // digit, Op, Fn or Constant
        int second = -1;  // Fn used while 2nd is on (-1: same)
        Label label;
        Label secondLabel;
        Style style = Style::Digit;
        mayo::Rect rect;
        mayo::Animated<float> press{0.0f};  // pressed / flashed by the keyboard
        mayo::Animated<float> lit{0.0f};    // pending operator, active 2nd
    };

    void buildKeys();
    void layoutKeys();
    void activate(Pad& key);
    void flash(Action action, int value = 0);
    void changed();  // engine state moved: refresh lit keys, redraw
    [[nodiscard]] int keyAt(mayo::Point p) const;
    [[nodiscard]] mayo::Rect displayRect() const;
    void drawKey(mayo::Painter& p, const Pad& key, bool hovered, double t);
    void drawLabel(mayo::Painter& p, const Label& label, const mayo::Rect& r, const mayo::Font& font,
                   const mayo::Color& color) const;
    void drawGlyph(mayo::Painter& p, const Pad& key, const mayo::Rect& r, const mayo::Color& color) const;
    void drawDisplay(mayo::Painter& p, double t);

    Engine m_engine;
    Mode m_mode = Mode::Basic;
    bool m_second = false;
    std::vector<Pad> m_keys;
    float m_keypadTop = 0;
    float m_keyHeight = 48;
    int m_hovered = -1;
    int m_pressed = -1;
    mayo::Animated<float> m_copyBlink{1.0f};
};

}  // namespace calc
