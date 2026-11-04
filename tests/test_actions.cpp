#include <doctest/doctest.h>
#include <memory>
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

TEST_CASE("Single-action snapshots own callbacks across structural mutation") {
    int phase = 0;
    SUBCASE("dynamic title") { phase = 0; }
    SUBCASE("enabled") { phase = 1; }
    SUBCASE("checked") { phase = 2; }
    ActionRegistry actions;
    std::weak_ptr<int> lifetime;
    bool mutated = false;
    const std::string id(128, 'a');
    {
        auto token = std::make_shared<int>(42);
        lifetime = token;
        auto mutate = [&, token](int current) {
            if (current != phase || mutated) return;
            mutated = true;
            CHECK(actions.remove(id));
            for (int i = 0; i < 64; ++i)
                CHECK(actions.add("added." + std::to_string(i), ActionDefinition{"Added", [] {}}));
            CHECK(*token == 42);  // The callable must survive its own removal/reallocation.
        };
        ActionDefinition action{"Original", [] {}, ActionShortcut{Key::A, Modifiers::Ctrl}};
        action.dynamicTitle = [mutate] { mutate(0); return "Dynamic"; };
        action.enabled = [mutate] { mutate(1); return true; };
        action.checked = [mutate] { mutate(2); return true; };
        REQUIRE(actions.add(id, std::move(action)));
    }
    auto value = actions.describe(id);
    REQUIRE(value.has_value());
    CHECK(value->id == id);
    CHECK(value->title == "Dynamic");
    CHECK(value->shortcut == "Ctrl+A");
    CHECK(value->enabled);
    CHECK(value->checked);
    CHECK(mutated);
    CHECK_FALSE(actions.describe(id).has_value());
    CHECK(lifetime.expired());
}

TEST_CASE("Collection snapshots freeze all registrations before callbacks") {
    int phase = 0;
    SUBCASE("dynamic title") { phase = 0; }
    SUBCASE("enabled") { phase = 1; }
    SUBCASE("checked") { phase = 2; }
    ActionRegistry actions;
    bool changed = false;
    int originalQueries = 0;
    int replacementQueries = 0;
    ActionDefinition first{"First", [] {}};
    auto mutate = [&](int current) {
        if (current == phase && !changed) {
            changed = true;
            CHECK(actions.remove("second"));
            ActionDefinition replacement{"Replacement", [] {}};
            replacement.checked = [&] { ++replacementQueries; return false; };
            CHECK(actions.add("second", std::move(replacement)));
            for (int i = 0; i < 64; ++i)
                CHECK(actions.add("new." + std::to_string(i), ActionDefinition{"New", [] {}}));
        }
    };
    first.dynamicTitle = [&] { mutate(0); return "First queried"; };
    first.enabled = [&] { mutate(1); return true; };
    first.checked = [&] { mutate(2); return false; };
    REQUIRE(actions.add("first", std::move(first)));
    ActionDefinition second{"Original second", [] {}};
    second.enabled = [&] { ++originalQueries; return true; };
    REQUIRE(actions.add("second", std::move(second)));
    auto values = actions.describeAll();
    REQUIRE(values.size() == 2);
    CHECK(values[0].id == "first");
    CHECK(values[0].title == "First queried");
    CHECK(values[1].id == "second");
    CHECK(values[1].title == "Original second");
    CHECK(originalQueries == 1);
    CHECK(replacementQueries == 0);
    auto next = actions.describeAll();
    REQUIRE(next.size() == 66);
    CHECK(next[1].title == "Replacement");
    CHECK(replacementQueries == 1);
}

TEST_CASE("Enabled callbacks cannot authorize removed or replaced registrations") {
    bool replace = false;
    bool shortcut = false;
    SUBCASE("remove during direct activation") {}
    SUBCASE("replace during direct activation") { replace = true; }
    SUBCASE("remove during shortcut dispatch") { shortcut = true; }
    SUBCASE("replace during shortcut dispatch") { replace = true; shortcut = true; }
    ActionRegistry actions;
    const std::string id(128, 'b');
    int oldCalls = 0;
    int newCalls = 0;
    ActionDefinition action{"Original", [&] { ++oldCalls; }, ActionShortcut{Key::B, Modifiers::Ctrl}};
    action.enabled = [&] {
        CHECK(actions.remove(id));
        if (replace) CHECK(actions.add(id, ActionDefinition{"New", [&] { ++newCalls; }}));
        return true;
    };
    REQUIRE(actions.add(id, std::move(action)));
    const bool activated = shortcut ? actions.dispatch(KeyEvent{Key::B, Modifiers::Ctrl}) : actions.activate(id);
    CHECK_FALSE(activated);
    CHECK(oldCalls == 0);
    CHECK(newCalls == 0);
    CHECK(actions.activate(id) == replace);
    CHECK(newCalls == (replace ? 1 : 0));
}

TEST_CASE("Enabled callbacks may grow the registry without revoking an action") {
    ActionRegistry actions;
    int calls = 0;
    ActionDefinition action{"Original", [&] { ++calls; }, ActionShortcut{Key::C, Modifiers::Ctrl}};
    action.enabled = [&] {
        for (int i = 0; i < 64; ++i)
            CHECK(actions.add("growth." + std::to_string(i), ActionDefinition{"Added", [] {}}));
        return true;
    };
    REQUIRE(actions.add("original", std::move(action)));
    CHECK(actions.dispatch(KeyEvent{Key::C, Modifiers::Ctrl}));
    CHECK(calls == 1);
}

TEST_CASE("Execute callbacks retain their captures through shortcut self-removal") {
    ActionRegistry actions;
    std::weak_ptr<int> lifetime;
    const std::string id(128, 'c');
    int calls = 0;
    {
        auto token = std::make_shared<int>(7);
        lifetime = token;
        REQUIRE(actions.add(id, ActionDefinition{"Remove", [&, token] {
            CHECK(actions.remove(id));
            CHECK(*token == 7);
            ++calls;
        }, ActionShortcut{Key::D, Modifiers::Ctrl}}));
    }
    CHECK(actions.dispatch(KeyEvent{Key::D, Modifiers::Ctrl}));
    CHECK(calls == 1);
    CHECK(lifetime.expired());
    CHECK_FALSE(actions.dispatch(KeyEvent{Key::D, Modifiers::Ctrl}));
}

TEST_CASE("Enabled callback captures survive self-replacement until activation returns") {
    ActionRegistry actions;
    std::weak_ptr<int> lifetime;
    int calls = 0;
    {
        auto token = std::make_shared<int>(9);
        lifetime = token;
        ActionDefinition action{"Original", [&] { ++calls; }};
        action.enabled = [&, token] {
            CHECK(actions.remove("original"));
            CHECK(actions.add("original", ActionDefinition{"Replacement", [&] { ++calls; }}));
            CHECK(*token == 9);
            return true;
        };
        REQUIRE(actions.add("original", std::move(action)));
    }
    CHECK_FALSE(actions.activate("original"));
    CHECK(calls == 0);
    CHECK(lifetime.expired());
    CHECK(actions.activate("original"));
    CHECK(calls == 1);
}

TEST_CASE("Removing an unrelated earlier entry preserves activation authority") {
    ActionRegistry actions;
    int calls = 0;
    REQUIRE(actions.add("earlier", ActionDefinition{"Earlier", [] {}}));
    ActionDefinition action{"Keep", [&] { ++calls; }};
    action.enabled = [&] { CHECK(actions.remove("earlier")); return true; };
    REQUIRE(actions.add("keep", std::move(action)));
    CHECK(actions.activate("keep"));
    CHECK(calls == 1);
}
