#include "mayonnaise/widgets/scroll_view.hpp"

#include <algorithm>
#include <cmath>

#include "mayonnaise/app.hpp"
#include "mayonnaise/painter.hpp"

namespace mayo {

namespace {
constexpr double kGestureEnd = 0.07;   // seconds without touchpad events = fingers lifted
constexpr float kDecay = 2.2f;         // momentum velocity decay per second (macOS "normal" rate)
constexpr float kSpringK = 170.0f;     // rubber-band spring stiffness
constexpr double kBarLinger = 0.9;     // scrollbars stay visible after activity
constexpr double kBarFade = 0.25;

float component(Vec2 v, int i) { return i == 0 ? v.x : v.y; }
void setComponent(Vec2& v, int i, float value) { (i == 0 ? v.x : v.y) = value; }
}  // namespace

ScrollViewView::ScrollViewView(std::shared_ptr<Widget> content) { setContent(std::move(content)); }

void ScrollViewView::setContent(std::shared_ptr<Widget> content) {
    if (m_content) removeChild(m_content.get());
    m_content = std::move(content);
    if (m_content) addChild(m_content);
}

void ScrollViewView::setAxes(ScrollAxes axes) {
    m_axes = axes;
    invalidateLayout();
}

void ScrollViewView::setContentInsetTop(float inset) {
    if (inset == m_insetTop) return;
    m_insetTop = inset;
    invalidateLayout();
}

bool ScrollViewView::scrolls(Axis axis) const {
    if (m_axes == ScrollAxes::Both) return true;
    return axis == Axis::Vertical ? m_axes == ScrollAxes::Vertical : m_axes == ScrollAxes::Horizontal;
}

Vec2 ScrollViewView::maxOffset() const {
    Rect v = contentRect();
    return {scrolls(Axis::Horizontal) ? std::max(0.0f, m_contentSize.width - v.width) : 0.0f,
            scrolls(Axis::Vertical) ? std::max(0.0f, m_contentSize.height + m_insetTop - v.height) : 0.0f};
}

Size ScrollViewView::measureContent(Size available) {
    if (!m_content) return {};
    Size proposal{scrolls(Axis::Horizontal) ? kInfinity : available.width,
                  scrolls(Axis::Vertical) ? kInfinity : available.height};
    Size s = m_content->measure(proposal);
    return {std::isfinite(available.width) ? available.width : s.width,
            std::isfinite(available.height) ? available.height : s.height};
}

void ScrollViewView::arrangeContent(const Rect& viewport) {
    if (!m_content) return;
    Size proposal{scrolls(Axis::Horizontal) ? kInfinity : viewport.width,
                  scrolls(Axis::Vertical) ? kInfinity : viewport.height};
    Size s = m_content->measure(proposal);
    // Content is at least as large as the viewport so expanding content (spacers) still fills it.
    m_contentSize = {std::max(s.width, viewport.width), std::max(s.height, viewport.height - m_insetTop)};
    if (m_phase == Phase::Idle) {
        Vec2 mx = maxOffset();
        m_offset = {std::clamp(m_offset.x, 0.0f, mx.x), std::clamp(m_offset.y, 0.0f, mx.y)};
    }
    place();
}

void ScrollViewView::place() {
    if (!m_content) return;
    Rect v = contentRect();
    m_content->arrange({v.x - m_offset.x, v.y + m_insetTop - m_offset.y, m_contentSize.width, m_contentSize.height});
}

void ScrollViewView::touch() {
    m_activity = now();
    if (auto* h = host()) h->scheduleFrame(m_activity + kBarLinger);
    invalidate();
}

void ScrollViewView::scrollTo(Vec2 offset, bool animated) {
    Vec2 mx = maxOffset();
    Vec2 target{std::clamp(offset.x, 0.0f, mx.x), std::clamp(offset.y, 0.0f, mx.y)};
    if (animated) {
        m_glide.set(m_offset);
        m_glide.animateTo(target, now(), Transition{0.3f, Curve::EaseInOut});
        m_phase = Phase::Gliding;
    } else {
        m_offset = target;
        m_phase = Phase::Idle;
        place();
    }
    touch();
}

bool ScrollViewView::onScroll(const ScrollEvent& e) {
    Vec2 delta = e.delta;
    if (!scrolls(Axis::Horizontal)) delta.x = 0;
    if (!scrolls(Axis::Vertical)) delta.y = 0;
    Vec2 mx = maxOffset();
    // Let the parent scroll if we can't move in the requested direction (scroll chaining).
    auto canMove = [&](int i) {
        float d = component(delta, i);
        float o = component(m_offset, i);
        float m = component(mx, i);
        return (d < 0 && o > 0) || (d > 0 && o < m) || (e.precise && m > 0 && d != 0);
    };
    if (!canMove(0) && !canMove(1)) return false;

    const double t = now();
    if (!e.precise) {
        Vec2 base = m_phase == Phase::Gliding ? m_glide.target() : m_offset;
        Vec2 target{std::clamp(base.x + delta.x, 0.0f, mx.x), std::clamp(base.y + delta.y, 0.0f, mx.y)};
        m_glide.set(m_offset);
        m_glide.animateTo(target, t, Transition{0.2f, Curve::EaseOut});
        m_phase = Phase::Gliding;
        m_velocity = {};
    } else {
        double dt = std::clamp(t - m_lastEvent, 0.004, 0.05);
        if (m_phase != Phase::Tracking) m_velocity = {};
        for (int i = 0; i < 2; ++i) {
            float d = component(delta, i);
            float o = component(m_offset, i);
            float m = component(mx, i);
            float over = o < 0 ? -o : (o > m ? o - m : 0.0f);
            // Rubber band: movement past the edge meets increasing resistance.
            if ((o <= 0 && d < 0) || (o >= m && d > 0)) d *= 0.45f / (1.0f + over / 40.0f);
            setComponent(m_offset, i, o + d);
            float v = component(m_velocity, i);
            setComponent(m_velocity, i, v + (d / static_cast<float>(dt) - v) * 0.5f);
        }
        m_phase = Phase::Tracking;
        m_lastEvent = t;
        if (auto* h = host()) h->scheduleFrame(t + kGestureEnd + 0.01);
        place();
    }
    m_lastStep = t;
    touch();
    return true;
}

void ScrollViewView::step(double t) {
    if (m_phase == Phase::Idle) return;
    float dt = std::clamp(static_cast<float>(t - m_lastStep), 0.0f, 0.05f);
    m_lastStep = t;
    Vec2 mx = maxOffset();

    if (m_phase == Phase::Gliding) {
        m_offset = m_glide.value(t);
        if (!m_glide.isAnimating(t)) m_phase = Phase::Idle;
        place();
        requestAnimationFrame();
        return;
    }
    if (m_phase == Phase::Tracking) {
        if (t - m_lastEvent < kGestureEnd) return;
        m_phase = Phase::Momentum;
        if (m_velocity.length() < 60) m_velocity = {};
    }

    bool active = false;
    for (int i = 0; i < 2; ++i) {
        float o = component(m_offset, i);
        float v = component(m_velocity, i);
        float m = component(mx, i);
        float bound = o < 0 ? 0.0f : (o > m ? m : o);
        float x = o - bound;  // overscroll distance
        if (x != 0) {
            // Critically damped spring back to the edge.
            float a = -kSpringK * x - 2.0f * std::sqrt(kSpringK) * v;
            v += a * dt;
        } else {
            v *= std::exp(-kDecay * dt);
        }
        o += v * dt;
        if (std::abs(v) < 4 && std::abs(o - std::clamp(o, 0.0f, m)) < 0.5f) {
            o = std::clamp(o, 0.0f, m);
            v = 0;
        } else {
            active = true;
        }
        setComponent(m_offset, i, o);
        setComponent(m_velocity, i, v);
    }
    if (!active) m_phase = Phase::Idle;
    place();
    touch();
    requestAnimationFrame();
}

Rect ScrollViewView::trackRect(Axis axis) const {
    Rect v = contentRect();
    const float w = m_hoveringBar || m_dragAxis >= 0 ? 11.0f : theme().metrics.scrollbarWidth + 2;
    if (axis == Axis::Vertical) return {v.right() - w - 1, v.y + m_insetTop + 2, w, v.height - m_insetTop - 4};
    return {v.x + 2, v.bottom() - w - 1, v.width - 4, w};
}

Rect ScrollViewView::thumbRect(Axis axis) const {
    Rect track = trackRect(axis);
    Rect v = contentRect();
    const int i = axis == Axis::Horizontal ? 0 : 1;
    float viewLen = i == 0 ? v.width : v.height - m_insetTop;
    float contentLen = i == 0 ? m_contentSize.width : m_contentSize.height;
    float trackLen = i == 0 ? track.width : track.height;
    float m = component(maxOffset(), i);
    float o = component(m_offset, i);
    float over = o < 0 ? -o : (o > m ? o - m : 0.0f);
    float len = std::max(20.0f, trackLen * viewLen / std::max(contentLen, 1.0f) - over * 0.5f);
    len = std::min(len, trackLen);
    float frac = m > 0 ? std::clamp(o / m, 0.0f, 1.0f) : 0.0f;
    float pos = (trackLen - len) * frac;
    Rect thumb = i == 0 ? Rect{track.x + pos, track.y, len, track.height} : Rect{track.x, track.y + pos, track.width, len};
    return thumb.inset(2.0f);
}

void ScrollViewView::draw(Painter& p) {
    const double t = now();
    step(t);
    Rect v = contentRect();
    p.pushClip(v, cornerRadius().max());
    paintChildren(p);
    p.popClip();

    // Overlay scrollbars.
    double idle = t - m_activity;
    float alpha = m_hoveringBar || m_dragAxis >= 0
                      ? 1.0f
                      : static_cast<float>(std::clamp(1.0 - (idle - kBarLinger) / kBarFade, 0.0, 1.0));
    if (idle > kBarLinger && alpha > 0) requestAnimationFrame();
    if (alpha <= 0) return;
    const Theme& th = theme();
    Color thumbColor = th.isDark() ? Color::white(0.45f) : Color::black(0.38f);
    Vec2 mx = maxOffset();
    for (int i = 0; i < 2; ++i) {
        Axis axis = i == 0 ? Axis::Horizontal : Axis::Vertical;
        if (!scrolls(axis) || component(mx, i) <= 0) continue;
        if (m_hoveringBar || m_dragAxis == i) {
            Rect track = trackRect(axis);
            p.fillRoundedRect(track, track.width * 0.5f, th.colors.tertiaryFill.multipliedAlpha(alpha));
        }
        Rect thumb = thumbRect(axis);
        float r = std::min(thumb.width, thumb.height) * 0.5f;
        p.fillRoundedRect(thumb, r, thumbColor.multipliedAlpha(alpha));
    }
}

Widget* ScrollViewView::hitTest(Point pt) {
    if (!isVisible() || !frame().contains(pt)) return nullptr;
    Vec2 mx = maxOffset();
    if (mx.y > 0 && scrolls(Axis::Vertical) && trackRect(Axis::Vertical).outset(2).contains(pt)) return this;
    if (mx.x > 0 && scrolls(Axis::Horizontal) && trackRect(Axis::Horizontal).outset(2).contains(pt)) return this;
    return Widget::hitTest(pt);
}

bool ScrollViewView::onPointerDown(const PointerEvent& e) {
    for (int i = 0; i < 2; ++i) {
        Axis axis = i == 0 ? Axis::Horizontal : Axis::Vertical;
        if (!scrolls(axis) || component(maxOffset(), i) <= 0) continue;
        Rect track = trackRect(axis);
        if (!track.outset(2).contains(e.position)) continue;
        Rect thumb = thumbRect(axis);
        float pos = component(e.position, i);
        float start = i == 0 ? thumb.x : thumb.y;
        float len = i == 0 ? thumb.width : thumb.height;
        m_dragAxis = i;
        m_dragGrab = (pos >= start && pos <= start + len) ? pos - start : len * 0.5f;
        m_phase = Phase::Idle;
        onPointerMove(e);
        return true;
    }
    return false;
}

void ScrollViewView::onPointerMove(const PointerEvent& e) {
    if (m_dragAxis >= 0) {
        const int i = m_dragAxis;
        Axis axis = i == 0 ? Axis::Horizontal : Axis::Vertical;
        Rect track = trackRect(axis);
        Rect thumb = thumbRect(axis);
        float trackStart = i == 0 ? track.x : track.y;
        float trackLen = i == 0 ? track.width : track.height;
        float len = (i == 0 ? thumb.width : thumb.height) + 4;
        float frac = (component(e.position, i) - m_dragGrab - trackStart) / std::max(1.0f, trackLen - len);
        setComponent(m_offset, i, std::clamp(frac, 0.0f, 1.0f) * component(maxOffset(), i));
        place();
        touch();
        return;
    }
    bool over = false;
    Vec2 mx = maxOffset();
    if (mx.y > 0 && scrolls(Axis::Vertical)) over |= trackRect(Axis::Vertical).outset(2).contains(e.position);
    if (mx.x > 0 && scrolls(Axis::Horizontal)) over |= trackRect(Axis::Horizontal).outset(2).contains(e.position);
    if (over != m_hoveringBar) {
        m_hoveringBar = over;
        touch();
    }
}

void ScrollViewView::onPointerUp(const PointerEvent& /*e*/) {
    m_dragAxis = -1;
    touch();
}

void ScrollViewView::onHoverChanged(bool hovered) {
    if (!hovered && m_hoveringBar) {
        m_hoveringBar = false;
        invalidate();
    }
}

}  // namespace mayo
