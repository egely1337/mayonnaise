#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "../actions.hpp"
#include "menu.hpp"

namespace mayo {

/// Adapt a named application action to the existing MenuView/MenuButtonView API.
/// The registry must outlive any returned menu item. Recreate menu items when state changes.
[[nodiscard]] inline std::optional<MenuItem> menuItemForAction(ActionRegistry& registry, std::string_view id) {
    auto current = registry.describe(id);
    if (!current) return std::nullopt;
    std::string name(id);
    MenuItem item(current->title, [&registry, name = std::move(name)] { registry.activate(name); });
    item.shortcut = current->shortcut;
    item.enabled = current->enabled;
    item.checked = current->checked;
    return item;
}

}  // namespace mayo
