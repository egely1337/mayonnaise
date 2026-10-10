#include <doctest/doctest.h>

#include "test_support.hpp"

using namespace mayo;
using test::Probe;

namespace {
struct Fixture {
    test::TestHost host;
    std::shared_ptr<Probe> a = std::make_shared<Probe>(Size{100, 50});
    std::shared_ptr<Probe> b = std::make_shared<Probe>(Size{100, 50});
    std::shared_ptr<StackView> root;
    Fixture() {
        auto stack = VStack({a, b}).spacing(0);
        root = stack.node();
        host.attach(root);
    }
    EventDispatcher& d() { return host.dispatcher; }
};
PointerEvent at(float x, float y) { return PointerEvent{{x, y}, MouseButton::Left, 1, Modifiers::None}; }
}  // namespace

TEST_CASE("Hit testing finds the deepest widget") {
    Fixture f;
    CHECK(f.d().hitTest({10, 10}) == f.a.get());
    CHECK(f.d().hitTest({10, 60}) == f.b.get());
    CHECK(f.d().hitTest({150, 10}) == f.root.get());
    CHECK(f.d().hitTest({900, 900}) == nullptr);
}

TEST_CASE("Hover enter and leave follow the pointer") {
    Fixture f;
    f.d().pointerMove(at(10, 10));
    CHECK(f.a->isHovered());
    CHECK(f.root->isHovered());
    CHECK(f.a->enters == 1);
    f.d().pointerMove(at(10, 60));
    CHECK_FALSE(f.a->isHovered());
    CHECK(f.a->leaves == 1);
    CHECK(f.b->isHovered());
    CHECK(f.root->isHovered());
    f.d().pointerLeave();
    CHECK_FALSE(f.b->isHovered());
    CHECK_FALSE(f.root->isHovered());
}

TEST_CASE("Pointer capture keeps delivering to the pressed widget") {
    Fixture f;
    f.d().pointerMove(at(10, 10));
    f.d().pointerDown(at(10, 10));
    CHECK(f.a->downs == 1);
    CHECK(f.a->isPressed());
    CHECK(f.d().captured() == f.a.get());
    f.d().pointerMove(at(10, 60));  // over b, but captured by a
    CHECK(f.a->moves >= 1);
    CHECK(f.b->moves == 0);
    CHECK_FALSE(f.a->isPressed());  // pressed only while inside
    f.d().pointerUp(at(10, 60));
    CHECK(f.a->ups == 1);
    CHECK(f.d().captured() == nullptr);
    CHECK(f.b->isHovered());
}

TEST_CASE("Disabled widgets don't receive presses") {
    Fixture f;
    f.a->setDisabled(true);
    f.d().pointerDown(at(10, 10));
    CHECK(f.a->downs == 0);
}

TEST_CASE("Tab cycles focus through focusable widgets") {
    Fixture f;
    f.a->focusable = true;
    f.b->focusable = true;
    f.d().keyDown(KeyEvent{Key::Tab});  // root has no key handler; dispatcher handles Tab
    CHECK(f.d().focused() == f.a.get());
    CHECK(f.a->focused);
    CHECK(f.d().focusVisible());
    f.d().focusNext(false);
    CHECK(f.d().focused() == f.b.get());
    CHECK_FALSE(f.a->focused);
    f.d().focusNext(false);
    CHECK(f.d().focused() == f.a.get());
    f.d().focusNext(true);
    CHECK(f.d().focused() == f.b.get());
}

TEST_CASE("Click focuses only focusOnClick widgets") {
    Fixture f;
    f.a->focusable = true;
    f.b->focusable = true;
    f.b->clickFocus = true;
    f.d().pointerDown(at(10, 10));
    f.d().pointerUp(at(10, 10));
    CHECK(f.d().focused() == nullptr);
    f.d().pointerDown(at(10, 60));
    CHECK(f.d().focused() == f.b.get());
    CHECK_FALSE(f.d().focusVisible());
}

TEST_CASE("Keys go to the focused widget; scroll bubbles from the hit widget") {
    Fixture f;
    f.b->focusable = true;
    f.d().setFocus(f.b.get(), true);
    f.d().keyDown(KeyEvent{Key::A});
    CHECK(f.b->keys == 1);
    CHECK(f.a->keys == 0);
    f.d().scroll(ScrollEvent{{10, 10}, {0, 5}, true, Modifiers::None});
    CHECK(f.a->scrolls == 1);
}

TEST_CASE("Overlays are modal and dismiss on outside click") {
    Fixture f;
    auto overlay = std::make_shared<Probe>(Size{50, 50});
    f.host.showOverlay(overlay);
    overlay->arrange({200, 200, 50, 50});
    int dismissed = 0;
    f.d().onOverlayDismissed = [&](Widget*) { ++dismissed; };
    f.d().pointerDown(at(210, 210));
    CHECK(overlay->downs == 1);
    f.d().pointerUp(at(210, 210));
    f.d().pointerDown(at(10, 10));  // outside: dismiss, don't deliver
    CHECK(dismissed == 1);
    CHECK(f.a->downs == 0);
    CHECK(f.d().overlays().empty());
}

TEST_CASE("Removed widgets are dropped from hover/focus state") {
    Fixture f;
    f.b->focusable = true;
    f.d().setFocus(f.b.get(), false);
    f.d().pointerMove(at(10, 60));
    f.root->removeChild(f.b.get());
    CHECK(f.d().focused() == nullptr);
    CHECK(f.d().hovered() == nullptr);
}

TEST_CASE("Animated values interpolate on the provided clock") {
    Animated<float> v(0.0f);
    v.animateTo(10.0f, 1.0, Transition{1.0f, Curve::Linear});
    CHECK(v.value(1.0) == doctest::Approx(0));
    CHECK(v.value(1.5) == doctest::Approx(5));
    CHECK(v.isAnimating(1.5));
    CHECK(v.value(2.5) == doctest::Approx(10));
    CHECK_FALSE(v.isAnimating(2.5));
    v.animateTo(0.0f, 3.0, Transition::spring());
    CHECK(v.value(3.0 + 0.35) == doctest::Approx(0));
    CHECK(applyCurve(Curve::EaseOut, 0.5f) > 0.5f);
}
