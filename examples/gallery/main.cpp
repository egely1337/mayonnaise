// Every Mayonnaise widget, in all states, with an appearance switch and accent colour picker.
#include <mayonnaise/mayonnaise.hpp>

#include <cstdlib>
#include <string>

using namespace mayo;

namespace {

Element section(const std::string& title, std::initializer_list<Element> rows) {
    auto body = VStack(rows).spacing(12);
    return VStack({
               Label(title).font(TextStyle::Headline),
               body,
           })
        .spacing(10);
}

}  // namespace

int main() {
    App app;
    auto& window = app.createWindow({.title = "Gallery", .width = 980, .height = 720});

    auto status = Label("Ready").secondary();

    window.setToolbar(Toolbar({
        Spacer(),
        ToolbarGroup({
            Button(Icon::ChevronLeft).style(ButtonStyle::Borderless).tooltip("Back"),
            Button(Icon::ChevronRight).style(ButtonStyle::Borderless).tooltip("Forward"),
        }),
        ToolbarGroup({
            Button(Icon::Share).style(ButtonStyle::Borderless).tooltip("Share"),
            Button(Icon::Trash).style(ButtonStyle::Borderless).tooltip("Delete"),
        }),
    }));
    auto picker = Picker({"Automatic", "Always", "When Scrolling", "Never"}, 0).onChange([&](int i) {
        status.setText("Picked option " + std::to_string(i));
    });
    auto menu = Menu("Actions", {
                                    MenuItem("New Folder", [&] { status.setText("New Folder"); }).withIcon(Icon::Folder).withShortcut("Ctrl+Shift+N"),
                                    MenuItem("Duplicate").withIcon(Icon::Document).withShortcut("Ctrl+D"),
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

    auto sliderValue = Label("50%").secondary().font(TextStyle::Callout);

    window.setContent(
        VStack({
            Label("Mayonnaise").font(TextStyle::LargeTitle),
            Label("A macOS-style UI toolkit for C++ and OpenGL.").secondary(),
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
                    section("Switches",
                            {
                                Toggle("Wi-Fi", true),
                                Toggle("Bluetooth"),
                                Toggle("Disabled", true).disabled(),
                            }),
                    section("Checkboxes",
                            {
                                Checkbox("Show hidden files", true),
                                Checkbox("Use tabs"),
                                Checkbox("Disabled").disabled(),
                            }),
                })
                    .spacing(28)
                    .width(440),
                VStack({
                    section("Slider",
                            {
                                HStack({
                                    Label("Volume"),
                                    Slider(0.5f).onChange([&](float v) {
                                        sliderValue.setText(std::to_string(static_cast<int>(v * 100)) + "%");
                                    }),
                                    sliderValue,
                                })
                                    .spacing(10),
                                Slider(0.3f).disabled(),
                            }),
                    section("Menus & Popovers",
                            {
                                HStack({picker, menu, popoverButton}).spacing(10),
                            }),
                    section("Appearance",
                            {
                                Toggle("Dark mode", app.appearance() == Appearance::Dark).onChange([&](bool on) {
                                    app.setAppearance(on ? Appearance::Dark : Appearance::Light);
                                }),
                            }),
                    section("Text",
                            {
                                Label("Body text wraps to the available width when it is longer than a single "
                                      "line, which is handy for descriptions and help text."),
                                Label("Secondary label").secondary(),
                                Label("Footnote").font(TextStyle::Footnote).tertiary(),
                            }),
                })
                    .spacing(28)
                    .fillWidth(),
            })
                .spacing(40)
                .align(Alignment::TopLeading),
            Spacer(),
            status,
        })
            .spacing(16)
            .padding(Insets(12, 28, 20, 28)));

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
