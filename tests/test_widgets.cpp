#include <doctest/doctest.h>

#include <mayonnaise/mayonnaise.hpp>

#include "test_support.hpp"

using namespace mayo;

namespace {
struct ScrollFixture {
    test::TestHost host;
    std::shared_ptr<test::Box> content = std::make_shared<test::Box>(Size{300, 2000});
    std::shared_ptr<ScrollViewView> scroll = std::make_shared<ScrollViewView>(content);
    ScrollFixture() { host.attach(scroll, {300, 400}); }
    void run(double seconds) {
        for (double t = 0; t < seconds; t += 1.0 / 60.0) {
            host.clock += 1.0 / 60.0;
            scroll->step(host.clock);
        }
    }
    ScrollEvent precise(float dy) { return ScrollEvent{{10, 10}, {0, dy}, true, Modifiers::None}; }
};
}  // namespace

TEST_CASE("ScrollView: wheel notches glide to a clamped target") {
    ScrollFixture f;
    CHECK(f.scroll->onScroll(ScrollEvent{{10, 10}, {0, 120}, false, Modifiers::None}));
    f.run(0.5);
    CHECK(f.scroll->offset().y == doctest::Approx(120));
    CHECK(f.content->frame().y == doctest::Approx(-120));
    f.scroll->onScroll(ScrollEvent{{10, 10}, {0, -1000}, false, Modifiers::None});
    f.run(0.5);
    CHECK(f.scroll->offset().y == doctest::Approx(0));
    // Already at the top: further upward scrolling is passed to the parent.
    CHECK_FALSE(f.scroll->onScroll(ScrollEvent{{10, 10}, {0, -40}, false, Modifiers::None}));
}

TEST_CASE("ScrollView: touchpad momentum continues after the gesture and stops") {
    ScrollFixture f;
    for (int i = 0; i < 6; ++i) {
        f.host.clock += 0.016;
        f.scroll->onScroll(f.precise(20));
    }
    float afterGesture = f.scroll->offset().y;
    CHECK(afterGesture == doctest::Approx(120));
    f.run(3.0);
    CHECK(f.scroll->offset().y > afterGesture + 50);  // coasted
    CHECK_FALSE(f.scroll->isAnimating());
    CHECK(f.scroll->offset().y <= 1600.0f);
}

TEST_CASE("ScrollView: rubber-band overscroll springs back") {
    ScrollFixture f;
    for (int i = 0; i < 5; ++i) {
        f.host.clock += 0.016;
        f.scroll->onScroll(f.precise(-30));
    }
    CHECK(f.scroll->offset().y < 0);
    CHECK(f.scroll->offset().y > -60);  // resistance applied
    f.run(2.0);
    CHECK(f.scroll->offset().y == doctest::Approx(0).epsilon(0.01));
    CHECK_FALSE(f.scroll->isAnimating());
}

TEST_CASE("TextField editing") {
    test::TestHost host;
    auto tf = std::make_shared<TextFieldView>("placeholder");
    host.attach(tf, {300, 24});
    tf->insert("hello world");
    CHECK(tf->text() == "hello world");
    tf->deleteBackward(true);
    CHECK(tf->text() == "hello ");
    tf->deleteBackward(false);
    CHECK(tf->text() == "hello");
    tf->moveCaret(0, false);
    tf->insert("> ");
    CHECK(tf->text() == "> hello");
    tf->selectAll();
    tf->insert("é");
    CHECK(tf->text() == "é");
    tf->deleteBackward(false);  // removes a 2-byte code point
    CHECK(tf->text().empty());

    tf->insert("one\ntwo");  // newlines are stripped in single-line fields
    CHECK(tf->text() == "onetwo");

    // Clipboard through the host.
    host.dispatcher.setFocus(tf.get(), false);
    tf->selectAll();
    CHECK(tf->onKeyDown(KeyEvent{Key::C, Modifiers::Ctrl}));
    CHECK(host.clip == "onetwo");
    tf->moveCaret(tf->text().size(), false);
    tf->onKeyDown(KeyEvent{Key::V, Modifiers::Ctrl});
    CHECK(tf->text() == "onetwoonetwo");
    tf->onKeyDown(KeyEvent{Key::Left, Modifiers::Shift});
    CHECK(tf->selectionAnchor() == 12);
    CHECK(tf->caret() == 11);
}

TEST_CASE("Secure fields never copy") {
    test::TestHost host;
    auto tf = std::make_shared<TextFieldView>("");
    host.attach(tf, {300, 24});
    tf->setSecure(true);
    tf->insert("hunter2");
    tf->selectAll();
    tf->onKeyDown(KeyEvent{Key::C, Modifiers::Ctrl});
    CHECK(host.clip.empty());
}

TEST_CASE("Toggle, checkbox, slider and segmented control state") {
    test::TestHost host;
    bool last = false;
    auto toggle = Toggle("x").onChange([&](bool on) { last = on; });
    host.attach(toggle.node(), {200, 30});
    toggle->onKeyDown(KeyEvent{Key::Space});
    CHECK(toggle.on());
    CHECK(last);

    float value = 0;
    auto slider = Slider(0.5f).step(0.1f).onChange([&](float v) { value = v; });
    host.attach(slider.node(), {200, 30});
    slider->onKeyDown(KeyEvent{Key::Right});
    CHECK(slider.value() == doctest::Approx(0.6f));
    CHECK(value == doctest::Approx(0.6f));
    slider.setValue(5.0f);
    CHECK(slider.value() == doctest::Approx(1.0f));

    int seg = -1;
    auto sc = SegmentedControl({"a", "b", "c"}).onChange([&](int i) { seg = i; });
    host.attach(sc.node(), {300, 30});
    sc->onKeyDown(KeyEvent{Key::Right});
    CHECK(seg == 1);
    sc->onKeyDown(KeyEvent{Key::Left});
    sc->onKeyDown(KeyEvent{Key::Left});
    CHECK(sc.selectedIndex() == 0);
}

TEST_CASE("Label wraps to the proposed width") {
    auto label = Label("A fairly long sentence that will need to wrap onto several lines here");
    Size one = label.node()->measure({kInfinity, kInfinity});
    Size wrapped = label.node()->measure({120, kInfinity});
    CHECK(wrapped.width <= 120);
    CHECK(wrapped.height > one.height * 2);
    auto single = Label("A fairly long sentence").lineLimit(1);
    CHECK(single.node()->measure({50, kInfinity}).height == doctest::Approx(one.height));
}

TEST_CASE("Theme tokens differ between appearances") {
    Theme light = Theme::make(Appearance::Light);
    Theme dark = Theme::make(Appearance::Dark);
    CHECK(light.colors.windowBackground.luminance() > 0.8f);
    CHECK(dark.colors.windowBackground.luminance() < 0.1f);
    CHECK(light.colors.label.a > 0.5f);
    CHECK(light.font(TextStyle::LargeTitle).size > light.font(TextStyle::Body).size);
    CHECK(light.font(TextStyle::Headline).weight == FontWeight::Semibold);
    Theme custom = Theme::make(Appearance::Light, Color::hex(0xFF0000));
    CHECK(custom.colors.accent == Color::hex(0xFF0000));
    CHECK(custom.colors.focusRing.a < 1.0f);
}
