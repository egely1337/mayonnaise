// Every Mayonnaise widget, in all states, with an appearance switch and an accent colour picker.
#include <mayonnaise/mayonnaise.hpp>

#include <cmath>
#include <cstdlib>
#include <string>

using namespace mayo;

namespace {

Element section(const std::string& title, std::initializer_list<Element> rows) {
    return VStack({
               Label(title).font(TextStyle::Title3),
               VStack(rows).spacing(12),
           })
        .spacing(10);
}

Element labelled(const std::string& label, const Element& control) {
    return HStack({Label(label).secondary().width(110), control}).spacing(12);
}

/// Colourful scene with glass panels on top, to show off the backdrop blur.
Element materialsDemo() {
    auto backdrop = Canvas([](Painter& p, const Rect& r) {
        const Theme& t = theme();
        p.fillLinearGradient(r, 14, t.colors.indigo, t.colors.pink, r.origin(), {r.right(), r.bottom()});
        for (int i = 0; i < 7; ++i) {
            float x = r.x + 30 + static_cast<float>(i) * (r.width - 60) / 6.0f;
            float y = r.center().y + std::sin(static_cast<float>(i) * 1.3f) * r.height * 0.22f;
            Color c = i % 3 == 0 ? t.colors.yellow : i % 3 == 1 ? t.colors.mint : t.colors.orange;
            p.fillCircle({x, y}, 26, c);
        }
    });
    auto glass = [](Material m, const std::string& name) {
        return VStack({Label(name).font(TextStyle::Headline), Label("Material").font(TextStyle::Subheadline).secondary()})
            .spacing(2)
            .padding(14)
            .background(m)
            .cornerRadius(14)
            .width(150);
    };
    return ZStack({
                      backdrop,
                      HStack({glass(Material::Sidebar, "Sidebar"), glass(Material::Popover, "Popover"),
                              glass(Material::Glass, "Glass")})
                          .spacing(18)
                          .align(Alignment::Center),
                  })
        .height(170);
}

}  // namespace

int main() {
    App app;
    auto& window = app.createWindow({.title = "Gallery", .width = 1000, .height = 760, .minWidth = 720});

    auto status = Label("Ready").secondary();
    auto sliderValue = Label("50%").secondary().font(TextStyle::Callout).width(36);
    auto progress = ProgressBar(0.5f);

    // Popups -------------------------------------------------------------------------------
    auto picker = Picker({"Automatic", "Always", "When Scrolling", "Never"}, 0).onChange([&](int i) {
        status.setText("Picked option " + std::to_string(i));
    });
    auto menu = Menu("Actions", {
                                    MenuItem("New Folder", [&] { status.setText("New Folder"); })
                                        .withIcon(Icon::Folder)
                                        .withShortcut("Ctrl+Shift+N"),
                                    MenuItem("Duplicate", [&] { status.setText("Duplicate"); })
                                        .withIcon(Icon::Document)
                                        .withShortcut("Ctrl+D"),
                                    MenuItem("Share…").withIcon(Icon::Share),
                                    MenuItem::separator(),
                                    MenuItem("Unavailable").disabled(),
                                    MenuItem("Move to Trash").withIcon(Icon::Trash).asDestructive(),
                                });
    auto popoverButton = Button("Show Popover");
    auto popoverDone = Button("Done").style(ButtonStyle::Prominent);
    auto popover = Popover(VStack({
                                      Label("Popover").font(TextStyle::Headline),
                                      Label("Translucent, blurred, and anchored to its button.").secondary().width(200),
                                      HStack({Spacer(), popoverDone}),
                                  })
                               .spacing(8));
    popoverDone.onClick([&] { popover.dismiss(); });
    popoverButton.onClick([&] { popover.show(popoverButton); });

    // Toolbar: appearance + accent ----------------------------------------------------------------
    const Color accents[] = {Theme::defaultAccent(), Color::hex(0xA550A7), Color::hex(0xF74F9E), Color::hex(0xFF5257),
                             Color::hex(0xF7821B), Color::hex(0x62BA46), Color::hex(0x8C8C8C)};
    std::vector<MenuItem> accentItems;
    const char* accentNames[] = {"Blue", "Purple", "Pink", "Red", "Orange", "Green", "Graphite"};
    for (int i = 0; i < 7; ++i) {
        Color c = accents[i];
        accentItems.emplace_back(accentNames[i], [&app, c] { app.setAccentColor(c); });
    }
    window.setToolbar(Toolbar({
        Spacer(),
        SegmentedControl({"Light", "Dark"}, app.appearance() == Appearance::Dark ? 1 : 0).onChange([&](int i) {
            app.setAppearance(i == 1 ? Appearance::Dark : Appearance::Light);
        }),
        Menu("Accent", accentItems).icon(Icon::Paintbrush),
        SearchField().width(180),
    }));

    auto content = VStack({
        VStack({Label("Mayonnaise").font(TextStyle::LargeTitle),
                Label("A macOS-style UI toolkit for C++ and OpenGL. Every control below is drawn by the library.")
                    .secondary()})
            .spacing(4),
        Divider(),
        HStack({
            VStack({
                section("Buttons",
                        {
                            HStack({
                                Button("Default").onClick([&] { status.setText("Default clicked"); }),
                                Button("Prominent").style(ButtonStyle::Prominent).onClick([&] {
                                    status.setText("Prominent clicked");
                                }),
                                Button("Delete").style(ButtonStyle::Destructive),
                                Button("Borderless").style(ButtonStyle::Borderless),
                            })
                                .spacing(10),
                            HStack({
                                Button(Icon::Plus, "Add"),
                                Button(Icon::Share).style(ButtonStyle::Borderless).tooltip("Share"),
                                Button(Icon::Gear).style(ButtonStyle::Borderless).tooltip("Settings"),
                                Button("Disabled").disabled(),
                                Button("Large").controlSize(ControlSize::Large).style(ButtonStyle::Prominent),
                            })
                                .spacing(10),
                        }),
                section("Switches & Checkboxes",
                        {
                            HStack({Toggle("Wi-Fi", true), Toggle("Bluetooth"), Toggle("Disabled", true).disabled()})
                                .spacing(22),
                            HStack({Checkbox("Show hidden files", true), Checkbox("Use tabs"),
                                    Checkbox("Disabled").disabled()})
                                .spacing(22),
                        }),
                section("Segmented Control",
                        {
                            SegmentedControl({"Day", "Week", "Month", "Year"}, 1).onChange([&](int i) {
                                status.setText("Segment " + std::to_string(i));
                            }),
                            SegmentedControl({Icon::List, Icon::Grid, Icon::Photo}),
                        }),
                section("Menus & Popovers", {HStack({picker, menu, popoverButton}).spacing(10)}),
            })
                .spacing(26)
                .width(440),
            VStack({
                section("Text Fields",
                        {
                            labelled("Name", TextField("Jane Appleseed").onSubmit([&](const std::string& s) {
                                status.setText("Submitted: " + s);
                            })),
                            labelled("Password", TextField("Required").secure()),
                            labelled("Search", SearchField()),
                            labelled("Disabled", TextField("Not editable").disabled()),
                        }),
                section("Slider & Progress",
                        {
                            HStack({
                                Label("Volume"),
                                Slider(0.5f).onChange([&](float v) {
                                    sliderValue.setText(std::to_string(static_cast<int>(v * 100)) + "%");
                                    progress.setValue(v);
                                }),
                                sliderValue,
                            })
                                .spacing(10),
                            Slider(0.3f).disabled(),
                            progress,
                            HStack({ProgressBar(), ProgressSpinner()}).spacing(14),
                        }),
                section("Text",
                        {
                            Label("Body text wraps to the available width when it is longer than a single line, "
                                  "which is handy for descriptions and help text."),
                            HStack({Label("Secondary").secondary(), Label("Tertiary").tertiary(),
                                    Label("Footnote").font(TextStyle::Footnote).secondary()})
                                .spacing(14),
                        }),
            })
                .spacing(26)
                .fillWidth(),
        })
            .spacing(36)
            .align(Alignment::TopLeading),
        section("Materials", {materialsDemo()}),
        status,
    });
    content.spacing(22).padding(Insets(12, 28, 28, 28));
    window.setContent(ScrollView(content));

    // MAYO_DEMO=picker|menu|popover opens a popup at startup (for screenshots).
    if (const char* demo = std::getenv("MAYO_DEMO")) {
        std::string which = demo;
        app.after(0.15, [&, which] {
            if (which == "picker") picker->open();
            if (which == "menu") menu->open();
            if (which == "popover") popover.show(popoverButton);
        });
    }
    return app.run();
}
