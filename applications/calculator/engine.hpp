#pragma once

// The calculator's arithmetic: keys in, display text out. No UI dependencies, so it is tested directly
// (tests/test_calculator_engine.cpp).
//
// Unlike a pocket calculator it keeps the whole expression and honours precedence and parentheses
// (2 + 3 × 4 = 14), like macOS Calculator: the display shows what is being typed, or the part of the
// expression that is already decided, and the line above it the expression so far.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace calc {

enum class Op : std::uint8_t { Add, Subtract, Multiply, Divide, Power, Root, EE };

enum class Fn : std::uint8_t {
    Square, Cube, Reciprocal, SquareRoot, CubeRoot,
    Exp, Exp10, Exp2, Ln, Log10, Log2,
    Factorial,
    Sin, Cos, Tan, Asin, Acos, Atan,
    Sinh, Cosh, Tanh, Asinh, Acosh, Atanh,
};

enum class Constant : std::uint8_t { Pi, E, Random };

/// How numbers are written: the user's locale decides the separators (1.234,5 in Turkish).
struct NumberFormat {
    char decimal = '.';
    char group = ',';
    bool grouping = true;
    int maxDigits = 12;  ///< significant digits of computed results
};

class Engine {
public:
    // Keys.
    void digit(int d);
    void point();
    void op(Op o);
    void equals();
    /// Clears the number being typed, or everything (AC) when there is none.
    void clear();
    void allClear();
    void backspace();
    void negate();
    void percent();
    void apply(Fn f);
    void constant(Constant c);
    void openParen();
    void closeParen();
    void memoryClear() { m_memory.reset(); }
    void memoryAdd();
    void memorySubtract();
    void memoryRecall();
    /// Enters a pasted number (in the current format, or plain C notation); false if the text isn't one.
    bool paste(std::string_view text);

    void setDegrees(bool degrees) { m_degrees = degrees; }
    [[nodiscard]] bool degrees() const { return m_degrees; }
    void setFormat(const NumberFormat& format) { m_format = format; }
    [[nodiscard]] const NumberFormat& numberFormat() const { return m_format; }

    // Display.
    [[nodiscard]] double value() const;
    [[nodiscard]] std::string displayText() const;
    /// The expression above the display ("12 × (3 +"), or the evaluated one after "=" ("2 + 3 × 4").
    [[nodiscard]] std::string expressionText() const;
    [[nodiscard]] bool hasError() const { return m_error; }
    /// True while a number is being typed: the clear key deletes digits (⌫) instead of clearing all (AC).
    [[nodiscard]] bool isTyping() const { return m_typing && m_entry != "0" && m_entry != "-0"; }
    [[nodiscard]] bool hasMemory() const { return m_memory.has_value(); }
    /// The operator just pressed, while the next operand hasn't been started (its key stays lit).
    [[nodiscard]] std::optional<Op> pendingOp() const;
    [[nodiscard]] int openParens() const;

    static std::string format(double v, const NumberFormat& format);
    /// A number as typed ("1234." → "1,234.").
    static std::string formatEntry(std::string_view entry, const NumberFormat& format);

private:
    struct Token {
        enum class Kind : std::uint8_t { Number, Operator, LParen } kind = Kind::Number;
        double value = 0;
        Op op = Op::Add;
        std::string text;  // how the operand reads in the expression line (number, "sin(30)", "(2 + 3)")
    };

    [[nodiscard]] double operand() const;  // the value an operator or function would take now
    [[nodiscard]] std::string operandText() const;
    void setResult(double v, std::string text);
    void startFresh();  // after "=" a digit starts a new calculation
    void commitEntry();
    void fail();
    /// Shunting-yard over `tokens` followed by `last`; with `until`, only reduces what an operator of that
    /// precedence would (the partial result shown after pressing it).
    [[nodiscard]] std::optional<double> evaluate(const std::vector<Token>& tokens, double last,
                                                 std::optional<Op> until = std::nullopt) const;
    [[nodiscard]] std::optional<double> applyFn(Fn f, double x) const;

    std::vector<Token> m_tokens;  // complete prefix; always waiting for an operand
    std::string m_entry = "0";     // digits being typed ('.' decimal, leading '-' when negated)
    bool m_typing = false;
    double m_current = 0;          // operand when not typing (a function result, constant, partial result)
    std::string m_currentText;     // its expression text; empty = format the number
    bool m_awaiting = false;       // just pressed an operator or "(": m_current is only a preview
    bool m_evaluated = false;      // just pressed "="
    std::string m_lastExpression;  // shown above the result after "="
    std::optional<Op> m_repeatOp;  // "=" again repeats the last operation (2 + 3 = = → 8)
    double m_repeatOperand = 0;
    bool m_error = false;
    bool m_degrees = true;
    std::optional<double> m_memory;
    NumberFormat m_format;
};

[[nodiscard]] std::string_view opSymbol(Op o);

}  // namespace calc
