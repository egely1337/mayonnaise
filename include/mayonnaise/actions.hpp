#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "events.hpp"

namespace mayo {

/// A keyboard shortcut expressed as actual key/modifier values, not display text.
struct ActionShortcut {
    Key key = Key::Unknown;
    Modifiers modifiers = Modifiers::None;

    [[nodiscard]] bool matches(const KeyEvent& event) const {
        return key != Key::Unknown && key == event.key && modifiers == event.modifiers;
    }
    [[nodiscard]] bool operator==(const ActionShortcut&) const = default;
};

/// Generate a menu label from the same shortcut that is used for key dispatch.
[[nodiscard]] inline std::string actionShortcutLabel(ActionShortcut shortcut) {
    if (shortcut.key == Key::Unknown) return {};
    std::string label;
    if (hasModifier(shortcut.modifiers, Modifiers::Ctrl)) label += "Ctrl+";
    if (hasModifier(shortcut.modifiers, Modifiers::Alt)) label += "Alt+";
    if (hasModifier(shortcut.modifiers, Modifiers::Shift)) label += "Shift+";
    if (hasModifier(shortcut.modifiers, Modifiers::Super)) label += "Super+";

    const auto code = static_cast<int>(shortcut.key);
    if (shortcut.key >= Key::A && shortcut.key <= Key::Z)
        return label + static_cast<char>('A' + code - static_cast<int>(Key::A));
    if (shortcut.key >= Key::Num0 && shortcut.key <= Key::Num9)
        return label + static_cast<char>('0' + code - static_cast<int>(Key::Num0));
    if (shortcut.key >= Key::F1 && shortcut.key <= Key::F12)
        return label + "F" + std::to_string(code - static_cast<int>(Key::F1) + 1);

    switch (shortcut.key) {
        case Key::Enter: return label + "Enter";
        case Key::Escape: return label + "Esc";
        case Key::Backspace: return label + "Backspace";
        case Key::Tab: return label + "Tab";
        case Key::Space: return label + "Space";
        case Key::Delete: return label + "Delete";
        case Key::Insert: return label + "Insert";
        case Key::Left: return label + "Left";
        case Key::Right: return label + "Right";
        case Key::Up: return label + "Up";
        case Key::Down: return label + "Down";
        case Key::Home: return label + "Home";
        case Key::End: return label + "End";
        case Key::PageUp: return label + "PageUp";
        case Key::PageDown: return label + "PageDown";
        case Key::Comma: return label + ",";
        case Key::Period: return label + ".";
        case Key::Minus: return label + "-";
        case Key::Equals: return label + "=";
        case Key::Slash: return label + "/";
        default: return {};
    }
}

/// Owned by the application: the registry never sends executable callbacks to another process.
struct ActionDefinition {
    std::string title;
    std::function<void()> execute;
    std::optional<ActionShortcut> shortcut;
    std::function<bool()> enabled;                ///< Empty means enabled.
    std::function<bool()> checked;                ///< Empty means unchecked.
    std::function<std::string()> dynamicTitle;    ///< Optional state-dependent label.

    ActionDefinition(std::string name, std::function<void()> handler,
                     std::optional<ActionShortcut> binding = {})
        : title(std::move(name)), execute(std::move(handler)), shortcut(binding) {}
};

/// A read-only value snapshot suitable for menu presentation and (later) IPC serialization.
struct ActionSnapshot {
    std::string id;
    std::string title;
    std::string shortcut;
    bool enabled = true;
    bool checked = false;
};

/// A small app/window-local action registry. Use from the same UI thread as its callbacks.
/// Existing MenuItem callbacks and Window::onKey() handlers remain fully supported.
class ActionRegistry {
public:
    /// Returns false for an empty/duplicate id, missing handler or duplicate shortcut.
    bool add(std::string id, ActionDefinition definition) {
        if (id.empty() || !definition.execute || find(id) != m_actions.end()) return false;
        if (definition.shortcut && definition.shortcut->key == Key::Unknown) return false;
        if (definition.shortcut) {
            for (const auto& entry : m_actions) {
                if (entry.definition.shortcut == definition.shortcut) return false;
            }
        }
        m_actions.push_back({std::move(id), std::move(definition)});
        return true;
    }

    bool remove(std::string_view id) {
        const auto it = find(id);
        if (it == m_actions.end()) return false;
        m_actions.erase(it);
        return true;
    }

    [[nodiscard]] std::optional<ActionSnapshot> describe(std::string_view id) const {
        const auto it = find(id);
        if (it == m_actions.end()) return std::nullopt;
        return snapshot(*it);
    }

    [[nodiscard]] std::vector<ActionSnapshot> describeAll() const {
        std::vector<ActionSnapshot> result;
        result.reserve(m_actions.size());
        for (const auto& action : m_actions) result.push_back(snapshot(action));
        return result;
    }

    /// Recheck enabled state before executing. Copy callback so it can remove its own action safely.
    bool activate(std::string_view id) const {
        const auto it = find(id);
        if (it == m_actions.end()) return false;
        const auto enabled = it->definition.enabled;
        const auto execute = it->definition.execute;
        if (enabled && !enabled()) return false;
        execute();
        return true;
    }

    /// Intended for Window::onKey: the window's normal focused-widget dispatch runs first.
    /// A disabled action doesn't consume the key. Repeats don't re-trigger commands.
    bool dispatch(const KeyEvent& event) const {
        if (event.repeat) return false;
        for (const auto& entry : m_actions) {
            if (entry.definition.shortcut && entry.definition.shortcut->matches(event))
                return activate(entry.id);
        }
        return false;
    }

private:
    struct Entry {
        std::string id;
        ActionDefinition definition;
    };
    std::vector<Entry> m_actions;

    [[nodiscard]] std::vector<Entry>::iterator find(std::string_view id) {
        return std::find_if(m_actions.begin(), m_actions.end(),
                            [id](const Entry& entry) { return entry.id == id; });
    }
    [[nodiscard]] std::vector<Entry>::const_iterator find(std::string_view id) const {
        return std::find_if(m_actions.cbegin(), m_actions.cend(),
                            [id](const Entry& entry) { return entry.id == id; });
    }
    [[nodiscard]] static ActionSnapshot snapshot(const Entry& entry) {
        const auto& a = entry.definition;
        return {entry.id, a.dynamicTitle ? a.dynamicTitle() : a.title,
                a.shortcut ? actionShortcutLabel(*a.shortcut) : std::string{},
                !a.enabled || a.enabled(), a.checked && a.checked()};
    }
};

}  // namespace mayo
