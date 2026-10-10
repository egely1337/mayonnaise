#include "mayonnaise/widgets/toolbar.hpp"

#include "mayonnaise/app.hpp"
#include "mayonnaise/painter.hpp"

namespace mayo {

ToolbarView::ToolbarView() : StackView(Axis::Horizontal) {
    setDragRegion(true);
    setSpacing(10);
    setAlignment(Alignment::Leading);
}

ToolbarGroupView::ToolbarGroupView() : StackView(Axis::Horizontal) {
    setSpacing(0);
    setAlignment(Alignment::Center);
    setPadding(Insets(3, 4));
}

void ToolbarGroupView::draw(Painter& p) {
    const Theme& t = theme();
    Rect r = frame();
    float radius = r.height * 0.5f;
    p.drawShadow(r, radius, t.colors.shadow.multipliedAlpha(t.isDark() ? 0.6f : 0.35f), 10, {0, 2});
    p.drawMaterial(r, radius, Material::Glass);
    paintChildren(p);
}

}  // namespace mayo
