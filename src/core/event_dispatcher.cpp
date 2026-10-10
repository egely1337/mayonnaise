#include "event_dispatcher.hpp"

#include <algorithm>

namespace mayo {

namespace {
std::shared_ptr<Widget> lock(Widget* w) { return w ? w->shared_from_this() : nullptr; }
}  // namespace

bool EventDispatcher::attached(const Widget* w) const {
    if (!w) return false;
    if (m_root && m_root->isAncestorOf(w)) return true;
    return std::any_of(m_overlays.begin(), m_overlays.end(), [&](const auto& o) { return o->isAncestorOf(w); });
}

void EventDispatcher::pushOverlay(std::shared_ptr<Widget> overlay) {
    m_overlays.push_back(std::move(overlay));
    refreshHover();
}

void EventDispatcher::removeOverlay(Widget* overlay) {
    auto it = std::find_if(m_overlays.begin(), m_overlays.end(), [&](const auto& o) { return o.get() == overlay; });
    if (it == m_overlays.end()) return;
    auto keep = *it;  // keep alive through callbacks
    m_overlays.erase(it);
    if (auto f = focused(); f && overlay->isAncestorOf(f)) setFocus(nullptr, false);
    refreshHover();
}

Widget* EventDispatcher::hitTest(Point p) const {
    if (!m_overlays.empty()) {
        // Modal: only the topmost overlay is interactive.
        return m_overlays.back()->hitTest(p);
    }
    return m_root ? m_root->hitTest(p) : nullptr;
}

Widget* EventDispatcher::hovered() const {
    if (m_hoverPath.empty()) return nullptr;
    auto w = m_hoverPath.back().lock();
    return w && attached(w.get()) ? w.get() : nullptr;
}

Widget* EventDispatcher::captured() const {
    auto w = m_captured.lock();
    return w && attached(w.get()) ? w.get() : nullptr;
}

Widget* EventDispatcher::focused() const {
    auto w = m_focused.lock();
    return w && attached(w.get()) ? w.get() : nullptr;
}

CursorShape EventDispatcher::cursor() const {
    if (Widget* c = captured()) return c->cursor();
    if (Widget* h = hovered()) return h->isEnabled() ? h->cursor() : CursorShape::Arrow;
    return CursorShape::Arrow;
}

void EventDispatcher::updateHover(Widget* target) {
    std::vector<Widget*> path;
    for (Widget* w = target; w; w = w->parent()) path.push_back(w);
    std::reverse(path.begin(), path.end());

    // Leave widgets no longer on the path (deepest first), then enter new ones (outermost first).
    std::vector<std::shared_ptr<Widget>> old;
    for (auto& weak : m_hoverPath) {
        if (auto w = weak.lock()) old.push_back(std::move(w));
    }
    for (auto it = old.rbegin(); it != old.rend(); ++it) {
        if (std::find(path.begin(), path.end(), it->get()) == path.end()) {
            (*it)->setHoveredState(false);
            (*it)->onPointerLeave();
        }
    }
    for (Widget* w : path) {
        bool wasHovered = std::any_of(old.begin(), old.end(), [&](const auto& o) { return o.get() == w; });
        if (!wasHovered) {
            w->setHoveredState(true);
            w->onPointerEnter();
        }
    }
    m_hoverPath.clear();
    for (Widget* w : path) m_hoverPath.push_back(w->weak_from_this());
}

void EventDispatcher::refreshHover() {
    if (captured()) return;
    updateHover(m_pointerInside ? hitTest(m_lastPointer) : nullptr);
}

void EventDispatcher::pointerMove(const PointerEvent& e) {
    m_lastPointer = e.position;
    m_pointerInside = true;
    if (auto c = lock(captured())) {
        c->setPressedState(c->frame().contains(e.position));
        c->onPointerMove(e);
        return;
    }
    updateHover(hitTest(e.position));
    for (auto& weak : m_hoverPath) {
        if (auto w = weak.lock(); w && w->isEnabled()) w->onPointerMove(e);
    }
}

void EventDispatcher::pointerDown(const PointerEvent& e) {
    m_lastPointer = e.position;
    m_pointerInside = true;
    if (!m_overlays.empty() && !m_overlays.back()->frame().contains(e.position)) {
        // Clicking outside a menu/popover dismisses it and is not delivered (macOS behaviour).
        auto top = m_overlays.back();
        if (onOverlayDismissed) onOverlayDismissed(top.get());
        removeOverlay(top.get());
        return;
    }
    updateHover(hitTest(e.position));
    Widget* target = hovered();
    if (!target) return;

    for (Widget* w = target; w; w = w->parent()) {
        if (w->acceptsFocus() && w->focusOnClick() && w->isEnabled()) {
            setFocus(w, false);
            break;
        }
    }
    for (Widget* w = target; w; w = w->parent()) {
        if (!w->isEnabled()) return;
        if (w->onPointerDown(e)) {
            auto keep = lock(w);
            m_captured = keep;
            keep->setPressedState(true);
            return;
        }
    }
}

void EventDispatcher::pointerUp(const PointerEvent& e) {
    m_lastPointer = e.position;
    if (auto c = lock(captured())) {
        m_captured.reset();
        c->setPressedState(false);
        c->onPointerUp(e);
    }
    m_captured.reset();
    refreshHover();
}

void EventDispatcher::pointerLeave() {
    m_pointerInside = false;
    if (captured()) return;
    updateHover(nullptr);
}

void EventDispatcher::scroll(const ScrollEvent& e) {
    m_lastPointer = e.position;
    for (Widget* w = hitTest(e.position); w; w = w->parent()) {
        if (w->isEnabled() && w->onScroll(e)) break;
    }
    refreshHover();
}

void EventDispatcher::magnify(const MagnifyEvent& e) {
    for (Widget* w = hitTest(e.position); w; w = w->parent()) {
        if (w->isEnabled() && w->onMagnify(e)) break;
    }
}

bool EventDispatcher::keyDown(const KeyEvent& e) {
    auto f = lock(focused());
    Widget* start = f ? f.get() : (!m_overlays.empty() ? m_overlays.back().get() : m_root.get());
    for (Widget* w = start; w; w = w->parent()) {
        if (w->isEnabled() && w->onKeyDown(e)) return true;
    }
    if (e.key == Key::Tab && !e.primary() && !e.alt()) {
        focusNext(e.shift());
        return true;
    }
    if (m_overlays.empty() && onUnhandledKey && onUnhandledKey(e)) return true;
    if (e.key == Key::Escape && !m_overlays.empty()) {
        auto top = m_overlays.back();
        if (onOverlayDismissed) onOverlayDismissed(top.get());
        removeOverlay(top.get());
        return true;
    }
    return false;
}

void EventDispatcher::textInput(const TextInputEvent& e) {
    if (auto f = lock(focused())) f->onTextInput(e);
}

void EventDispatcher::setFocus(Widget* widget, bool visible) {
    auto old = lock(focused());
    m_focusVisible = widget && visible;
    if (old.get() == widget) {
        if (widget) widget->invalidate();
        return;
    }
    m_focused = widget ? widget->weak_from_this() : std::weak_ptr<Widget>{};
    if (old) {
        old->onFocusChanged(false);
        old->invalidate();
    }
    if (widget) {
        widget->onFocusChanged(true);
        widget->invalidate();
    }
}

void EventDispatcher::collectFocusable(Widget* w, std::vector<Widget*>& out) const {
    if (!w->isVisible() || !w->isEnabled()) return;
    if (w->acceptsFocus()) out.push_back(w);
    for (auto& c : w->children()) collectFocusable(c.get(), out);
}

void EventDispatcher::focusNext(bool backwards) {
    std::vector<Widget*> order;
    Widget* scope = !m_overlays.empty() ? m_overlays.back().get() : m_root.get();
    if (!scope) return;
    collectFocusable(scope, order);
    if (order.empty()) return;
    Widget* cur = focused();
    auto it = std::find(order.begin(), order.end(), cur);
    size_t index = 0;
    if (it == order.end()) {
        index = backwards ? order.size() - 1 : 0;
    } else {
        auto pos = static_cast<size_t>(it - order.begin());
        index = backwards ? (pos + order.size() - 1) % order.size() : (pos + 1) % order.size();
    }
    setFocus(order[index], true);
}

}  // namespace mayo
