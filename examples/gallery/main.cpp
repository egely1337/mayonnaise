// Every Mayonnaise widget, in all states, with an appearance switch and an accent colour picker.
#include <mayonnaise/mayonnaise.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace mayo;

namespace {

Element section(const std::string& title, std::initializer_list<Element> rows) {
    return VStack({
               Label(title).font(TextStyle::Title3),
               VStack(rows).spacing(12),
           })
        .spacing(10);
}

Element page(const std::string& title, const std::string& subtitle, std::initializer_list<Element> sections) {
    auto v = VStack({VStack({Label(title).font(TextStyle::LargeTitle), Label(subtitle).secondary()}).spacing(4)});
    for (const auto& s : sections) v.add(s);
    v.spacing(28).padding(Insets(8, 32, 32, 32));
    return v;
}

Element labelled(const std::string& label, const Element& control) {
    return HStack({Label(label).secondary().width(110), control}).spacing(12);
}

/// A generated "wallpaper" so the gallery needs no image files.
std::shared_ptr<ImageData> makeWallpaper() {
    const int w = 640;
    const int h = 360;
    std::vector<std::uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float fx = static_cast<float>(x) / w;
            float fy = static_cast<float>(y) / h;
            Color sky = Color::hex(0x2B5BD7).mix(Color::hex(0xF29B6C), fy);
            float hill1 = 0.62f + 0.08f * std::sin(fx * 6.0f + 1.0f);
            float hill2 = 0.75f + 0.06f * std::sin(fx * 9.0f + 3.0f);
            // Soft (anti-aliased) edges: blend over ~1.5 px.
            auto edge = [](float d) { return std::clamp(d * 360.0f / 1.5f + 0.5f, 0.0f, 1.0f); };
            float sun = std::hypot(fx - 0.7f, (fy - 0.45f) * 0.56f);
            Color c = sky.mix(Color::hex(0xFFE7A8), edge(0.07f - sun));
            c = c.mix(Color::hex(0x5B3C88).mix(Color::hex(0x2D1F4F), std::max(0.0f, fy - hill1) * 2.5f), edge(fy - hill1));
            c = c.mix(Color::hex(0x1E1638).mix(Color::hex(0x0E0A1C), std::max(0.0f, fy - hill2) * 3.0f), edge(fy - hill2));
            size_t i = (static_cast<size_t>(y) * w + x) * 4;
            px[i + 0] = static_cast<std::uint8_t>(c.r * 255);
            px[i + 1] = static_cast<std::uint8_t>(c.g * 255);
            px[i + 2] = static_cast<std::uint8_t>(c.b * 255);
            px[i + 3] = 255;
        }
    }
    return ImageData::fromPixels(w, h, px);
}

/// Colourful scene with glass panels on top, to show off the backdrop blur.
Element materialsDemo(const std::shared_ptr<ImageData>& wallpaper) {
    auto glass = [](Material m, const std::string& name) {
        return VStack({Label(name).font(TextStyle::Headline), Label("Material").font(TextStyle::Subheadline).secondary()})
            .spacing(2)
            .padding(14)
            .background(m)
            .cornerRadius(14)
            .width(140);
    };
    return ZStack({
                      Image(wallpaper).contentMode(ContentMode::Fill).cornerRadius(16).fillWidth().fillHeight(),
                      HStack({glass(Material::Sidebar, "Sidebar"), glass(Material::Popover, "Popover"),
                              glass(Material::Menu, "Menu"), glass(Material::Glass, "Glass")})
                          .spacing(16)
                          .align(Alignment::Center),
                  })
        .height(220);
}

}  // namespace

int main() {
    App app;
    auto& window = app.createWindow({.title = "Gallery", .width = 1060, .height = 740, .minWidth = 760});

    auto status = Label("Click something").secondary();
    auto sliderValue = Label("50%").secondary().font(TextStyle::Callout).width(36);
    auto progress = ProgressBar(0.5f);
    auto wallpaper = makeWallpaper();

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

    // Pages ------------------------------------------------------------------------------------
    auto controls = page(
        "Controls", "Buttons, switches and checkboxes in every style and state.",
        {
            section("Buttons",
                    {
                        HStack({
                            Button("Default").onClick([&] { status.setText("Default clicked"); }),
                            Button("Prominent").style(ButtonStyle::Prominent).onClick([&] {
                                status.setText("Prominent clicked");
                            }),
                            Button("Delete").style(ButtonStyle::Destructive),
                            Button("Borderless").style(ButtonStyle::Borderless),
                            Button("Disabled").disabled(),
                        })
                            .spacing(10),
                        HStack({
                            Button(Icon::Plus, "Add"),
                            Button(Icon::Share).style(ButtonStyle::Borderless).tooltip("Share"),
                            Button(Icon::Gear).style(ButtonStyle::Borderless).tooltip("Settings"),
                            Button("Small").controlSize(ControlSize::Small),
                            Button("Large").controlSize(ControlSize::Large).style(ButtonStyle::Prominent),
                        })
                            .spacing(10),
                    }),
            section("Switches",
                    {HStack({Toggle("Wi-Fi", true), Toggle("Bluetooth"), Toggle("Disabled", true).disabled()})
                         .spacing(24)}),
            section("Checkboxes", {HStack({Checkbox("Show hidden files", true), Checkbox("Use tabs"),
                                           Checkbox("Disabled").disabled()})
                                       .spacing(24)}),
            section("Segmented Control",
                    {
                        SegmentedControl({"Day", "Week", "Month", "Year"}, 1).onChange([&](int i) {
                            status.setText("Segment " + std::to_string(i));
                        }),
                        SegmentedControl({Icon::List, Icon::Grid, Icon::Photo}),
                    }),
            status,
        });

    auto input = page("Text Input", "Single-line fields with selection, clipboard and placeholders.",
                      {
                          section("Text Fields",
                                  {
                                      labelled("Name", TextField("Jane Appleseed").width(280).onSubmit(
                                                           [&](const std::string& s) { status.setText("Submitted " + s); })),
                                      labelled("Password", TextField("Required").secure().width(280)),
                                      labelled("Search", SearchField().width(280)),
                                      labelled("Disabled", TextField("Not editable").disabled().width(280)),
                                  }),
                          section("Sliders",
                                  {
                                      HStack({Label("Volume"),
                                              Slider(0.5f).width(320).onChange([&](float v) {
                                                  sliderValue.setText(std::to_string(static_cast<int>(v * 100)) + "%");
                                                  progress.setValue(v);
                                              }),
                                              sliderValue})
                                          .spacing(10),
                                      Slider(0.3f).width(320).disabled(),
                                  }),
                      });

    auto feedback = page("Progress & Menus", "Progress indicators, pop-up and pull-down menus, popovers.",
                         {
                             section("Progress", {progress.width(380), ProgressBar().width(380), ProgressSpinner()}),
                             section("Menus & Popovers", {HStack({picker, menu, popoverButton}).spacing(10)}),
                         });

    auto materials = page("Materials", "Backdrop-blurred, tinted glass over an image.",
                          {
                              section("Over an image", {materialsDemo(wallpaper)}),
                              section("Image", {HStack({Image(wallpaper).frame(240, 135).cornerRadius(12),
                                                        Image(wallpaper).contentMode(ContentMode::Fill).frame(135, 135).cornerRadius(68)})
                                                    .spacing(16)}),
                          });

    std::vector<Element> typeRows;
    const std::pair<TextStyle, const char*> styles[] = {
        {TextStyle::LargeTitle, "Large Title"}, {TextStyle::Title1, "Title 1"},   {TextStyle::Title2, "Title 2"},
        {TextStyle::Title3, "Title 3"},         {TextStyle::Headline, "Headline"}, {TextStyle::Body, "Body"},
        {TextStyle::Callout, "Callout"},       {TextStyle::Subheadline, "Subheadline"},
        {TextStyle::Footnote, "Footnote"},     {TextStyle::Caption, "Caption"},
    };
    auto typeList = VStack({}).spacing(10);
    for (const auto& [style, name] : styles) {
        typeList.add(HStack({Label(name).secondary().width(120), Label("The quick brown fox").font(style)}).spacing(12));
    }
    auto typography = page("Typography", "The macOS type scale, set in Inter.",
                           {typeList, section("Wrapping", {Label("Body text wraps to the available width when it is "
                                                                 "longer than a single line, which is handy for "
                                                                 "descriptions and help text.")
                                                               .maxWidth(520)})});

    auto detail = VStack({controls});
    auto scroller = ScrollView(detail);
    auto sidebar = Sidebar({
        SidebarItem::header("Widgets"),
        SidebarItem("controls", Icon::Square, "Controls"),
        SidebarItem("input", Icon::Keyboard, "Text Input"),
        SidebarItem("feedback", Icon::List, "Progress & Menus"),
        SidebarItem::header("Look & Feel"),
        SidebarItem("materials", Icon::Photo, "Materials"),
        SidebarItem("typography", Icon::Document, "Typography"),
    });
    sidebar.selection("controls").onSelect([&](const std::string& id) {
        detail.clear();
        if (id == "controls") detail.add(controls);
        if (id == "input") detail.add(input);
        if (id == "feedback") detail.add(feedback);
        if (id == "materials") detail.add(materials);
        if (id == "typography") detail.add(typography);
        scroller.scrollTo({0, 0}, false);
    });
    auto split = SplitView(sidebar, scroller).sidebarWidth(210);

    // Toolbar: sidebar toggle, appearance, accent ----------------------------------------------------
    const std::pair<const char*, Color> accents[] = {
        {"Blue", Theme::defaultAccent()},  {"Purple", Color::hex(0xA550A7)}, {"Pink", Color::hex(0xF74F9E)},
        {"Red", Color::hex(0xFF5257)},     {"Orange", Color::hex(0xF7821B)}, {"Green", Color::hex(0x62BA46)},
        {"Graphite", Color::hex(0x8C8C8C)},
    };
    std::vector<MenuItem> accentItems;
    for (const auto& [name, color] : accents) {
        Color c = color;
        accentItems.emplace_back(name, [&app, c] { app.setAccentColor(c); });
    }
    window.setToolbar(Toolbar({
        ToolbarGroup({Button(Icon::Sidebar).style(ButtonStyle::Borderless).tooltip("Toggle Sidebar").onClick([&] {
            split.toggleSidebar();
        })}),
        Spacer(),
        SegmentedControl({"Light", "Dark"}, app.appearance() == Appearance::Dark ? 1 : 0).onChange([&](int i) {
            app.setAppearance(i == 1 ? Appearance::Dark : Appearance::Light);
        }),
        Menu("Accent", accentItems).icon(Icon::Paintbrush),
    }));
    window.setContent(split);

    // MAYO_DEMO=<page> or picker|menu|popover, for screenshots.
    if (const char* demo = std::getenv("MAYO_DEMO")) {
        std::string which = demo;
        app.after(0.15, [&, which] {
            if (which == "picker" || which == "menu" || which == "popover") sidebar.select("feedback");
            else sidebar.select(which);
        });
        app.after(0.3, [&, which] {
            if (which == "picker") picker->open();
            if (which == "menu") menu->open();
            if (which == "popover") popover.show(popoverButton);
        });
    }
    return app.run();
}
