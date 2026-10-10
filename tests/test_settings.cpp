#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

#include "mayonnaise/settings.hpp"
#include "mayonnaise/theme.hpp"

namespace fs = std::filesystem;
using namespace mayo;

namespace {
/// Points XDG_CONFIG_HOME at a fresh temporary directory for the test's lifetime.
struct TempConfig {
    fs::path dir = fs::temp_directory_path() / ("mayo-settings-test-" + std::to_string(::getpid()));
    std::string previous;
    bool had = false;
    TempConfig() {
        fs::remove_all(dir);
        if (const char* p = std::getenv("XDG_CONFIG_HOME")) {
            previous = p;
            had = true;
        }
        setenv("XDG_CONFIG_HOME", dir.c_str(), 1);
    }
    ~TempConfig() {
        if (had) setenv("XDG_CONFIG_HOME", previous.c_str(), 1);
        else unsetenv("XDG_CONFIG_HOME");
        fs::remove_all(dir);
    }
};
}  // namespace

TEST_CASE("settings round-trip through ~/.config/mayonnaise/<app>/settings.conf") {
    TempConfig tmp;
    {
        Settings s("demo");
        CHECK(s.directory() == (tmp.dir / "mayonnaise" / "demo").string());
        CHECK_FALSE(s.contains("x"));
        CHECK(s.getInt("missing", 7) == 7);
        s.set("flag", true);
        s.set("count", 42);
        s.set("ratio", 0.25);
        s.set("name", "multi\nline \\ value");
        s.set("path", std::string("/home/me/Documents"));
    }  // destructor saves
    CHECK(fs::exists(tmp.dir / "mayonnaise" / "demo" / "settings.conf"));
    Settings s("demo");
    CHECK(s.getBool("flag"));
    CHECK(s.getInt("count") == 42);
    CHECK(s.getDouble("ratio") == doctest::Approx(0.25));
    CHECK(s.getString("name") == "multi\nline \\ value");
    CHECK(s.getString("path") == "/home/me/Documents");
    s.remove("flag");
    s.save();
    CHECK_FALSE(Settings("demo").contains("flag"));
}

TEST_CASE("settings tolerate hand edits and bad values") {
    TempConfig tmp;
    fs::create_directories(tmp.dir / "mayonnaise" / "edited");
    std::ofstream(tmp.dir / "mayonnaise" / "edited" / "settings.conf")
        << "# comment\n\n  appearance =  dark  \nbroken line\nsize = big\nenabled = yes\n";
    Settings s("edited");
    CHECK(s.getString("appearance") == "dark");
    CHECK(s.getInt("size", 3) == 3);
    CHECK(s.getBool("enabled"));
    CHECK_FALSE(s.contains("broken line"));
}

TEST_CASE("read-only settings never write") {
    TempConfig tmp;
    {
        Settings s("ro");
        s.setReadOnly(true);
        s.set("a", 1);
        CHECK(s.getInt("a") == 1);
    }
    CHECK_FALSE(fs::exists(tmp.dir / "mayonnaise" / "ro" / "settings.conf"));
}

TEST_CASE("app names can't escape the settings directory") {
    TempConfig tmp;
    Settings s("../evil/name");
    CHECK(s.directory() == (tmp.dir / "mayonnaise" / ".._evil_name").string());
}

TEST_CASE("desktop accent colours snap to the macOS palette") {
    // GNOME's accents.
    CHECK(std::string(nearestAccent(Color::hex(0x3584E4)).name) == "Blue");
    CHECK(std::string(nearestAccent(Color::hex(0x2190A4)).name) == "Blue");  // teal
    CHECK(std::string(nearestAccent(Color::hex(0x3A944A)).name) == "Green");
    CHECK(std::string(nearestAccent(Color::hex(0xC88800)).name) == "Yellow");
    CHECK(std::string(nearestAccent(Color::hex(0xED5B00)).name) == "Orange");
    CHECK(std::string(nearestAccent(Color::hex(0xE62D42)).name) == "Red");
    CHECK(std::string(nearestAccent(Color::hex(0xD56199)).name) == "Pink");
    CHECK(std::string(nearestAccent(Color::hex(0x9141AC)).name) == "Purple");
    CHECK(std::string(nearestAccent(Color::hex(0x777777)).name) == "Graphite");
    CHECK(accentChoices().front().color == Theme::defaultAccent());
}
