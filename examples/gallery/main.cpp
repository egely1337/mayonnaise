// Every Mayonnaise widget, in all states, with an appearance switch and accent colour picker.
#include <mayonnaise/mayonnaise.hpp>

#include <cstdio>

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

    return app.run();
}
