#include "engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <random>

namespace calc {

namespace {

constexpr int kMaxEntryDigits = 15;

int precedence(Op o) {
    switch (o) {
        case Op::Add:
        case Op::Subtract: return 1;
        case Op::Multiply:
        case Op::Divide: return 2;
        case Op::Power:
        case Op::Root: return 3;
        case Op::EE: return 4;
    }
    return 0;
}

bool rightAssociative(Op o) { return o == Op::Power; }

/// Whether an operator already on the stack is applied before `next` is pushed.
bool reducesBefore(Op top, Op next) {
    return precedence(top) > precedence(next) || (precedence(top) == precedence(next) && !rightAssociative(next));
}

std::optional<double> finite(double v) {
    if (!std::isfinite(v)) return std::nullopt;
    return v == 0 ? 0.0 : v;  // no "-0" results
}

std::optional<double> applyOp(Op o, double a, double b) {
    switch (o) {
        case Op::Add: return finite(a + b);
        case Op::Subtract: return finite(a - b);
        case Op::Multiply: return finite(a * b);
        case Op::Divide:
            if (b == 0) return std::nullopt;
            return finite(a / b);
        case Op::Power: return finite(std::pow(a, b));
        case Op::Root: {
            if (b == 0) return std::nullopt;
            // Odd roots of negative numbers are real (the cube root of -8 is -2).
            if (a < 0 && std::fmod(b, 2.0) != 0 && std::trunc(b) == b) return finite(-std::pow(-a, 1.0 / b));
            return finite(std::pow(a, 1.0 / b));
        }
        case Op::EE: return finite(a * std::pow(10.0, b));
    }
    return std::nullopt;
}

bool isInteger(double v) { return std::trunc(v) == v; }

/// Wraps a composite operand in parentheses for the expression line ("(2 + 3)²", but "5²").
std::string group(const std::string& t) {
    if (t.empty()) return t;
    if (t.front() == '(' && t.back() == ')') return t;
    const bool simple = t.find(' ') == std::string::npos && t.find('-') == std::string::npos;
    return simple ? t : "(" + t + ")";
}

std::string groupDigits(std::string_view digits, char sep) {
    std::string out;
    const size_t n = digits.size();
    for (size_t i = 0; i < n; ++i) {
        out += digits[i];
        const size_t left = n - 1 - i;
        if (sep && left > 0 && left % 3 == 0) out += sep;
    }
    return out;
}

}  // namespace

std::string_view opSymbol(Op o) {
    switch (o) {
        case Op::Add: return "+";
        case Op::Subtract: return "−";
        case Op::Multiply: return "×";
        case Op::Divide: return "÷";
        case Op::Power: return "^";
        case Op::Root: return "yroot";
        case Op::EE: return "E";
    }
    return "?";
}

// --- formatting ------------------------------------------------------------------------------------------

std::string Engine::formatEntry(std::string_view entry, const NumberFormat& f) {
    std::string out;
    if (!entry.empty() && entry.front() == '-') {
        out += '-';
        entry.remove_prefix(1);
    }
    const size_t dot = entry.find('.');
    out += groupDigits(entry.substr(0, dot), f.grouping ? f.group : 0);
    if (dot != std::string_view::npos) {
        out += f.decimal;
        out += entry.substr(dot + 1);
    }
    return out;
}

std::string Engine::format(double v, const NumberFormat& f) {
    if (v == 0 || !std::isfinite(v)) return "0";
    const int digits = std::clamp(f.maxDigits, 1, 17);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*e", digits - 1, v);
    const char* e = std::strchr(buf, 'e');
    const int exponent = e ? std::atoi(e + 1) : 0;
    if (exponent >= digits || exponent < -8) {
        // Scientific: "1.5e20", trailing zeros of the mantissa dropped.
        std::string mantissa(buf, static_cast<size_t>(e - buf));
        if (mantissa.find('.') != std::string::npos) {
            while (mantissa.back() == '0') mantissa.pop_back();
            if (mantissa.back() == '.') mantissa.pop_back();
        }
        std::replace(mantissa.begin(), mantissa.end(), '.', f.decimal);
        return mantissa + "e" + std::to_string(exponent);
    }
    const int decimals = std::max(0, digits - 1 - exponent);
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string plain = buf;
    if (plain.find('.') != std::string::npos) {
        while (plain.back() == '0') plain.pop_back();
        if (plain.back() == '.') plain.pop_back();
    }
    if (plain == "-0") plain = "0";
    return formatEntry(plain, f);
}

// --- state helpers ---------------------------------------------------------------------------------------

double Engine::operand() const { return m_typing ? std::strtod(m_entry.c_str(), nullptr) : m_current; }

double Engine::value() const { return operand(); }

std::string Engine::operandText() const {
    if (m_typing) return formatEntry(m_entry, m_format);
    if (!m_currentText.empty()) return m_currentText;
    return format(m_current, m_format);
}

void Engine::setResult(double v, std::string text) {
    const auto ok = finite(v);
    if (!ok) {
        fail();
        return;
    }
    m_current = *ok;
    m_currentText = std::move(text);
    m_typing = false;
    m_awaiting = false;
    if (m_evaluated) {
        // A function of the previous result starts a new calculation with it.
        m_evaluated = false;
        m_lastExpression.clear();
    }
}

void Engine::startFresh() {
    m_tokens.clear();
    m_evaluated = false;
    m_lastExpression.clear();
}

void Engine::fail() {
    m_error = true;
    m_tokens.clear();
    m_typing = false;
    m_awaiting = false;
    m_evaluated = false;
    m_current = 0;
    m_currentText.clear();
    m_lastExpression.clear();
    m_repeatOp.reset();
}

std::optional<Op> Engine::pendingOp() const {
    if (m_awaiting && !m_tokens.empty() && m_tokens.back().kind == Token::Kind::Operator) return m_tokens.back().op;
    return std::nullopt;
}

int Engine::openParens() const {
    return static_cast<int>(std::count_if(m_tokens.begin(), m_tokens.end(),
                                          [](const Token& t) { return t.kind == Token::Kind::LParen; }));
}

std::optional<double> Engine::evaluate(const std::vector<Token>& tokens, double last, std::optional<Op> until) const {
    std::vector<double> values;
    std::vector<const Token*> ops;  // operators and open parentheses
    auto reduce = [&]() -> bool {
        const Op o = ops.back()->op;
        ops.pop_back();
        if (values.size() < 2) return false;
        const double b = values.back();
        values.pop_back();
        const auto r = applyOp(o, values.back(), b);
        if (!r) return false;
        values.back() = *r;
        return true;
    };
    for (const Token& t : tokens) {
        if (t.kind == Token::Kind::Number) {
            values.push_back(t.value);
        } else if (t.kind == Token::Kind::LParen) {
            ops.push_back(&t);
        } else {
            while (!ops.empty() && ops.back()->kind == Token::Kind::Operator && reducesBefore(ops.back()->op, t.op)) {
                if (!reduce()) return std::nullopt;
            }
            ops.push_back(&t);
        }
    }
    values.push_back(last);
    if (until) {
        // Only what pressing `until` settles: 2 + 3 × shows 3, 2 × 3 + shows 6.
        while (!ops.empty() && ops.back()->kind == Token::Kind::Operator && reducesBefore(ops.back()->op, *until)) {
            if (!reduce()) return std::nullopt;
        }
        return values.back();
    }
    while (!ops.empty()) {
        if (ops.back()->kind == Token::Kind::LParen) {
            ops.pop_back();  // unclosed parentheses close at the end
            continue;
        }
        if (!reduce()) return std::nullopt;
    }
    return values.empty() ? std::nullopt : std::optional<double>(values.back());
}

std::optional<double> Engine::applyFn(Fn f, double x) const {
    constexpr double kPi = std::numbers::pi;
    const double toRad = m_degrees ? kPi / 180.0 : 1.0;
    auto clean = [](double v) -> std::optional<double> {
        if (!std::isfinite(v)) return std::nullopt;
        return std::abs(v) < 1e-15 ? 0.0 : v;
    };
    switch (f) {
        case Fn::Square: return finite(x * x);
        case Fn::Cube: return finite(x * x * x);
        case Fn::Reciprocal:
            if (x == 0) return std::nullopt;
            return finite(1.0 / x);
        case Fn::SquareRoot:
            if (x < 0) return std::nullopt;
            return finite(std::sqrt(x));
        case Fn::CubeRoot: return finite(std::cbrt(x));
        case Fn::Exp: return finite(std::exp(x));
        case Fn::Exp10: return finite(std::pow(10.0, x));
        case Fn::Exp2: return finite(std::exp2(x));
        case Fn::Ln:
            if (x <= 0) return std::nullopt;
            return finite(std::log(x));
        case Fn::Log10:
            if (x <= 0) return std::nullopt;
            return finite(std::log10(x));
        case Fn::Log2:
            if (x <= 0) return std::nullopt;
            return finite(std::log2(x));
        case Fn::Factorial: {
            if (x < 0 && isInteger(x)) return std::nullopt;
            if (x > 170) return std::nullopt;
            const double r = std::tgamma(x + 1);
            return finite(isInteger(x) ? std::round(r) : r);
        }
        case Fn::Sin:
        case Fn::Cos:
        case Fn::Tan: {
            if (m_degrees) {
                // Exact answers at the angles people actually type (sin 180° is 0, not 1.2e-16).
                const double a = std::fmod(std::fmod(x, 360.0) + 360.0, 360.0);
                if (f == Fn::Tan && std::fmod(a, 180.0) == 90.0) return std::nullopt;
                if (std::fmod(a, 90.0) == 0) {
                    const int q = static_cast<int>(a / 90.0);
                    static constexpr double kSin[] = {0, 1, 0, -1};
                    static constexpr double kCos[] = {1, 0, -1, 0};
                    if (f == Fn::Sin) return kSin[q];
                    if (f == Fn::Cos) return kCos[q];
                    return 0.0;
                }
            }
            const double r = x * toRad;
            return clean(f == Fn::Sin ? std::sin(r) : f == Fn::Cos ? std::cos(r) : std::tan(r));
        }
        case Fn::Asin:
        case Fn::Acos:
            if (x < -1 || x > 1) return std::nullopt;
            return clean((f == Fn::Asin ? std::asin(x) : std::acos(x)) / toRad);
        case Fn::Atan: return clean(std::atan(x) / toRad);
        case Fn::Sinh: return finite(std::sinh(x));
        case Fn::Cosh: return finite(std::cosh(x));
        case Fn::Tanh: return clean(std::tanh(x));
        case Fn::Asinh: return finite(std::asinh(x));
        case Fn::Acosh:
            if (x < 1) return std::nullopt;
            return finite(std::acosh(x));
        case Fn::Atanh:
            if (x <= -1 || x >= 1) return std::nullopt;
            return finite(std::atanh(x));
    }
    return std::nullopt;
}

// --- keys ------------------------------------------------------------------------------------------------

void Engine::digit(int d) {
    if (m_error) allClear();
    if (m_evaluated) startFresh();
    if (!m_typing) {
        m_entry = "0";
        m_typing = true;
        m_awaiting = false;
        m_currentText.clear();
    }
    const auto count = std::count_if(m_entry.begin(), m_entry.end(), [](char c) { return c >= '0' && c <= '9'; });
    if (count >= kMaxEntryDigits) return;
    const char c = static_cast<char>('0' + std::clamp(d, 0, 9));
    if (m_entry == "0") m_entry = std::string(1, c);
    else if (m_entry == "-0") m_entry = std::string("-") + c;
    else m_entry += c;
}

void Engine::point() {
    if (m_error) allClear();
    if (m_evaluated) startFresh();
    if (!m_typing) {
        m_entry = "0";
        m_typing = true;
        m_awaiting = false;
        m_currentText.clear();
    }
    if (m_entry.find('.') == std::string::npos) m_entry += '.';
}

void Engine::op(Op o) {
    if (m_error) return;
    if (m_awaiting && !m_tokens.empty() && m_tokens.back().kind == Token::Kind::Operator) {
        // Changing one's mind: 2 + × means 2 ×.
        m_tokens.back().op = o;
        const std::vector<Token> prefix(m_tokens.begin(), m_tokens.end() - 2);
        if (const auto preview = evaluate(prefix, m_tokens[m_tokens.size() - 2].value, o)) m_current = *preview;
        return;
    }
    if (m_evaluated) {
        m_evaluated = false;
        m_lastExpression.clear();
    }
    const double v = operand();
    const std::string text = operandText();
    const auto preview = evaluate(m_tokens, v, o);
    if (!preview) {
        fail();
        return;
    }
    m_tokens.push_back(Token{Token::Kind::Number, v, Op::Add, text});
    m_tokens.push_back(Token{Token::Kind::Operator, 0, o, {}});
    m_current = *preview;
    m_currentText.clear();
    m_typing = false;
    m_awaiting = true;
}

void Engine::equals() {
    if (m_error) return;
    if (m_evaluated) {
        if (!m_repeatOp) return;
        const double before = m_current;
        const auto r = applyOp(*m_repeatOp, before, m_repeatOperand);
        if (!r) {
            fail();
            return;
        }
        m_lastExpression = format(before, m_format) + " " + std::string(opSymbol(*m_repeatOp)) + " " + format(m_repeatOperand, m_format);
        m_current = *r;
        m_currentText.clear();
        return;
    }
    const double last = operand();
    std::string expression = expressionText();
    if (m_awaiting && !m_tokens.empty() && m_tokens.back().kind == Token::Kind::Operator) {
        expression += " " + operandText();  // 2 + = uses the shown value: 2 + 2
    }
    for (int i = openParens(); i > 0; --i) expression += ")";
    if (!m_tokens.empty() && m_tokens.back().kind == Token::Kind::Operator) {
        m_repeatOp = m_tokens.back().op;
        m_repeatOperand = last;
    } else {
        m_repeatOp.reset();
    }
    const auto r = evaluate(m_tokens, last);
    if (!r) {
        fail();
        return;
    }
    const auto ok = finite(*r);
    if (!ok) {
        fail();
        return;
    }
    m_tokens.clear();
    m_current = *ok;
    m_currentText.clear();
    m_typing = false;
    m_awaiting = false;
    m_evaluated = true;
    m_lastExpression = expression;
}

void Engine::clear() {
    if (isTyping() || (!m_awaiting && !m_evaluated && (m_current != 0 || !m_currentText.empty()))) {
        m_entry = "0";
        m_typing = true;
        m_current = 0;
        m_currentText.clear();
        return;
    }
    allClear();
}

void Engine::allClear() {
    m_tokens.clear();
    m_entry = "0";
    m_typing = false;
    m_current = 0;
    m_currentText.clear();
    m_awaiting = false;
    m_evaluated = false;
    m_lastExpression.clear();
    m_repeatOp.reset();
    m_error = false;
}

void Engine::backspace() {
    if (m_error) {
        allClear();
        return;
    }
    if (!m_typing) return;
    m_entry.pop_back();
    if (m_entry.empty() || m_entry == "-") m_entry = "0";
}

void Engine::negate() {
    if (m_error) return;
    if (m_awaiting) {
        m_entry = "-0";  // after an operator, ± starts a negative number
        m_typing = true;
        m_awaiting = false;
        return;
    }
    if (m_typing) {
        if (m_entry.front() == '-') m_entry.erase(0, 1);
        else m_entry.insert(0, "-");
        return;
    }
    const bool evaluated = m_evaluated;
    const std::string text = m_currentText.empty() ? std::string() : "-" + group(m_currentText);
    const double v = m_current == 0 ? 0 : -m_current;
    m_current = v;
    m_currentText = text;
    if (evaluated) m_lastExpression.clear();
}

void Engine::percent() {
    if (m_error) return;
    const double v = operand();
    double r = v / 100.0;
    // 50 + 10 % is 50 + 5: a percentage of what it is added to or taken from.
    if (m_tokens.size() >= 2 && m_tokens.back().kind == Token::Kind::Operator &&
        (m_tokens.back().op == Op::Add || m_tokens.back().op == Op::Subtract)) {
        const std::vector<Token> prefix(m_tokens.begin(), m_tokens.end() - 2);
        if (const auto base = evaluate(prefix, m_tokens[m_tokens.size() - 2].value, m_tokens.back().op)) r = *base * v / 100.0;
    }
    setResult(r, group(operandText()) + "%");
}

void Engine::apply(Fn f) {
    if (m_error) return;
    const double x = operand();
    const std::string t = operandText();
    const auto r = applyFn(f, x);
    if (!r) {
        fail();
        return;
    }
    std::string text;
    switch (f) {
        case Fn::Square: text = group(t) + "²"; break;
        case Fn::Cube: text = group(t) + "³"; break;
        case Fn::Reciprocal: text = "1/" + group(t); break;
        case Fn::SquareRoot: text = "√" + group(t); break;
        case Fn::CubeRoot: text = "∛" + group(t); break;
        case Fn::Exp: text = "e^" + group(t); break;
        case Fn::Exp10: text = "10^" + group(t); break;
        case Fn::Exp2: text = "2^" + group(t); break;
        case Fn::Ln: text = "ln(" + t + ")"; break;
        case Fn::Log10: text = "log(" + t + ")"; break;
        case Fn::Log2: text = "log₂(" + t + ")"; break;
        case Fn::Factorial: text = group(t) + "!"; break;
        case Fn::Sin: text = "sin(" + t + ")"; break;
        case Fn::Cos: text = "cos(" + t + ")"; break;
        case Fn::Tan: text = "tan(" + t + ")"; break;
        case Fn::Asin: text = "asin(" + t + ")"; break;
        case Fn::Acos: text = "acos(" + t + ")"; break;
        case Fn::Atan: text = "atan(" + t + ")"; break;
        case Fn::Sinh: text = "sinh(" + t + ")"; break;
        case Fn::Cosh: text = "cosh(" + t + ")"; break;
        case Fn::Tanh: text = "tanh(" + t + ")"; break;
        case Fn::Asinh: text = "asinh(" + t + ")"; break;
        case Fn::Acosh: text = "acosh(" + t + ")"; break;
        case Fn::Atanh: text = "atanh(" + t + ")"; break;
    }
    setResult(*r, std::move(text));
}

void Engine::constant(Constant c) {
    if (m_error) allClear();
    if (m_evaluated) startFresh();
    switch (c) {
        case Constant::Pi: setResult(std::numbers::pi, "π"); break;
        case Constant::E: setResult(std::numbers::e, "e"); break;
        case Constant::Random: {
            static std::mt19937_64 rng{std::random_device{}()};
            setResult(std::uniform_real_distribution<double>(0.0, 1.0)(rng), {});
            break;
        }
    }
}

void Engine::openParen() {
    if (m_error) allClear();
    if (m_evaluated) startFresh();
    // A number right before "(" multiplies it: 2(3 + 4) = 14.
    if (!m_awaiting && (isTyping() || !m_currentText.empty() || (!m_typing && m_current != 0))) {
        m_tokens.push_back(Token{Token::Kind::Number, operand(), Op::Add, operandText()});
        m_tokens.push_back(Token{Token::Kind::Operator, 0, Op::Multiply, {}});
    }
    m_tokens.push_back(Token{Token::Kind::LParen, 0, Op::Add, {}});
    m_current = 0;
    m_currentText.clear();
    m_typing = false;
    m_awaiting = true;
}

void Engine::closeParen() {
    if (m_error || openParens() == 0) return;
    const double last = operand();
    const std::string lastText = operandText();
    size_t open = m_tokens.size();
    while (open > 0 && m_tokens[open - 1].kind != Token::Kind::LParen) --open;
    const std::vector<Token> inner(m_tokens.begin() + static_cast<std::ptrdiff_t>(open), m_tokens.end());
    std::string text = "(";
    for (const Token& t : inner) text += t.kind == Token::Kind::Operator ? " " + std::string(opSymbol(t.op)) + " " : t.text;
    text += lastText + ")";
    const auto r = evaluate(inner, last);
    if (!r) {
        fail();
        return;
    }
    m_tokens.erase(m_tokens.begin() + static_cast<std::ptrdiff_t>(open) - 1, m_tokens.end());
    setResult(*r, std::move(text));
}

void Engine::commitEntry() {
    // Like "=" for the memory keys: the next digit starts a new number instead of extending this one.
    if (!m_typing) return;
    m_current = operand();
    m_currentText.clear();
    m_typing = false;
}

void Engine::memoryAdd() {
    if (m_error) return;
    m_memory = m_memory.value_or(0) + operand();
    commitEntry();
}

void Engine::memorySubtract() {
    if (m_error) return;
    m_memory = m_memory.value_or(0) - operand();
    commitEntry();
}

void Engine::memoryRecall() {
    if (!m_memory) return;
    if (m_error) allClear();
    if (m_evaluated) startFresh();
    setResult(*m_memory, {});
}

bool Engine::paste(std::string_view text) {
    // Accept the display's own format (grouping, locale decimal) as well as plain "1234.5" / "1e-3".
    std::string clean;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\'' || c == '_') continue;
        if (m_format.grouping && c == m_format.group && m_format.group != m_format.decimal) continue;
        clean += c == m_format.decimal ? '.' : c;
    }
    if (clean.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(clean.c_str(), &end);
    if (end != clean.c_str() + clean.size() || !std::isfinite(v)) return false;
    if (m_error) allClear();
    if (m_evaluated) startFresh();
    setResult(v, {});
    return true;
}

// --- display ---------------------------------------------------------------------------------------------

std::string Engine::displayText() const {
    if (m_error) return "Error";
    if (m_typing) return formatEntry(m_entry, m_format);
    return format(m_current, m_format);
}

std::string Engine::expressionText() const {
    if (m_error) return {};
    if (m_evaluated) return m_lastExpression;
    std::string out;
    for (const Token& t : m_tokens) {
        if (t.kind == Token::Kind::Number) out += t.text;
        else if (t.kind == Token::Kind::LParen) out += "(";
        else out += " " + std::string(opSymbol(t.op)) + " ";
    }
    // A lone number being typed is already the display; the line above shows expressions.
    if (!m_awaiting && (!m_tokens.empty() || !m_currentText.empty())) out += operandText();
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

}  // namespace calc
