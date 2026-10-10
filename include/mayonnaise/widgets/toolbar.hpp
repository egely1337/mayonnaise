#pragma once

#include "../layout.hpp"

namespace mayo {

/// Row of toolbar items living in the window's unified title bar. Empty space stays draggable.
class ToolbarView : public StackView {
public:
    ToolbarView();
    [[nodiscard]] bool expandsContent(Axis axis) const override { return axis == Axis::Horizontal; }
};

/// Items grouped on one Liquid Glass capsule (macOS 26 toolbar style).
class ToolbarGroupView : public StackView {
public:
    ToolbarGroupView();
    void draw(Painter& painter) override;
};

class Toolbar : public detail::ContainerHandle<Toolbar, ToolbarView> {
public:
    Toolbar(std::initializer_list<Element> items = {})  // NOLINT(google-explicit-constructor)
        : ContainerHandle(std::make_shared<ToolbarView>()) {
        addAll(items);
    }
    Toolbar& spacing(float s) { return apply([&](ToolbarView& n) { n.setSpacing(s); }); }
};

class ToolbarGroup : public detail::ContainerHandle<ToolbarGroup, ToolbarGroupView> {
public:
    ToolbarGroup(std::initializer_list<Element> items = {})  // NOLINT(google-explicit-constructor)
        : ContainerHandle(std::make_shared<ToolbarGroupView>()) {
        addAll(items);
    }
};

}  // namespace mayo
