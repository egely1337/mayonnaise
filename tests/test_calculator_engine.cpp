#include <doctest/doctest.h>

#include <string>

#include "engine.hpp"

using namespace calc;

namespace {
/// Types keys like a user: digits, ".", + - * / ^, ( ), = and % ; 'n' is ±, 'c' clear, '<' backspace.
void keys(Engine& e, std::string_view s) {
    for (char k : s) {
        if (k >= '0' && k <= '9') e.digit(k - '0');
        else if (k == '.') e.point();
        else if (k == '+') e.op(Op::Add);
        else if (k == '-') e.op(Op::Subtract);
        else if (k == '*') e.op(Op::Multiply);
        else if (k == '/') e.op(Op::Divide);
        else if (k == '^') e.op(Op::Power);
        else if (k == '(') e.openParen();
        else if (k == ')') e.closeParen();
        else if (k == '=') e.equals();
        else if (k == '%') e.percent();
        else if (k == 'n') e.negate();
        else if (k == 'c') e.clear();
        else if (k == '<') e.backspace();
    }
}

std::string run(std::string_view s) {
    Engine e;
    keys(e, s);
    return e.displayText();
}
}  // namespace

TEST_CASE("Calculator: precedence, parentheses and partial results") {
    CHECK(run("2+3*4=") == "14");
    CHECK(run("2*3+4=") == "10");
    CHECK(run("(2+3)*4=") == "20");
    CHECK(run("2(3+4)=") == "14");      // implicit multiplication
    CHECK(run("2^3^2=") == "512");      // power is right associative
    CHECK(run("10-4-3=") == "3");       // the rest is left associative
    CHECK(run("2+3*") == "3");          // only 3 × … is pending
    CHECK(run("2*3+") == "6");          // 2 × 3 is settled
    CHECK(run("(1+2") == "2");
    CHECK(run("(1+2)") == "3");
    CHECK(run("(1+2*(3+4") == "4");
    CHECK(run("(1+2*(3+4=") == "15");  // open parentheses close at "="
}

TEST_CASE("Calculator: editing keys") {
    Engine e;
    keys(e, "12+*");  // changed mind: ×
    CHECK(e.pendingOp() == Op::Multiply);
    keys(e, "3=");
    CHECK(e.displayText() == "36");
    CHECK(e.expressionText() == "12 × 3");
    keys(e, "=");  // repeats × 3
    CHECK(e.displayText() == "108");

    CHECK(run("123<") == "12");
    CHECK(run("5n") == "-5");
    CHECK(run("5+n3=") == "2");
    CHECK(run("1.5.2") == "1.52");  // a second point is ignored
    CHECK(run("7c") == "0");
    Engine f;
    keys(f, "7+8c");
    CHECK(f.displayText() == "0");
    keys(f, "2=");
    CHECK(f.displayText() == "9");  // C only cleared the 8
}

TEST_CASE("Calculator: percent like macOS") {
    CHECK(run("50+10%=") == "55");
    CHECK(run("50-10%=") == "45");
    CHECK(run("50*10%=") == "5");
    CHECK(run("25%") == "0.25");
}

TEST_CASE("Calculator: errors and recovery") {
    Engine e;
    keys(e, "5/0=");
    CHECK(e.hasError());
    CHECK(e.displayText() == "Error");
    keys(e, "3+4=");
    CHECK_FALSE(e.hasError());
    CHECK(e.displayText() == "7");
    e.apply(Fn::Reciprocal);  // 1/7 fine
    CHECK_FALSE(e.hasError());
    keys(e, "0");
    e.apply(Fn::Reciprocal);
    CHECK(e.hasError());
}

TEST_CASE("Calculator: scientific functions") {
    Engine e;
    keys(e, "30");
    e.apply(Fn::Sin);
    CHECK(e.displayText() == "0.5");
    keys(e, "180");
    e.apply(Fn::Sin);
    CHECK(e.displayText() == "0");  // exact, not 1.2e-16
    keys(e, "90");
    e.apply(Fn::Tan);
    CHECK(e.hasError());
    e.allClear();
    e.setDegrees(false);
    e.constant(Constant::Pi);
    e.apply(Fn::Cos);
    CHECK(e.displayText() == "-1");
    keys(e, "5");
    e.apply(Fn::Factorial);
    CHECK(e.displayText() == "120");
    keys(e, "2");
    e.apply(Fn::Square);
    keys(e, "+");
    CHECK(e.expressionText() == "2² +");
    keys(e, "1=");
    CHECK(e.displayText() == "5");
    keys(e, "8n");
    e.apply(Fn::CubeRoot);
    CHECK(e.displayText() == "-2");
}

TEST_CASE("Calculator: memory") {
    Engine e;
    keys(e, "5");
    e.memoryAdd();
    keys(e, "3");
    e.memoryAdd();
    e.allClear();
    e.memoryRecall();
    CHECK(e.displayText() == "8");
    CHECK(e.hasMemory());
    e.memoryClear();
    CHECK_FALSE(e.hasMemory());
}

TEST_CASE("Calculator: number formatting follows the locale") {
    NumberFormat tr{',', '.', true, 12};
    CHECK(Engine::format(1234567.5, tr) == "1.234.567,5");
    CHECK(Engine::format(0.1 + 0.2, NumberFormat{}) == "0.3");
    CHECK(Engine::format(2.0 / 3.0, NumberFormat{}) == "0.666666666667");
    CHECK(Engine::format(1e20, NumberFormat{}) == "1e20");
    CHECK(Engine::format(-1.5e-12, NumberFormat{}) == "-1.5e-12");
    CHECK(Engine::format(123456789012.0, NumberFormat{}) == "123,456,789,012");
    CHECK(Engine::formatEntry("-1234.50", tr) == "-1.234,50");
    CHECK(Engine::formatEntry("12.", NumberFormat{}) == "12.");

    Engine e;
    e.setFormat(tr);
    CHECK(e.paste("1.234,5"));
    CHECK(e.displayText() == "1.234,5");
    CHECK(e.paste("2500,25"));
    CHECK(e.value() == doctest::Approx(2500.25));
    CHECK_FALSE(e.paste("abc"));
    Engine plain;
    CHECK(plain.paste(" 2.5e3\n"));
    CHECK(plain.value() == doctest::Approx(2500));
}
