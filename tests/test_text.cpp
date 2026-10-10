#include <doctest/doctest.h>

#include "core/utf8.hpp"
#include "render/font.hpp"

using namespace mayo;

TEST_CASE("UTF-8 helpers") {
    std::string s = "aé€😀";
    size_t i = 0;
    CHECK(utf8::next(s, i) == U'a');
    CHECK(utf8::next(s, i) == U'é');
    CHECK(utf8::next(s, i) == U'€');
    CHECK(utf8::next(s, i) == U'😀');
    CHECK(i == s.size());
    CHECK(utf8::prevBoundary(s, s.size()) == s.size() - 4);
    CHECK(utf8::nextBoundary(s, 1) == 3);
    std::string out;
    utf8::append(out, U'€');
    CHECK(out == "€");
    std::string bad = "\xff";
    i = 0;
    CHECK(utf8::next(bad, i) == 0xFFFD);
    CHECK(i == 1);
}

TEST_CASE("Text measurement scales with size and weight") {
    Font body{13, FontWeight::Regular, 16};
    float w = render::measureText("Hello", body);
    CHECK(w > 20);
    CHECK(w < 50);
    CHECK(render::measureText("Hello", body.withSize(26)) > w * 1.8f);
    CHECK(render::measureText("Hello", body.withWeight(FontWeight::Bold)) > w);
    CHECK(render::measureText("", body) == 0);
}

TEST_CASE("Metrics are sane") {
    auto m = render::FontLibrary::instance().metrics({13, FontWeight::Regular, 16});
    CHECK(m.ascender > m.capHeight);
    CHECK(m.capHeight > m.xHeight);
    CHECK(m.descender > 0);
}

TEST_CASE("Word wrapping") {
    Font body{13, FontWeight::Regular, 16};
    std::string text = "one two three four five six seven";
    auto single = render::wrapText(text, body, 0);
    REQUIRE(single.size() == 1);
    float full = single[0].width;
    auto lines = render::wrapText(text, body, full / 2.5f);
    CHECK(lines.size() >= 3);
    for (const auto& l : lines) CHECK(l.width <= full / 2.5f + 0.01f);
    // Lines cover the text without the break spaces.
    CHECK(lines.front().begin == 0);
    CHECK(lines.back().end == text.size());

    auto hard = render::wrapText("a\nb\n", body, 0);
    CHECK(hard.size() == 3);

    auto longWord = render::wrapText("abcdefghijklmnopqrstuvwxyz", body, 40);
    CHECK(longWord.size() > 1);
}

TEST_CASE("Caret positions") {
    Font body{13, FontWeight::Regular, 16};
    std::vector<size_t> b;
    std::vector<float> x;
    render::caretPositions("aé", body, b, x);
    REQUIRE(b.size() == 3);
    CHECK(b[1] == 1);
    CHECK(b[2] == 3);
    CHECK(x[0] == 0);
    CHECK(x[2] > x[1]);
}
