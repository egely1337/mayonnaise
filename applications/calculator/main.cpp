// Calculator — a macOS Calculator-style calculator built on Mayonnaise.
//
// Basic and scientific keypads (Ctrl+1 / Ctrl+2, or the key at the bottom left), full expressions with
// precedence and parentheses, memory, degrees/radians, the keyboard (numpad included), copy and paste, and
// numbers written the way the user's locale writes them.
#include <mayonnaise/mayonnaise.hpp>

#include <cstdlib>
#include <locale>
#include <string>

#include "calculator_view.hpp"

using namespace mayo;
using calc::Mode;

namespace {

struct ModeSize {
    Size size;
    Size minimum;
};

ModeSize sizeFor(Mode mode) {
    if (mode == Mode::Scientific) return {{700, 430}, {560, 360}};
    return {{264, 460}, {220, 380}};
}

/// Decimal and grouping separators from the user's locale (1.234,5 in Turkish, 1,234.5 in English).
calc::NumberFormat localeFormat(bool grouping) {
    calc::NumberFormat f;
    f.grouping = grouping;
    try {
        const std::locale loc("");
        const auto& np = std::use_facet<std::numpunct<char>>(loc);
        f.decimal = np.decimal_point();
        f.group = np.thousands_sep();
    } catch (...) {
        // An unknown locale name in the environment: keep the C defaults.
    }
    if (f.decimal != '.' && f.decimal != ',') f.decimal = '.';
    // Locales whose separator isn't a printable ASCII character (French uses a narrow space) get a plain space.
    if (static_cast<unsigned char>(f.group) < 0x20 || static_cast<unsigned char>(f.group) >= 0x7F) f.group = ' ';
    if (f.group == f.decimal) f.group = f.decimal == ',' ? '.' : ',';
    return f;
}

}  // namespace

int main() {
    App app("calculator", MAYO_DESKTOP_ID);
    Settings& prefs = app.settings();
    Mode mode = prefs.getString("mode") == "scientific" ? Mode::Scientific : Mode::Basic;
    bool grouping = prefs.getBool("grouping", true);

    const ModeSize initial = sizeFor(mode);
    auto& window = app.createWindow({.title = "Calculator",
                                     .width = static_cast<int>(initial.size.width),
                                     .height = static_cast<int>(initial.size.height),
                                     .minWidth = static_cast<int>(initial.minimum.width),
                                     .minHeight = static_cast<int>(initial.minimum.height)});
    window.setTitleVisible(false);

    auto view = std::make_shared<calc::CalculatorView>();
    view->setNumberFormat(localeFormat(grouping));
    view->setDegrees(prefs.getBool("degrees", true));
    view->setMode(mode);

    std::function<void(Mode)> setMode = [&](Mode m) {
        if (m == view->mode()) return;
        view->setMode(m);
        prefs.set("mode", m == Mode::Scientific ? "scientific" : "basic");
        const ModeSize s = sizeFor(m);
        // Lower the minimum first, or shrinking back to basic would be clamped by the scientific minimum.
        window.setMinimumSize(s.minimum);
        window.setSize(s.size);
    };
    view->onToggleMode = [&] { setMode(view->mode() == Mode::Basic ? Mode::Scientific : Mode::Basic); };
    view->onDegreesChanged = [&](bool degrees) { prefs.set("degrees", degrees); };

    // Right-click on the display: copy and paste.
    auto menu = std::make_shared<MenuView>(std::vector<MenuItem>{});
    auto menuAnchor = std::make_shared<Widget>();
    view->addChild(menuAnchor);
    view->onContextMenu = [&](Point at) {
        menu->setItems({
            MenuItem("Copy", [&] { view->copy(); }).withIcon(Icon::DocOnDoc).withShortcut("Ctrl+C").disabled(view->engine().hasError()),
            MenuItem("Paste", [&] { view->paste(); }).withShortcut("Ctrl+V"),
        });
        menu->setGap(2);
        menuAnchor->arrange({at.x, at.y, 1, 1});
        menu->showFrom(*menuAnchor);
        if (auto* h = view->host()) h->setFocus(menu.get(), false);
    };
    menu->setOnDismiss([&] {
        app.post([&] {
            if (auto* h = view->host()) h->setFocus(view.get(), false);
        });
    });

    // Settings window (Ctrl+,).
    Window* settingsWindow = nullptr;
    auto openSettings = [&] {
        if (settingsWindow) {
            settingsWindow->raise();
            return;
        }
        auto& w = app.createWindow({.title = "Calculator Settings", .width = 520, .height = 420, .minWidth = 440, .minHeight = 300});
        auto separators = Toggle("", grouping).onChange([&](bool on) {
            grouping = on;
            prefs.set("grouping", on);
            view->setNumberFormat(localeFormat(on));
        });
        auto angle = Picker({"Degrees", "Radians"}, view->degrees() ? 0 : 1).onChange([&](int i) {
            view->setDegrees(i == 0);
            prefs.set("degrees", i == 0);
        });
        auto form = Form({
            AppearanceSettings(app),
            FormSection("Calculator", {FormRow("Thousands separators", separators, "Group digits the way your language writes numbers"),
                                       FormRow("Angles", angle, "Used by sin, cos and tan in the scientific keypad")}),
        });
        w.setContent(ScrollView(VStack({form}).padding(Insets(12, 24, 24, 24))));
        w.onKey([&w](const KeyEvent& e) {
            if (e.key == Key::Escape || (e.primary() && e.key == Key::W)) {
                w.close();
                return true;
            }
            return false;
        });
        w.onClosed([&] { settingsWindow = nullptr; });
        settingsWindow = &w;
    };

    window.setContent(Element(view));
    window.onKey([&](const KeyEvent& e) {
        if (!e.primary()) return false;
        switch (e.key) {
            case Key::Num1: setMode(Mode::Basic); return true;
            case Key::Num2: setMode(Mode::Scientific); return true;
            case Key::Comma: openSettings(); return true;
            case Key::Q: app.quit(); return true;
            default: return false;
        }
    });
    if (auto* h = view->host()) h->setFocus(view.get(), false);

    // MAYO_DEMO for screenshots: "scientific" switches the keypad, "settings" opens the Settings window, and
    // anything else is typed ("12+3*4" ; '=' evaluates).
    if (const char* env = std::getenv("MAYO_DEMO")) {
        const std::string demo = env;
        app.after(0.2, [&, demo] {
            if (demo == "scientific") setMode(Mode::Scientific);
            else if (demo == "settings") openSettings();
            else view->onTextInput(TextInputEvent{demo});
        });
    }
    return app.run();
}
