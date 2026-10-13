// KConfig files as the System Settings example reads and writes them for KDE Plasma.
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "kconfig.hpp"

namespace fs = std::filesystem;

namespace {

/// A fresh XDG_CONFIG_HOME for the whole run (kcfg caches files by name, so each test uses its own file).
const fs::path& configHome() {
    static const fs::path dir = [] {
        std::string tmpl = (fs::temp_directory_path() / "mayo-kconfig-XXXXXX").string();
        const fs::path d = mkdtemp(tmpl.data());
        setenv("XDG_CONFIG_HOME", d.c_str(), 1);
        setenv("XDG_CONFIG_DIRS", (d / "xdg").c_str(), 1);
        return d;
    }();
    return dir;
}

void put(const std::string& name, const std::string& text) {
    fs::create_directories(configHome());
    std::ofstream(configHome() / name) << text;
}

std::string contents(const std::string& name) {
    std::ifstream in(configHome() / name);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

TEST_CASE("kconfig: escaping round-trips") {
    for (const std::string v : {"plain", " leading", "trailing ", "tab\there", "line\nbreak", "back\\slash", "a\\,b"}) {
        CHECK(kcfg::unescape(kcfg::escape(v)) == v);
    }
    CHECK(kcfg::unescape("\\sx\\x41") == " xA");
    CHECK(gx::splitList("us,tr,de") == std::vector<std::string>{"us", "tr", "de"});
    CHECK(gx::splitList(gx::joinList({"a,b", "c"})) == std::vector<std::string>{"a,b", "c"});
    CHECK(gx::splitList("").empty());
}

TEST_CASE("kconfig: reading groups, nested groups and markers") {
    put("readrc",
        "# comment\n"
        "[General]\n"
        "ColorScheme=BreezeDark\n"
        "Name[de]=Allgemein\n"
        "Name=General\n"
        "folders[$e]=$HOME/Music\n"
        "\n"
        "[Containments][1][Wallpaper][org.kde.image][General]\n"
        "Image = file:///usr/share/wallpapers/Next/\n");
    fs::create_directories(configHome() / "xdg");
    std::ofstream(configHome() / "xdg" / "readrc") << "[General]\nAccentColor=61,174,233\n";

    CHECK(kcfg::read("readrc", "General", "ColorScheme") == "BreezeDark");
    CHECK(kcfg::read("readrc", "General", "Name") == "General");
    CHECK(kcfg::read("readrc", "General", "folders") == std::string(std::getenv("HOME")) + "/Music");
    CHECK(kcfg::read("readrc", "Containments][1][Wallpaper][org.kde.image][General", "Image") ==
          "file:///usr/share/wallpapers/Next/");
    CHECK(kcfg::read("readrc", "General", "AccentColor") == "61,174,233");  // system default
    CHECK_FALSE(kcfg::read("readrc", "General", "Missing"));
    CHECK(kcfg::groups("readrc") == std::vector<std::string>{"General", "Containments][1][Wallpaper][org.kde.image][General"});
}

TEST_CASE("kconfig: writing keeps the rest of the file") {
    put("writerc", "# keep me\n[Windows]\nFocusPolicy=ClickToFocus\nPlacement=Smart\n\n[Other]\nx=1\n");
    kcfg::write("writerc", "Windows", "Placement", "Centered");
    kcfg::write("writerc", "Windows", "ElectricBorderTiling", "false");
    kcfg::write("writerc", "NightColor", "Active", "true");
    kcfg::write("writerc", "Other", "x", std::nullopt);
    kcfg::write("writerc", "Other", "y", " spaced ");
    CHECK(contents("writerc") ==
          "# keep me\n[Windows]\nFocusPolicy=ClickToFocus\nPlacement=Centered\nElectricBorderTiling=false\n\n"
          "[Other]\ny=\\sspaced\\s\n\n[NightColor]\nActive=true\n");
    CHECK(kcfg::read("writerc", "Other", "y") == " spaced ");
    CHECK_FALSE(kcfg::read("writerc", "Other", "x"));
}

TEST_CASE("kconfig: keys bound to entries") {
    put("keyrc", "[Keyboard]\nRepeatDelay=250\n");
    auto delay = kcfg::key("keyrc", "Keyboard", "RepeatDelay", "i", "600");
    auto rate = kcfg::key("keyrc", "Keyboard", "RepeatRate", "d", "25");
    auto repeat = kcfg::flagKey("keyrc", "Keyboard", "KeyRepeat", "repeat", "nothing", true);
    auto layouts = kcfg::key("keyrc", "Layout", "LayoutList", "as", "");
    CHECK(delay.getInt() == 250);
    CHECK(rate.getDouble() == doctest::Approx(25));
    CHECK(rate.isDefault());
    CHECK(repeat.getBool());

    int fired = 0;
    delay.watch([&] { ++fired; });
    delay.setInt(400);
    CHECK(fired == 1);
    CHECK(delay.getInt() == 400);
    delay.setInt(400);  // unchanged: no write, no notification
    CHECK(fired == 1);

    repeat.set(false);
    CHECK(kcfg::read("keyrc", "Keyboard", "KeyRepeat") == "nothing");
    CHECK_FALSE(repeat.getBool());
    rate.setDouble(30.5);
    CHECK(kcfg::read("keyrc", "Keyboard", "RepeatRate") == "30.5");
    layouts.setStrv({"us", "tr"});
    CHECK(kcfg::read("keyrc", "Layout", "LayoutList") == "us,tr");
    CHECK(layouts.getStrv() == std::vector<std::string>{"us", "tr"});
    delay.reset();
    CHECK(delay.getInt() == 600);
    CHECK(delay.isDefault());
}
