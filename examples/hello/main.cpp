#include <mayonnaise/mayonnaise.hpp>

int main() {
    mayo::App app;

    auto& window = app.createWindow({ .title = "Hello", .width = 800, .height = 520 });

    int count = 0;
    auto label = mayo::Label("Clicked 0 times").font(mayo::TextStyle::Title2);

    window.setContent(
        mayo::VStack({
            label,
            mayo::Button("Click me").style(mayo::ButtonStyle::Prominent)
                .onClick([&] { label.setText("Clicked " + std::to_string(++count) + " times"); }),
            mayo::Toggle("Dark mode").onChange([&](bool on) {
                app.setAppearance(on ? mayo::Appearance::Dark : mayo::Appearance::Light);
            }),
        }).spacing(12).padding(24).align(mayo::Alignment::Center)
    );

    return app.run();
}
