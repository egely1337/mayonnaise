// A "System Settings"-like window: translucent sidebar, grouped forms, live appearance switching.
#include <mayonnaise/mayonnaise.hpp>

#include <array>
#include <cstdlib>
#include <string>
#include <map>

using namespace mayo;

namespace {

struct Accent {
    const char* name;
    Color color;
};
const std::array<Accent, 8> kAccents{{
    {"Multicolor", Theme::defaultAccent()},
    {"Purple", Color::hex(0xA550A7)},
    {"Pink", Color::hex(0xF74F9E)},
    {"Red", Color::hex(0xFF5257)},
    {"Orange", Color::hex(0xF7821B)},
    {"Yellow", Color::hex(0xFFC600)},
    {"Green", Color::hex(0x62BA46)},
    {"Graphite", Color::hex(0x8C8C8C)},
}};

/// Row of accent colour dots, drawn with a Canvas (shows custom drawing without subclassing).
Element accentPicker(App& app) {
    constexpr float kDot = 16;
    constexpr float kGap = 10;
    auto canvas = Canvas([&app](Painter& p, const Rect& r) {
        for (size_t i = 0; i < kAccents.size(); ++i) {
            Point c{r.x + kDot * 0.5f + static_cast<float>(i) * (kDot + kGap), r.center().y};
            Color col = kAccents[i].color;
            p.drawShadow({c.x - kDot / 2, c.y - kDot / 2, kDot, kDot}, kDot / 2, Color::black(0.15f), 2, {0, 0.5f});
            p.fillCircle(c, kDot * 0.5f, col);
            p.strokeCircle(c, kDot * 0.5f, Color::black(0.12f), 0.5f);
            if (app.accentColor() == col) p.fillCircle(c, 3, Color::white());
        }
    });
    canvas.frame(kAccents.size() * (kDot + kGap) - kGap, 22);
    canvas.onClick([&app, node = canvas.node()](Point pt) {
        float x = pt.x - node->frame().x;
        auto i = static_cast<size_t>(x / (kDot + kGap));
        if (i < kAccents.size()) app.setAccentColor(kAccents[i].color);
    });
    return canvas;
}

Element pageTitle(const std::string& title, Icon icon, Color tile) {
    auto badge = Canvas([icon, tile](Painter& p, const Rect& r) {
        p.fillVerticalGradient(r, 9, tile.lighter(0.12f), tile);
        p.drawIcon(icon, r.inset(7), Color::white(), 1.1f);
    });
    badge.frame(36, 36);
    return HStack({badge, Label(title).font(TextStyle::Title1)}).spacing(12);
}

Element generalPage() {
    return VStack({
               pageTitle("General", Icon::Gear, Color::hex(0x8E8E93)),
               Form({
                   FormSection({
                       FormRow("About", Button(Icon::ChevronRight).style(ButtonStyle::Borderless)),
                       FormRow("Software Update", Label("Up to date").secondary()),
                       FormRow("Storage", Label("412 GB available").secondary()),
                   }),
                   FormSection("Sharing",
                               {
                                   FormRow("AirDrop & Handoff", Toggle({}, true)),
                                   FormRow("Login Items & Extensions", Button(Icon::ChevronRight).style(ButtonStyle::Borderless)),
                                   FormRow("Language & Region", Picker({"English (US)", "Deutsch", "Türkçe", "日本語"})),
                               }),
                   FormSection("Date & Time",
                               {
                                   FormRow("Set time and date automatically", Toggle({}, true)),
                                   FormRow("24-hour time", Toggle(), "Show the time as 13:00 instead of 1:00 PM"),
                               }),
                   FormSection("Startup",
                               {
                                   FormRow("Startup Disk", Label("Macintosh HD").secondary()),
                                   FormRow("Time Machine", Button("Set Up…")),
                                   FormRow("Transfer or Reset", Button(Icon::ChevronRight).style(ButtonStyle::Borderless)),
                               },
                               "Changes take effect the next time you restart."),
               }),
           })
        .spacing(20);
}

Element appearancePage(App& app) {
    return VStack({
               pageTitle("Appearance", Icon::Paintbrush, Color::hex(0x1C1C1E)),
               Form({
                   FormSection({
                       FormRow("Appearance",
                               Picker({"Light", "Dark"}, app.appearance() == Appearance::Dark ? 1 : 0)
                                   .onChange([&app](int i) {
                                       app.setAppearance(i == 1 ? Appearance::Dark : Appearance::Light);
                                   })),
                       FormRow("Accent color", accentPicker(app)),
                       FormRow("Show scroll bars", Picker({"Automatically", "When scrolling", "Always"})),
                   }),
                   FormSection("Sidebar",
                               {
                                   FormRow("Sidebar icon size", Picker({"Small", "Medium", "Large"}, 1)),
                                   FormRow("Allow wallpaper tinting in windows", Toggle({}, true)),
                               }),
               }),
           })
        .spacing(20);
}

Element soundPage() {
    return VStack({
               pageTitle("Sound", Icon::Speaker, Color::hex(0xFF375F)),
               Form({
                   FormSection("Output",
                               {
                                   FormRow("Output volume", HStack({Slider(0.7f).width(220), Toggle("Mute")}).spacing(14)),
                                   FormRow("Balance", Slider(0.5f).width(220)),
                               }),
                   FormSection("Sound Effects",
                               {
                                   FormRow("Alert sound", Picker({"Boop", "Breeze", "Bubble", "Crystal", "Funky"})),
                                   FormRow("Alert volume", Slider(0.4f).width(220)),
                                   FormRow("Play sound on startup", Toggle({}, true)),
                                   FormRow("Play user interface sound effects", Toggle({}, true)),
                               }),
               }),
           })
        .spacing(20);
}

Element wifiPage() {
    auto networks = Form({
        FormSection({
            FormRow("Wi-Fi", Toggle({}, true), "Connected to Mayonnaise-5G"),
        }),
        FormSection("Known Networks",
                    {
                        FormRow("Mayonnaise-5G", Menu("Options", {MenuItem("Copy Password").withIcon(Icon::Lock),
                                                                  MenuItem("Network Settings…").withIcon(Icon::Gear),
                                                                  MenuItem::separator(),
                                                                  MenuItem("Forget This Network").asDestructive()})
                                                     .borderless()),
                        FormRow("Coffee Shop", Label("Saved").secondary()),
                        FormRow("Airport Free Wi-Fi", Label("Open").secondary()),
                    }),
        FormSection("Other Networks",
                    {
                        FormRow("Ask to join networks", Picker({"Notify", "Ask", "Off"})),
                        FormRow("Ask to join hotspots", Picker({"Automatic", "Ask", "Never"})),
                    }),
    });
    return VStack({pageTitle("Wi-Fi", Icon::Wifi, Color::hex(0x0088FF)), networks}).spacing(20);
}

Element placeholderPage(const std::string& title, Icon icon, Color tile) {
    return VStack({
               pageTitle(title, icon, tile),
               Form({FormSection({
                   FormRow("Nothing to configure here yet", Label("—").secondary()),
               })}),
           })
        .spacing(20);
}

}  // namespace

int main() {
    App app;
    auto& window = app.createWindow({.title = "Settings", .width = 860, .height = 620, .minWidth = 640, .minHeight = 420});

    auto sidebar = Sidebar({
        SidebarItem("wifi", Icon::Wifi, "Wi-Fi").withTile(Color::hex(0x0088FF)),
        SidebarItem("bluetooth", Icon::Bluetooth, "Bluetooth").withTile(Color::hex(0x0088FF)),
        SidebarItem("network", Icon::Globe, "Network").withTile(Color::hex(0x0088FF)),
        SidebarItem::header("System"),
        SidebarItem("general", Icon::Gear, "General").withTile(Color::hex(0x8E8E93)),
        SidebarItem("appearance", Icon::Paintbrush, "Appearance").withTile(Color::hex(0x1C1C1E)),
        SidebarItem("accessibility", Icon::PersonCircle, "Accessibility").withTile(Color::hex(0x0088FF)),
        SidebarItem("displays", Icon::Display, "Displays").withTile(Color::hex(0x0088FF)),
        SidebarItem("battery", Icon::Battery, "Battery").withTile(Color::hex(0x34C759)),
        SidebarItem::header("Notifications"),
        SidebarItem("notifications", Icon::Bell, "Notifications").withTile(Color::hex(0xFF3B30)).withBadge("2"),
        SidebarItem("sound", Icon::Speaker, "Sound").withTile(Color::hex(0xFF375F)),
        SidebarItem("focus", Icon::Moon, "Focus").withTile(Color::hex(0x6155F5)),
        SidebarItem::header("Security"),
        SidebarItem("privacy", Icon::Hand, "Privacy & Security").withTile(Color::hex(0x0088FF)),
        SidebarItem("lock", Icon::Lock, "Lock Screen").withTile(Color::hex(0x1C1C1E)),
        SidebarItem("users", Icon::Person, "Users & Groups").withTile(Color::hex(0x0088FF)),
        SidebarItem("keyboard", Icon::Keyboard, "Keyboard").withTile(Color::hex(0x8E8E93)),
    });
    sidebar.selection("appearance").header(SearchField());

    auto detail = VStack({appearancePage(app)}).padding(Insets(8, 28, 24, 28));
    auto scroller = ScrollView(detail);
    auto split = SplitView(sidebar, scroller).sidebarWidth(230);

    sidebar.onSelect([&](const std::string& id) {
        detail.clear();
        if (id == "general") detail.add(generalPage());
        else if (id == "appearance") detail.add(appearancePage(app));
        else if (id == "sound") detail.add(soundPage());
        else if (id == "wifi") detail.add(wifiPage());
        else if (id == "bluetooth") detail.add(placeholderPage("Bluetooth", Icon::Bluetooth, Color::hex(0x0088FF)));
        else if (id == "battery") detail.add(placeholderPage("Battery", Icon::Battery, Color::hex(0x34C759)));
        else detail.add(placeholderPage("Settings", Icon::Gear, Color::hex(0x8E8E93)));
    });

    window.setToolbar(Toolbar({
        ToolbarGroup({
            Button(Icon::ChevronLeft).style(ButtonStyle::Borderless),
            Button(Icon::ChevronRight).style(ButtonStyle::Borderless),
        }),
        Spacer(),
        ToolbarGroup({Button(Icon::Sidebar).style(ButtonStyle::Borderless).tooltip("Toggle Sidebar").onClick([&] {
            split.toggleSidebar();
        })}),
    }));
    window.setContent(split);

    // MAYO_DEMO=scroll shows a long page scrolled under the toolbar (for screenshots).
    if (const char* demo = std::getenv("MAYO_DEMO"); demo && std::string(demo) == "scroll") {
        sidebar.select("general");
        app.after(0.1, [&] { scroller.scrollTo({0, 150}, false); });
    }
    return app.run();
}
