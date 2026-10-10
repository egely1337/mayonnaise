#pragma once

#include <deque>

#include "../widget.hpp"

namespace mayo {

enum class ScrollAxes : std::uint8_t { Vertical, Horizontal, Both };

/// Clips a single child and scrolls it. Wheel notches glide, touchpads track 1:1 with
/// rubber-band overscroll and momentum, and overlay scrollbars fade out when idle.
class ScrollViewView : public Widget {
public:
    explicit ScrollViewView(std::shared_ptr<Widget> content);

    void setAxes(ScrollAxes axes);
    void setContent(std::shared_ptr<Widget> content);
    [[nodiscard]] Vec2 offset() const { return m_offset; }
    void scrollTo(Vec2 offset, bool animated = true);
    /// Extra space above the content (e.g. a toolbar the content slides under).
    void setContentInsetTop(float inset);

    Size measureContent(Size available) override;
    void arrangeContent(const Rect& content) override;
    [[nodiscard]] bool expandsContent(Axis /*axis*/) const override { return true; }
    void draw(Painter& painter) override;
    Widget* hitTest(Point p) override;
    bool onScroll(const ScrollEvent& e) override;
    bool onPointerDown(const PointerEvent& e) override;
    void onPointerMove(const PointerEvent& e) override;
    void onPointerUp(const PointerEvent& e) override;
    void onHoverChanged(bool hovered) override;
    /// Advances scroll physics to `now` (called every frame from draw; public for tests).
    void step(double now);
    [[nodiscard]] bool isAnimating() const { return m_phase != Phase::Idle; }

private:
    enum class Phase : std::uint8_t { Idle, Tracking, Momentum, Gliding };
    [[nodiscard]] Vec2 maxOffset() const;
    [[nodiscard]] bool scrolls(Axis axis) const;
    void place();
    void touch();  // marks activity so scrollbars show
    [[nodiscard]] Rect thumbRect(Axis axis) const;
    [[nodiscard]] Rect trackRect(Axis axis) const;

    std::shared_ptr<Widget> m_content;
    ScrollAxes m_axes = ScrollAxes::Vertical;
    Size m_contentSize;
    float m_insetTop = 0;
    Vec2 m_offset;
    Vec2 m_velocity;  // points/second
    std::deque<std::pair<double, Vec2>> m_samples;  // recent touchpad deltas, for the fling velocity
    Phase m_phase = Phase::Idle;
    double m_lastEvent = 0;
    double m_lastStep = 0;
    Animated<Vec2> m_glide;
    double m_activity = -10;
    bool m_hoveringBar = false;
    int m_dragAxis = -1;  // 0 = vertical bar, 1 = horizontal bar
    float m_dragGrab = 0;
};

class ScrollView : public WidgetHandle<ScrollView, ScrollViewView> {
public:
    explicit ScrollView(const Element& content) : WidgetHandle(std::make_shared<ScrollViewView>(content.widget())) {}
    ScrollView& axes(ScrollAxes a) { return apply([&](ScrollViewView& n) { n.setAxes(a); }); }
    void scrollTo(Vec2 offset, bool animated = true) { m_node->scrollTo(offset, animated); }
};

}  // namespace mayo
