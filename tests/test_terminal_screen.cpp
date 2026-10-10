#include <doctest/doctest.h>

#include <string>

#include "screen.hpp"

// The Finder and other examples are UI; the terminal's emulation core is plain logic and tested here.
using namespace term;

namespace {
std::string row(const Screen& s, int y) {
    std::string o;
    for (const auto& c : s.line(y).cells) {
        if (c.attrs() & kWideSpacer) continue;
        const char32_t cp = c.codepoint();
        if (cp < 0x80)
            o.push_back(static_cast<char>(cp));
        else
            o += "?";
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o;
}
}  // namespace

TEST_CASE("terminal: text and line feeds") {
    Screen s(10, 3);
    s.feed("hello\r\nworld");
    CHECK(row(s, 0) == "hello");
    CHECK(row(s, 1) == "world");
    CHECK((s.cursorX() == 5 && s.cursorY() == 1));
}

TEST_CASE("terminal: autowrap marks soft-wrapped lines") {
    Screen s(5, 2);
    s.feed("abcdefg");
    CHECK(row(s, 0) == "abcde");
    CHECK(row(s, 1) == "fg");
    CHECK(s.line(0).wrapped);
}

TEST_CASE("terminal: the last column waits before wrapping") {
    Screen s(5, 2);
    s.feed("abcde");
    CHECK(s.cursorX() == 4);
    s.feed("\r\n");
    CHECK(s.cursorY() == 1);
    CHECK((!s.line(0).wrapped));
}

TEST_CASE("terminal: lines leaving the screen go to the trimmed scrollback") {
    Screen s(10, 2);
    s.feed("1\r\n2\r\n3\r\n4");
    CHECK(s.scrollbackSize() == 2);
    CHECK(row(s, 0) == "3");
    CHECK(s.scrollbackLine(0).cells.size() == 1);
}

TEST_CASE("terminal: cursor addressing and SGR bold/colour") {
    Screen s(10, 3);
    s.feed("\x1b[2;3Hx");
    CHECK(row(s, 1) == "  x");
    s.feed("\x1b[1;31mR\x1b[0m");
    CHECK(s.line(1).cells[3].fg == paletteColor(1));
    CHECK((s.line(1).cells[3].attrs() & kBold));
}

TEST_CASE("terminal: 256-colour and true colour, ';' and ':' forms") {
    Screen s(10, 1);
    s.feed("\x1b[38;2;10;20;30mA\x1b[48:2::1:2:3mB\x1b[38;5;200mC");
    CHECK(s.line(0).cells[0].fg == rgbColor(10, 20, 30));
    CHECK(s.line(0).cells[1].bg == rgbColor(1, 2, 3));
    CHECK(s.line(0).cells[2].fg == paletteColor(200));
}

TEST_CASE("terminal: erase in line and display") {
    Screen s(10, 2);
    s.feed("abcdef\x1b[1;3H\x1b[K");
    CHECK(row(s, 0) == "ab");
    s.feed("\x1b[2J");
    CHECK(row(s, 0) == "");
}

TEST_CASE("terminal: replies to DSR, DA and DECRQM") {
    Screen s(10, 2);
    std::string out;
    s.respond = [&](const std::string& r) { out += r; };
    s.feed("\x1b[2;4H\x1b[6n");
    CHECK(out == "\x1b[2;4R");
    out.clear();
    s.feed("\x1b[c");
    CHECK(out == "\x1b[?62;22c");
    out.clear();
    s.feed("\x1b[?2004$p");
    CHECK(out == "\x1b[?2004;2$y");
}

TEST_CASE("terminal: the alternate screen restores the main screen and cursor") {
    Screen s(10, 3);
    s.feed("main\x1b[?1049h");
    CHECK(s.altScreen());
    CHECK(row(s, 0) == "");
    s.feed("alt");
    s.feed("\x1b[?1049l");
    CHECK((!s.altScreen()));
    CHECK(row(s, 0) == "main");
    CHECK(s.cursorX() == 4);
}

TEST_CASE("terminal: DEC special graphics line drawing") {
    Screen s(10, 1);
    s.feed("\x1b(0qqx\x1b(Bq");
    CHECK(s.line(0).cells[0].codepoint() == 0x2500);
    CHECK(s.line(0).cells[2].codepoint() == 0x2502);
    CHECK(s.line(0).cells[3].codepoint() == 'q');
}

TEST_CASE("terminal: wide characters take two cells") {
    Screen s(6, 1);
    s.feed(
        "a\xe4\xb8\xad"
        "b");
    CHECK(s.line(0).cells[1].codepoint() == 0x4E2D);
    CHECK((s.line(0).cells[1].attrs() & kWide));
    CHECK((s.line(0).cells[2].attrs() & kWideSpacer));
    CHECK(s.line(0).cells[3].codepoint() == 'b');
    CHECK(s.cursorX() == 4);
}

TEST_CASE("terminal: OSC title and working directory") {
    Screen s(10, 3);
    s.feed("\x1b]0;my title\x07");
    CHECK(s.title() == "my title");
    s.feed("\x1b]2;other\x1b\\");
    CHECK(s.title() == "other");
    s.feed("\x1b]7;file://host/home/x%20y\x07");
    CHECK(s.workingDirectory() == "/home/x y");
}

TEST_CASE("terminal: delete line inside a scroll region") {
    Screen s(10, 5);
    s.feed("1\r\n2\r\n3\r\n4\r\n5");
    s.feed("\x1b[2;4r\x1b[2;1H\x1b[M");
    CHECK(row(s, 1) == "3");
    CHECK(row(s, 3) == "");
    CHECK(row(s, 4) == "5");
    CHECK(s.scrollbackSize() == 0);
}

TEST_CASE("terminal: delete and insert characters") {
    Screen s(10, 3);
    s.feed("abc\x1b[1;2H\x1b[2P");
    CHECK(row(s, 0) == "a");
    s.feed("\x1b[1;1H\x1b[4hX\x1b[4l");
    CHECK(row(s, 0) == "Xa");
}

TEST_CASE("terminal: reflow re-wraps the scrollback on width changes") {
    Screen s(10, 3);
    s.feed("0123456789AB\r\nnext");
    CHECK(row(s, 0) == "0123456789");
    CHECK(row(s, 1) == "AB");
    s.resize(20, 3);
    CHECK(row(s, 0) == "0123456789AB");
    CHECK(row(s, 1) == "next");
    CHECK((s.cursorY() == 1 && s.cursorX() == 4));
    s.resize(5, 3);
    CHECK((s.scrollbackSize() + 3 >= 4));
    CHECK(row(s, 2) == "next");
    CHECK((s.cursorY() == 2 && s.cursorX() == 4));
    s.resize(20, 3);
    CHECK(row(s, 0) == "0123456789AB");
    CHECK(s.scrollbackSize() == 0);
}

TEST_CASE("terminal: shrinking rows moves lines to the scrollback and back") {
    Screen s(10, 5);
    s.feed("a\r\nb\r\nc\r\nd");
    s.resize(10, 2);
    CHECK(row(s, 0) == "c");
    CHECK(row(s, 1) == "d");
    CHECK(s.scrollbackSize() == 2);
    s.resize(10, 5);
    CHECK(row(s, 0) == "a");
    CHECK(s.cursorY() == 3);
}

TEST_CASE("terminal: cursor, key, and mouse modes") {
    Screen s(10, 2);
    s.feed("\x1b[?25l\x1b[5 q\x1b[?1h\x1b[?1002h\x1b[?1006h");
    CHECK((!s.cursorVisible()));
    CHECK(s.cursorShape() == CursorShape::Bar);
    CHECK(s.appCursorKeys());
    CHECK(s.mouseMode() == MouseMode::Drag);
    CHECK(s.sgrMouse());
}

TEST_CASE("terminal: REP repeats the last character") {
    Screen s(10, 2);
    s.feed("x\x1b[5b");
    CHECK(row(s, 0) == "xxxxxx");
}

TEST_CASE("terminal: a wide character at the last column wraps") {
    Screen s(4, 2);
    s.feed("ab\xe4\xb8\xad\xe4\xb8\xad");
    CHECK(row(s, 0) == "ab?");
    CHECK(s.line(0).wrapped);
    CHECK(row(s, 1) == "?");
}

TEST_CASE("terminal: OSC 52 sets the clipboard") {
    Screen s(10, 2);
    std::string clip;
    s.onClipboard = [&](const std::string& c) { clip = c; };
    s.feed("\x1b]52;c;aGVsbG8=\x07");
    CHECK(clip == "hello");
}

TEST_CASE("terminal: erasing uses the current background colour") {
    Screen s(10, 2);
    s.feed("\x1b[31m\x1b[2J");
    CHECK(s.line(0).cells[0].bg == kDefaultColor);
    s.feed("\x1b[41m\x1b[K");
    CHECK(s.line(0).cells[5].bg == paletteColor(1));
}
