#include <doctest/doctest.h>

#include <mayonnaise/color.hpp>
#include <mayonnaise/geometry.hpp>

using namespace mayo;

TEST_CASE("Rect basics") {
    Rect r{10, 20, 100, 50};
    CHECK(r.right() == 110);
    CHECK(r.bottom() == 70);
    CHECK(r.contains({10, 20}));
    CHECK_FALSE(r.contains({110, 20}));
    CHECK(r.center() == Point{60, 45});
}

TEST_CASE("Rect inset and intersection") {
    Rect r{0, 0, 100, 100};
    CHECK(r.inset(Insets(10, 20)) == Rect{20, 10, 60, 80});
    CHECK(r.inset(80) == Rect{80, 80, 0, 0});
    CHECK(r.intersected({50, 50, 100, 100}) == Rect{50, 50, 50, 50});
    CHECK(r.intersected({200, 200, 10, 10}).isEmpty());
    CHECK(r.united({50, 50, 100, 100}) == Rect{0, 0, 150, 150});
    CHECK(r.intersects({99, 99, 5, 5}));
    CHECK_FALSE(r.intersects({100, 0, 5, 5}));
}

TEST_CASE("Vec2 math") {
    Vec2 a{3, 4};
    CHECK(a.length() == doctest::Approx(5));
    CHECK(a + Vec2{1, 1} == Vec2{4, 5});
    CHECK(lerp(Vec2{0, 0}, Vec2{10, 20}, 0.5f) == Vec2{5, 10});
}

TEST_CASE("CornerRadii") {
    CornerRadii r(4);
    CHECK(r.topLeft == 4);
    CHECK(r.bottomLeft == 4);
    CHECK(CornerRadii{}.isZero());
    CHECK(CornerRadii(1, 7, 3, 2).max() == 7);
}

TEST_CASE("Color parsing") {
    auto c = Color::parse("#FF8000");
    REQUIRE(c);
    CHECK(c->r == doctest::Approx(1.0));
    CHECK(c->g == doctest::Approx(128.0 / 255.0));
    CHECK(c->b == doctest::Approx(0.0));
    CHECK(c->a == doctest::Approx(1.0));
    auto s = Color::parse("0f08");
    CHECK_FALSE(s);
    auto shortForm = Color::parse("#0f0");
    REQUIRE(shortForm);
    CHECK(shortForm->g == doctest::Approx(1.0));
    auto withAlpha = Color::parse("#00000080");
    REQUIRE(withAlpha);
    CHECK(withAlpha->a == doctest::Approx(128.0 / 255.0));
    CHECK_FALSE(Color::parse("#zzzzzz"));
}

TEST_CASE("Color helpers") {
    CHECK(Color::hex(0xFFFFFF).luminance() == doctest::Approx(1.0));
    CHECK(Color::hex(0x000000).luminance() == doctest::Approx(0.0));
    CHECK(Color::white().mix(Color::black(), 0.5f).r == doctest::Approx(0.5));
    CHECK(Color::hex(0x123456).toRGBA8() == 0x123456FFu);
    CHECK(Color::black().lighter(1.0f) == Color::white());
}
