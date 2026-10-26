# Experimental named actions (first iteration)

This change introduces a small, **optional**, application-local `mayo::ActionRegistry`.
It is a foundation for later application menus and external menu export. It does
**not** implement an external/global menu, a D-Bus publisher, a desktop panel, or
automatic switching of in-window menubars.

Existing `MenuItem` and `Window::onKey` usages remain unchanged. The registry is
header-only, requires no additional library, and runs on the application's UI
thread. Application authors own the registry and the callbacks it contains.

```cpp
#include "mayonnaise/actions.hpp"
#include "mayonnaise/widgets/action_menu.hpp"

mayo::ActionRegistry actions;
int copied = 0;
mayo::ActionDefinition copy{"Copy", [&] { ++copied; },
                            mayo::ActionShortcut{mayo::Key::C, mayo::Modifiers::Ctrl}};
actions.add("edit.copy", std::move(copy));

// Integrate alongside existing app shortcuts; widgets retain first chance at keys.
window.onKey([&](const mayo::KeyEvent& key) {
    return actions.dispatch(key);
});

// Menu items are snapshots; rebuild when dynamic state changes.
auto copyMenuItem = mayo::menuItemForAction(actions, "edit.copy");
```

Shortcuts use `Key`/`Modifiers`, not a label string, and the label is generated
from the same binding that dispatches keys. Only one action may own a given
shortcut in a registry. Enabled/checked states and dynamic labels are evaluated
when queried. `activate()` rechecks the enabled state before running a callback.

State queries own copies of the queried action definitions and callbacks before
calling application code. `describe(id)` returns that registration's snapshot even
if a state callback removes or replaces it. `describeAll()` freezes the complete
ordered collection before its first callback: removed/replaced entries still use
the original definitions, and newly added entries appear only in a later query.
Dynamic title, enabled and checked callbacks run in that order for each snapshot.
Values read through captures are evaluated when each callback runs; this is not
an atomic snapshot of application state. Mutable state stored inside a copied
callable is local to that copy; use captured application state for persistent state.

State callbacks may add/remove actions without invalidating the query. Activation
checks enabled state once using an owned definition, then verifies that the same
registration still exists. Removal or replacement (even under the same ID) rejects
the activation without executing either handler. Unrelated additions/removals do
not revoke it. Once execution begins, the execute callback may unregister itself.
Changes are not rolled back if a callback throws; exceptions propagate normally.
Callbacks must not destroy or concurrently access the registry during an operation.

Action callbacks must not outlive captured objects. The adapter's returned
`MenuItem` references the registry; the registry must therefore outlive the menu.
The registry is **not thread-safe** and should be used on the UI/event thread.

Next intended step: an application-level hierarchical `MenuModel` referencing
action IDs, then optional serialization and an external-menu protocol adapter.
The desktop shell will decide when an external menu is actually presented;
normal applications must retain in-window menus without a functioning host.
