#include "calculator_view.hpp"

#include <algorithm>
#include <cmath>

using namespace mayo;

namespace calc {

namespace {

constexpr float kPad = 12;
constexpr float kMinDisplay = 118;  // display height the keypad never eats into
constexpr float kPressFade = 0.22f;

float gapFor(float keyHeight) { return std::clamp(keyHeight * 0.19f, 6.0f, 14.0f); }

}  // namespace

CalculatorView::CalculatorView() { buildKeys(); }

void CalculatorView::setMode(Mode mode) {
    if (mode == m_mode) return;
    m_mode = mode;
    m_second = false;
    buildKeys();
    invalidateLayout();
}

void CalculatorView::setDegrees(bool degrees) {
    m_engine.setDegrees(degrees);
    for (auto& k : m_keys) {
        if (k.action == Action::RadDeg) k.label.main = degrees ? "Rad" : "Deg";
    }
    invalidate();
}

void CalculatorView::setNumberFormat(const NumberFormat& format) {
    m_engine.setFormat(format);
    for (auto& k : m_keys) {
        if (k.action == Action::Point) k.label.main = std::string(1, format.decimal);
    }
    invalidate();
}

// --- keys --------------------------------------------------------------------------------------------------

void CalculatorView::buildKeys() {
    m_keys.clear();
    m_hovered = m_pressed = -1;
    auto key = [](Action a, int value, Label label, Style style, int second = -1, Label secondLabel = {}) {
        Pad k;
        k.action = a;
        k.value = value;
        k.label = std::move(label);
        k.style = style;
        k.second = second;
        k.secondLabel = std::move(secondLabel);
        return k;
    };
    auto fn = [&](Fn f, Label l, Fn second, Label sl) {
        return key(Action::Function, static_cast<int>(f), std::move(l), Style::Scientific, static_cast<int>(second), std::move(sl));
    };
    auto fn1 = [&](Fn f, Label l) { return key(Action::Function, static_cast<int>(f), std::move(l), Style::Scientific); };
    auto opKey = [&](Op o, Style s, Label l) { return key(Action::Operator, static_cast<int>(o), std::move(l), s); };
    auto digit = [&](int d) { return key(Action::Digit, d, {std::to_string(d)}, Style::Digit); };
    const std::string decimal(1, m_engine.numberFormat().decimal);

    // Rows of the scientific block (left of the basic keys in scientific mode), then the basic keys.
    std::vector<std::vector<Pad>> sci{
        {key(Action::OpenParen, 0, {"("}, Style::Scientific), key(Action::CloseParen, 0, {")"}, Style::Scientific),
         key(Action::MemoryClear, 0, {"mc"}, Style::Scientific), key(Action::MemoryAdd, 0, {"m+"}, Style::Scientific),
         key(Action::MemorySubtract, 0, {"m−"}, Style::Scientific), key(Action::MemoryRecall, 0, {"mr"}, Style::Scientific)},
        {key(Action::Second, 0, {"2", "nd"}, Style::Scientific), fn1(Fn::Square, {"x", "2"}), fn1(Fn::Cube, {"x", "3"}),
         opKey(Op::Power, Style::Scientific, {"x", "y"}), fn(Fn::Exp, {"e", "x"}, Fn::Exp2, {"2", "x"}), fn1(Fn::Exp10, {"10", "x"})},
        {fn1(Fn::Reciprocal, {"/x", "", "", "1"}), fn1(Fn::SquareRoot, {"√x", "", "", "2"}),
         fn1(Fn::CubeRoot, {"√x", "", "", "3"}), opKey(Op::Root, Style::Scientific, {"√x", "", "", "y"}),
         fn(Fn::Ln, {"ln"}, Fn::Log2, {"log", "", "2"}), fn1(Fn::Log10, {"log", "", "10"})},
        {fn1(Fn::Factorial, {"x!"}), fn(Fn::Sin, {"sin"}, Fn::Asin, {"sin", "−1"}), fn(Fn::Cos, {"cos"}, Fn::Acos, {"cos", "−1"}),
         fn(Fn::Tan, {"tan"}, Fn::Atan, {"tan", "−1"}), key(Action::Constant, static_cast<int>(Constant::E), {"e"}, Style::Scientific),
         opKey(Op::EE, Style::Scientific, {"EE"})},
        {key(Action::RadDeg, 0, {m_engine.degrees() ? "Rad" : "Deg"}, Style::Scientific),
         fn(Fn::Sinh, {"sinh"}, Fn::Asinh, {"sinh", "−1"}), fn(Fn::Cosh, {"cosh"}, Fn::Acosh, {"cosh", "−1"}),
         fn(Fn::Tanh, {"tanh"}, Fn::Atanh, {"tanh", "−1"}), key(Action::Constant, static_cast<int>(Constant::Pi), {"π"}, Style::Scientific),
         key(Action::Constant, static_cast<int>(Constant::Random), {"Rand"}, Style::Scientific)},
    };
    std::vector<std::vector<Pad>> basic{
        {key(Action::Clear, 0, {"AC"}, Style::Function), key(Action::Negate, 0, {}, Style::Function),
         key(Action::Percent, 0, {"%"}, Style::Function), opKey(Op::Divide, Style::Operator, {})},
        {digit(7), digit(8), digit(9), opKey(Op::Multiply, Style::Operator, {})},
        {digit(4), digit(5), digit(6), opKey(Op::Subtract, Style::Operator, {})},
        {digit(1), digit(2), digit(3), opKey(Op::Add, Style::Operator, {})},
        {key(Action::Mode, 0, {}, Style::Function), digit(0), key(Action::Point, 0, {decimal}, Style::Digit),
         key(Action::Equals, 0, {}, Style::Operator)},
    };
    for (size_t row = 0; row < basic.size(); ++row) {
        if (m_mode == Mode::Scientific) {
            for (auto& k : sci[row]) m_keys.push_back(std::move(k));
        }
        for (auto& k : basic[row]) m_keys.push_back(std::move(k));
    }
    changed();
}

void CalculatorView::arrangeContent(const Rect& /*content*/) { layoutKeys(); }

void CalculatorView::layoutKeys() {
    const Rect r = frame();
    const int cols = m_mode == Mode::Scientific ? 10 : 4;
    const int rows = 5;
    // Keys fill the width; they are at most square, and a tall window gives the extra height to the display.
    float gap = gapFor(48);
    float cellW = 48;
    for (int i = 0; i < 2; ++i) {  // the gap depends on the key size; one refinement is plenty
        cellW = (r.width - kPad * 2 - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        gap = gapFor(cellW);
    }
    const float maxH = (r.height - kMinDisplay - kPad - gap * static_cast<float>(rows - 1)) / static_cast<float>(rows);
    m_keyHeight = std::max(20.0f, std::min(cellW, maxH));
    m_keypadTop = r.bottom() - kPad - m_keyHeight * static_cast<float>(rows) - gap * static_cast<float>(rows - 1);
    for (size_t i = 0; i < m_keys.size(); ++i) {
        const auto row = static_cast<float>(static_cast<int>(i) / cols);
        const auto col = static_cast<float>(static_cast<int>(i) % cols);
        m_keys[i].rect = {r.x + kPad + col * (cellW + gap), m_keypadTop + row * (m_keyHeight + gap), cellW, m_keyHeight};
    }
}

int CalculatorView::keyAt(Point p) const {
    for (size_t i = 0; i < m_keys.size(); ++i) {
        // A little slack between keys so fast clicks near an edge still land.
        if (m_keys[i].rect.outset(2).contains(p)) return static_cast<int>(i);
    }
    return -1;
}

Rect CalculatorView::displayRect() const {
    const Rect r = frame();
    return {r.x, r.y, r.width, m_keypadTop - r.y};
}

void CalculatorView::changed() {
    const double t = now();
    const auto pending = m_engine.pendingOp();
    for (auto& k : m_keys) {
        bool lit = false;
        if (k.action == Action::Operator && k.style == Style::Operator) lit = pending && static_cast<int>(*pending) == k.value;
        if (k.action == Action::Second) lit = m_second;
        if (k.action == Action::MemoryRecall) lit = m_engine.hasMemory();
        const float target = lit ? 1.0f : 0.0f;
        if (k.lit.target() != target) k.lit.animateTo(target, t, Transition{0.18f, Curve::EaseOut});
    }
    invalidate();
}

void CalculatorView::activate(Pad& k) {
    Engine& e = m_engine;
    switch (k.action) {
        case Action::Digit: e.digit(k.value); break;
        case Action::Point: e.point(); break;
        case Action::Operator: e.op(static_cast<Op>(k.value)); break;
        case Action::Equals: e.equals(); break;
        case Action::Clear:
            if (e.isTyping()) e.backspace();
            else e.allClear();
            break;
        case Action::Negate: e.negate(); break;
        case Action::Percent: e.percent(); break;
        case Action::Function:
            e.apply(static_cast<Fn>(m_second && k.second >= 0 ? k.second : k.value));
            break;
        case Action::Constant: e.constant(static_cast<Constant>(k.value)); break;
        case Action::OpenParen: e.openParen(); break;
        case Action::CloseParen: e.closeParen(); break;
        case Action::MemoryClear: e.memoryClear(); break;
        case Action::MemoryAdd: e.memoryAdd(); break;
        case Action::MemorySubtract: e.memorySubtract(); break;
        case Action::MemoryRecall: e.memoryRecall(); break;
        case Action::Second: m_second = !m_second; break;
        case Action::RadDeg:
            setDegrees(!e.degrees());
            if (onDegreesChanged) onDegreesChanged(e.degrees());
            break;
        case Action::Mode:
            if (onToggleMode) onToggleMode();
            return;  // the keys were rebuilt
    }
    changed();
}

void CalculatorView::flash(Action action, int value) {
    for (auto& k : m_keys) {
        if (k.action != action || (k.value != value && (action == Action::Digit || action == Action::Operator ||
                                                        action == Action::Function || action == Action::Constant)))
            continue;
        k.press.set(1.0f);
        k.press.animateTo(0.0f, now(), Transition{kPressFade, Curve::EaseOut});
        break;
    }
    invalidate();
}

// --- pointer -----------------------------------------------------------------------------------------------

bool CalculatorView::onPointerDown(const PointerEvent& e) {
    if (auto* h = host(); h && !isFocused()) h->setFocus(this, false);
    if (e.button == MouseButton::Right) {
        if (displayRect().contains(e.position) && onContextMenu) onContextMenu(e.position);
        return true;
    }
    if (e.button != MouseButton::Left) return false;
    m_pressed = keyAt(e.position);
    if (m_pressed < 0) return false;
    Pad& k = m_keys[static_cast<size_t>(m_pressed)];
    k.press.set(1.0f);
    invalidate();
    return true;
}

void CalculatorView::onPointerMove(const PointerEvent& e) {
    const int over = keyAt(e.position);
    if (over != m_hovered) {
        m_hovered = over;
        invalidate();
    }
    if (m_pressed >= 0) {
        // Dragging off a key lets it go without activating, like a button.
        Pad& k = m_keys[static_cast<size_t>(m_pressed)];
        const float target = over == m_pressed ? 1.0f : 0.0f;
        if (k.press.target() != target) {
            k.press.animateTo(target, now(), Transition{0.12f, Curve::EaseOut});
            invalidate();
        }
    }
}

void CalculatorView::onPointerUp(const PointerEvent& e) {
    if (m_pressed < 0) return;
    const int index = m_pressed;
    m_pressed = -1;
    Pad& k = m_keys[static_cast<size_t>(index)];
    k.press.animateTo(0.0f, now(), Transition{kPressFade, Curve::EaseOut});
    if (keyAt(e.position) == index) activate(k);
    invalidate();
}

void CalculatorView::onPointerLeave() {
    if (m_hovered >= 0) {
        m_hovered = -1;
        invalidate();
    }
}

// --- keyboard ----------------------------------------------------------------------------------------------

bool CalculatorView::onKeyDown(const KeyEvent& e) {
    // Characters arrive as text input (any layout, numpad included); only non-character keys are here.
    if (e.primary()) {
        if (e.key == Key::C) {
            copy();
            return true;
        }
        if (e.key == Key::V) {
            paste();
            return true;
        }
        return false;  // app shortcuts
    }
    switch (e.key) {
        case Key::Enter:
            m_engine.equals();
            flash(Action::Equals);
            break;
        case Key::Backspace:
            if (m_engine.isTyping()) m_engine.backspace();
            else m_engine.allClear();
            flash(Action::Clear);
            break;
        case Key::Escape:
            m_engine.clear();
            flash(Action::Clear);
            break;
        case Key::Delete:
            m_engine.allClear();
            flash(Action::Clear);
            break;
        default: return false;
    }
    changed();
    return true;
}

bool CalculatorView::onTextInput(const TextInputEvent& e) {
    bool used = false;
    // Decode a few non-ASCII operators people may type (× ÷ − on some layouts) along with ASCII.
    std::string_view s = e.text;
    while (!s.empty()) {
        Engine& en = m_engine;
        auto starts = [&](std::string_view p) {
            if (s.substr(0, p.size()) != p) return false;
            s.remove_prefix(p.size());
            return true;
        };
        auto op = [&](Op o) {
            en.op(o);
            flash(Action::Operator, static_cast<int>(o));
        };
        if (starts("×")) op(Op::Multiply);
        else if (starts("÷")) op(Op::Divide);
        else if (starts("−")) op(Op::Subtract);
        else {
            const char c = s.front();
            s.remove_prefix(1);
            if (c >= '0' && c <= '9') {
                en.digit(c - '0');
                flash(Action::Digit, c - '0');
            } else if (c == '.' || c == ',') {
                en.point();
                flash(Action::Point);
            } else if (c == '+') op(Op::Add);
            else if (c == '-') op(Op::Subtract);
            else if (c == '*' || c == 'x' || c == 'X') op(Op::Multiply);
            else if (c == '/') op(Op::Divide);
            else if (c == '^') op(Op::Power);
            else if (c == '=') {
                en.equals();
                flash(Action::Equals);
            } else if (c == '%') {
                en.percent();
                flash(Action::Percent);
            } else if (c == '(') {
                en.openParen();
                flash(Action::OpenParen);
            } else if (c == ')') {
                en.closeParen();
                flash(Action::CloseParen);
            } else if (c == '!') {
                en.apply(Fn::Factorial);
                flash(Action::Function, static_cast<int>(Fn::Factorial));
            } else if (c == 'p') {
                en.constant(Constant::Pi);
                flash(Action::Constant, static_cast<int>(Constant::Pi));
            } else if (c == 'e') {
                en.constant(Constant::E);
                flash(Action::Constant, static_cast<int>(Constant::E));
            } else {
                continue;
            }
        }
        used = true;
    }
    if (used) changed();
    return used;
}

void CalculatorView::onFocusChanged(bool focused) {
    if (auto* h = host()) h->setTextInput(focused, displayRect());
}

void CalculatorView::copy() {
    if (m_engine.hasError()) return;
    if (auto* h = host()) h->setClipboardText(m_engine.displayText());
    // A quick blink of the number says it was copied.
    m_copyBlink.set(0.25f);
    m_copyBlink.animateTo(1.0f, now(), Transition{0.35f, Curve::EaseOut});
    invalidate();
}

void CalculatorView::paste() {
    if (auto* h = host()) {
        if (m_engine.paste(h->clipboardText())) changed();
    }
}

// --- drawing -----------------------------------------------------------------------------------------------

void CalculatorView::draw(Painter& p) {
    const double t = now();
    drawDisplay(p, t);
    for (size_t i = 0; i < m_keys.size(); ++i) drawKey(p, m_keys[i], static_cast<int>(i) == m_hovered, t);
}

void CalculatorView::drawDisplay(Painter& p, double /*t*/) {
    const Theme& th = theme();
    const auto& c = th.colors;
    const Rect d = displayRect();
    const float right = d.right() - kPad - 6;
    const float width = d.width - kPad * 2 - 12;

    // The result: big and light, shrinking to fit long numbers.
    const std::string text = m_engine.displayText();
    const float base = std::min(m_keyHeight * (m_mode == Mode::Basic ? 1.12f : 0.98f), d.height * 0.5f);
    Font font{base, FontWeight::Light, base * 1.2f};
    if (const float w = p.measureText(text, font); w > width) font = font.withSize(std::max(18.0f, base * width / w));
    const float baseline = d.bottom() - std::max(8.0f, m_keyHeight * 0.2f);
    const float w = p.measureText(text, font);
    p.drawText(text, {right - w, baseline}, font, c.label.multipliedAlpha(sample(m_copyBlink)));

    // The expression so far, above it; long ones keep their end visible.
    std::string expr = m_engine.expressionText();
    const Font small{std::clamp(m_keyHeight * 0.34f, 13.0f, 20.0f), FontWeight::Regular, 0};
    const float exprBaseline = baseline - fontMetrics(font).capHeight - small.size * 0.9f;
    if (!expr.empty()) {
        if (p.measureText(expr, small) > width) {
            const std::string ellipsis = "…";
            while (!expr.empty() && p.measureText(ellipsis + expr, small) > width) {
                size_t cut = 1;
                while (cut < expr.size() && (static_cast<unsigned char>(expr[cut]) & 0xC0) == 0x80) ++cut;  // whole UTF-8 characters
                expr.erase(0, cut);
            }
            expr = ellipsis + expr;
        }
        const float ew = p.measureText(expr, small);
        p.drawText(expr, {right - ew, exprBaseline}, small, c.secondaryLabel);
    }

    // Mode indicators on the left: radians and a value in memory.
    std::string tags;
    if (m_mode == Mode::Scientific && !m_engine.degrees()) tags = "Rad";
    if (m_engine.hasMemory()) tags += tags.empty() ? "M" : "  M";
    if (const int open = m_engine.openParens(); open > 0) tags += std::string(tags.empty() ? "" : "  ") + "( ×" + std::to_string(open);
    if (!tags.empty()) {
        const Font tag{12, FontWeight::Medium, 14};
        p.drawText(tags, {d.x + kPad + 6, exprBaseline}, tag, c.tertiaryLabel);
    }
}

void CalculatorView::drawKey(Painter& p, const Pad& k, bool hovered, double t) {
    const Theme& th = theme();
    const auto& c = th.colors;
    const bool dark = th.isDark();
    const float press = k.press.value(t);
    const float lit = k.lit.value(t);
    if (k.press.isAnimating(t) || k.lit.isAnimating(t)) requestAnimationFrame();

    Color face;
    Color ink = c.label;
    switch (k.style) {
        case Style::Digit: face = c.control; break;
        case Style::Function: face = c.label.withAlpha(dark ? 0.30f : 0.15f); break;
        case Style::Scientific: face = dark ? c.label.withAlpha(0.09f) : c.label.withAlpha(0.07f); break;
        case Style::Operator:
            face = c.orange;
            ink = Color::white();
            break;
    }
    if (k.style == Style::Operator) {
        // The pending operator turns white with an orange glyph, like macOS.
        face = face.mix(Color::white(), lit);
        ink = ink.mix(c.orange, lit);
    } else if (lit > 0) {
        face = face.mix(c.label.withAlpha(dark ? 0.55f : 0.35f), lit * 0.6f);
    }
    if (hovered) face = face.mix(dark ? Color::white(face.a) : Color::black(face.a), 0.08f);
    if (press > 0) face = face.mix(dark ? Color::white(std::max(face.a, 0.5f)) : Color::black(std::max(face.a, 0.25f)), press * 0.28f);

    const Rect r = k.rect;
    const float radius = std::min(r.width, r.height) * 0.5f;
    if (!dark && k.style == Style::Digit) p.drawShadow(r, radius, c.controlShadow, 3, {0, 0.5f});
    p.fillRoundedRect(r, radius, face);

    if (k.action == Action::Operator && k.style == Style::Operator) {
        drawGlyph(p, k, r, ink);
        return;
    }
    if (k.action == Action::Equals || k.action == Action::Negate || k.action == Action::Mode ||
        (k.action == Action::Clear && m_engine.isTyping())) {
        drawGlyph(p, k, r, ink);
        return;
    }
    const bool isSci = k.style == Style::Scientific;
    const float size = r.height * (isSci ? 0.3f : (k.style == Style::Function ? 0.36f : 0.44f));
    const Font font{size, isSci || k.style == Style::Function ? FontWeight::Medium : FontWeight::Regular, size * 1.2f};
    const Label& label = m_second && k.second >= 0 ? k.secondLabel : k.label;
    drawLabel(p, label, r, font, ink);
}

void CalculatorView::drawLabel(Painter& p, const Label& l, const Rect& r, const Font& font, const Color& color) const {
    const Font small = font.withSize(font.size * 0.62f);
    const float wPre = l.preSup.empty() ? 0 : p.measureText(l.preSup, small);
    const float wMain = p.measureText(l.main, font);
    const float wSup = l.sup.empty() ? 0 : p.measureText(l.sup, small);
    const float wSub = l.sub.empty() ? 0 : p.measureText(l.sub, small);
    const float total = wPre + wMain + std::max(wSup, wSub);
    const float cap = fontMetrics(font).capHeight;
    const float baseline = std::round((r.center().y + cap * 0.5f) * p.scale()) / p.scale();
    float x = r.center().x - total * 0.5f;
    if (wPre > 0) {
        p.drawText(l.preSup, {x, baseline - cap * 0.55f}, small, color);
        x += wPre;
    }
    p.drawText(l.main, {x, baseline}, font, color);
    x += wMain;
    if (wSup > 0) p.drawText(l.sup, {x, baseline - cap * 0.55f}, small, color);
    if (wSub > 0) p.drawText(l.sub, {x, baseline + small.size * 0.28f}, small, color);
}

void CalculatorView::drawGlyph(Painter& p, const Pad& k, const Rect& r, const Color& color) const {
    // Operators are drawn as geometry: centred exactly and crisp at any size, whatever the font.
    const Point o = r.center();
    const float s = r.height * 0.36f;  // glyph extent
    const float w = std::max(1.5f, r.height * 0.052f);
    auto hbar = [&](float y, float half) { p.fillRoundedRect({o.x - half, o.y + y - w * 0.5f, half * 2, w}, w * 0.5f, color); };
    auto plus = [&](Point at, float half) {
        p.fillRoundedRect({at.x - half, at.y - w * 0.5f, half * 2, w}, w * 0.5f, color);
        p.fillRoundedRect({at.x - w * 0.5f, at.y - half, w, half * 2}, w * 0.5f, color);
    };
    if (k.action == Action::Operator) {
        switch (static_cast<Op>(k.value)) {
            case Op::Add: plus(o, s * 0.5f); break;
            case Op::Subtract: hbar(0, s * 0.5f); break;
            case Op::Multiply: {
                const float d = s * 0.36f;
                p.drawLine({o.x - d, o.y - d}, {o.x + d, o.y + d}, color, w);
                p.drawLine({o.x - d, o.y + d}, {o.x + d, o.y - d}, color, w);
                break;
            }
            case Op::Divide:
                hbar(0, s * 0.5f);
                p.fillCircle({o.x, o.y - s * 0.3f}, w * 0.8f, color);
                p.fillCircle({o.x, o.y + s * 0.3f}, w * 0.8f, color);
                break;
            default: break;
        }
        return;
    }
    switch (k.action) {
        case Action::Equals:
            hbar(-s * 0.16f, s * 0.5f);
            hbar(s * 0.16f, s * 0.5f);
            break;
        case Action::Negate: {
            // ⁺∕₋ : a small plus, a slash and a small minus.
            const float q = s * 0.5f;
            plus({o.x - q * 0.62f, o.y - q * 0.45f}, q * 0.32f);
            p.fillRoundedRect({o.x + q * 0.62f - q * 0.32f, o.y + q * 0.45f - w * 0.5f, q * 0.64f, w}, w * 0.5f, color);
            p.drawLine({o.x + q * 0.42f, o.y - q * 0.95f}, {o.x - q * 0.42f, o.y + q * 0.95f}, color, w * 0.8f);
            break;
        }
        case Action::Clear: {
            // ⌫
            const float h = s * 0.62f;
            const float x0 = o.x - h * 0.95f;
            const float x1 = o.x + h * 0.95f;
            const Point a{x0, o.y}, b{x0 + h * 0.55f, o.y - h * 0.5f}, cc{x1, o.y - h * 0.5f}, d{x1, o.y + h * 0.5f},
                e{x0 + h * 0.55f, o.y + h * 0.5f};
            const float lw = w * 0.75f;
            p.drawLine(a, b, color, lw);
            p.drawLine(b, cc, color, lw);
            p.drawLine(cc, d, color, lw);
            p.drawLine(d, e, color, lw);
            p.drawLine(e, a, color, lw);
            const Point m{(x0 + h * 0.55f + x1) * 0.5f, o.y};
            const float xd = h * 0.2f;
            p.drawLine({m.x - xd, m.y - xd}, {m.x + xd, m.y + xd}, color, lw);
            p.drawLine({m.x - xd, m.y + xd}, {m.x + xd, m.y - xd}, color, lw);
            break;
        }
        case Action::Mode: {
            // A tiny calculator: switches between basic and scientific.
            const float bw = s * 0.62f;
            const float bh = s * 0.82f;
            const Rect body{o.x - bw * 0.5f, o.y - bh * 0.5f, bw, bh};
            p.strokeRoundedRect(body, bw * 0.18f, color, w * 0.7f);
            p.fillRoundedRect({body.x + bw * 0.2f, body.y + bh * 0.16f, bw * 0.6f, bh * 0.16f}, 1, color);
            for (int row = 0; row < 2; ++row) {
                for (int col = 0; col < 2; ++col) {
                    p.fillCircle({body.x + bw * (0.33f + 0.34f * static_cast<float>(col)), body.y + bh * (0.56f + 0.22f * static_cast<float>(row))},
                                 w * 0.45f, color);
                }
            }
            break;
        }
        default: break;
    }
}

}  // namespace calc
