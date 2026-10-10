#pragma once

#include "mayonnaise/geometry.hpp"

namespace mayo {

/// Mixin for overlay widgets that position themselves (menus, popovers) and want to know when the
/// window dismissed them (outside click, Escape).
class PopupHost {
public:
    virtual ~PopupHost() = default;
    [[nodiscard]] virtual Rect placePopup(Size window, Size measured) = 0;
    virtual void popupDismissed() {}
};

}  // namespace mayo
