#include <doctest/doctest.h>
#include "mayonnaise/actions.hpp"
#include "mayonnaise/widgets/action_menu.hpp"

using namespace mayo;

TEST_CASE("Named actions have unique stable ids and typed shortcuts") {
    ActionRegistry actions;
    int invoked = 0;
    ActionDefinition copy{"Copy", [&] { ++invoked; }, ActionShortcut{Key::C, Modifiers::Ctrl}};
    CHECK(actions.add("edit.copy", copy));
    CHECK_FALSE(actions.add("edit.copy", copy));
    CHECK_FALSE(actions.add("", copy));
    CHECK_FALSE(actions.add("edit.other-copy", copy));  // ambiguous key binding
    CHECK_FALSE(actions.add("edit.broken", ActionDefinition{"Broken", {}}));
    CHECK(actions.describeAll().size() == 1);
    CHECK(actions.describe("edit.copy")->shortcut == "Ctrl+C");
    CHECK(actions.dispatch(KeyEvent{Key::C, Modifiers::Ctrl}));
    CHECK(invoked == 1);
    CHECK_FALSE(actions.dispatch(KeyEvent{Key::C, Modifiers::Ctrl | Modifiers::Shift}));
    CHECK_FALSE(actions.dispatch(KeyEvent{Key::C, Modifiers::Ctrl, true}));
    CHECK_FALSE(actions.dispatch(KeyEvent{Key::X, Modifiers::Ctrl}));
    CHECK(invoked == 1);
}

TEST_CASE("Actions evaluate enabled state and title when queried or invoked") {
    ActionRegistry actions;
    bool canUndo = false;
    bool checked = false;
    int calls = 0;
    std::string operation = "Rename";
    ActionDefinition undo{"Undo", [&] { ++calls; }, ActionShortcut{Key::Z, Modifiers::Ctrl}};
    undo.enabled = [&] { return canUndo; };
    undo.checked = [&] { return checked; };
    undo.dynamicTitle = [&] { return "Undo " + operation; };
    REQUIRE(actions.add("edit.undo", std::move(undo)));

    auto first = actions.describe("edit.undo");
    REQUIRE(first.has_value());
    CHECK(first->title == "Undo Rename");
    CHECK_FALSE(first->enabled);
    CHECK_FALSE(first->checked);
    CHECK_FALSE(actions.activate("edit.undo"));
    CHECK_FALSE(actions.dispatch(KeyEvent{Key::Z, Modifiers::Ctrl}));
    CHECK(calls == 0);

    canUndo = true;
    checked = true;
    operation = "Move";
    auto second = actions.describe("edit.undo");
    REQUIRE(second.has_value());
    CHECK(second->title == "Undo Move");
    CHECK(second->enabled);
    CHECK(second->checked);
    CHECK(actions.dispatch(KeyEvent{Key::Z, Modifiers::Ctrl}));
    CHECK(calls == 1);
}

TEST_CASE("Actions can unregister themselves during activation") {
    ActionRegistry actions;
    int calls = 0;
    REQUIRE(actions.add("file.quit", ActionDefinition{"Quit", [&] {
        ++calls;
        actions.remove("file.quit");
    }}));
    CHECK(actions.activate("file.quit"));
    CHECK(calls == 1);
    CHECK_FALSE(actions.activate("file.quit"));
    CHECK_FALSE(actions.describe("file.quit").has_value());
    CHECK_FALSE(actions.remove("file.quit"));
}

TEST_CASE("Existing menus can present named actions and refresh their state") {
    ActionRegistry actions;
    bool enabled = false;
    int calls = 0;
    ActionDefinition copy{"Copy", [&] { ++calls; }, ActionShortcut{Key::C, Modifiers::Ctrl}};
    copy.enabled = [&] { return enabled; };
    REQUIRE(actions.add("edit.copy", std::move(copy)));

    auto item = menuItemForAction(actions, "edit.copy");
    REQUIRE(item.has_value());
    CHECK(item->title == "Copy");
    CHECK(item->shortcut == "Ctrl+C");
    CHECK_FALSE(item->enabled);
    item->action();                         // disabled must still be enforced by registry
    CHECK(calls == 0);
    enabled = true;
    CHECK_FALSE(item->enabled);            // MenuItem is intentionally a snapshot
    auto refreshed = menuItemForAction(actions, "edit.copy");
    REQUIRE(refreshed.has_value());
    CHECK(refreshed->enabled);
    refreshed->action();
    CHECK(calls == 1);
    CHECK_FALSE(menuItemForAction(actions, "not.registered").has_value());
}

TEST_CASE("Shortcut labels cover common menu accelerators") {
    CHECK(actionShortcutLabel({Key::Comma, Modifiers::Ctrl}) == "Ctrl+,");
    CHECK(actionShortcutLabel({Key::Num1, Modifiers::Ctrl}) == "Ctrl+1");
    CHECK(actionShortcutLabel({Key::F5, Modifiers::None}) == "F5");
    CHECK(actionShortcutLabel({Key::N, Modifiers::Ctrl | Modifiers::Shift}) == "Ctrl+Shift+N");
    CHECK(actionShortcutLabel({Key::Unknown, Modifiers::None}).empty());
}
