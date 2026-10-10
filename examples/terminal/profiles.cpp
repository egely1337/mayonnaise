#include "profiles.hpp"

#include <algorithm>

using mayo::Color;

namespace term {

namespace {

constexpr std::array<Color, 16> kLightAnsi{
    Color::hex(0x1E1E1E), Color::hex(0xC4352B), Color::hex(0x2E8B3A), Color::hex(0xA87B07),
    Color::hex(0x2A5FCC), Color::hex(0x9B36B8), Color::hex(0x16899A), Color::hex(0xB8B8BC),
    Color::hex(0x6E6E73), Color::hex(0xE5483D), Color::hex(0x34A846), Color::hex(0xC99A1C),
    Color::hex(0x3F7CEB), Color::hex(0xBB55D6), Color::hex(0x24A8BB), Color::hex(0xE5E5EA),
};
constexpr std::array<Color, 16> kDarkAnsi{
    Color::hex(0x3A3A3C), Color::hex(0xFF5F57), Color::hex(0x5FD068), Color::hex(0xF5C451),
    Color::hex(0x5B9CFF), Color::hex(0xD27BF2), Color::hex(0x59D4E0), Color::hex(0xD9D9DE),
    Color::hex(0x7C7C80), Color::hex(0xFF7A73), Color::hex(0x7BE584), Color::hex(0xFFD772),
    Color::hex(0x7FB4FF), Color::hex(0xE39CFF), Color::hex(0x7EE6F0), Color::hex(0xFFFFFF),
};
constexpr std::array<Color, 16> kSolarized{
    Color::hex(0x073642), Color::hex(0xDC322F), Color::hex(0x859900), Color::hex(0xB58900),
    Color::hex(0x268BD2), Color::hex(0xD33682), Color::hex(0x2AA198), Color::hex(0xEEE8D5),
    Color::hex(0x002B36), Color::hex(0xCB4B16), Color::hex(0x586E75), Color::hex(0x657B83),
    Color::hex(0x839496), Color::hex(0x6C71C4), Color::hex(0x93A1A1), Color::hex(0xFDF6E3),
};

}  // namespace

const std::vector<std::string>& profileNames() {
    static const std::vector<std::string> names{"Basic", "Pro", "Homebrew", "Ocean", "Solarized Dark", "Solarized Light"};
    return names;
}

Profile profileNamed(const std::string& name, bool dark) {
    if (name == "Pro") return {name, Color::hex(0x000000), Color::hex(0xF2F2F2), Color::hex(0x9C9C9C), Color::hex(0x414A5C), kDarkAnsi};
    if (name == "Homebrew")
        return {name, Color::hex(0x000000), Color::hex(0x28FE14), Color::hex(0x38FE27), Color::hex(0x0A4E19), kDarkAnsi};
    if (name == "Ocean") return {name, Color::hex(0x224FBC), Color::hex(0xFFFFFF), Color::hex(0x7F7F7F), Color::hex(0x4A73D9), kDarkAnsi};
    if (name == "Solarized Dark")
        return {name, Color::hex(0x002B36), Color::hex(0x839496), Color::hex(0x93A1A1), Color::hex(0x0B4A5A), kSolarized};
    if (name == "Solarized Light")
        return {name, Color::hex(0xFDF6E3), Color::hex(0x657B83), Color::hex(0x586E75), Color::hex(0xE6DFC8), kSolarized};
    // Basic: plain white or black paper that follows the appearance, like macOS Terminal's default.
    if (dark) return {"Basic", Color::hex(0x1E1E1E), Color::hex(0xF2F2F2), Color::hex(0xA8A8AD), Color::hex(0x3F4F6E), kDarkAnsi};
    return {"Basic", Color::hex(0xFFFFFF), Color::hex(0x1D1D1F), Color::hex(0x8E8E93), Color::hex(0xB4D2FF), kLightAnsi};
}

std::array<Color, 256> palette256(const Profile& profile) {
    std::array<Color, 256> p{};
    std::copy(profile.ansi.begin(), profile.ansi.end(), p.begin());
    static constexpr int kLevels[6] = {0, 95, 135, 175, 215, 255};
    for (int r = 0; r < 6; ++r) {
        for (int g = 0; g < 6; ++g) {
            for (int b = 0; b < 6; ++b) p[static_cast<size_t>(16 + r * 36 + g * 6 + b)] = Color::rgb8(kLevels[r], kLevels[g], kLevels[b]);
        }
    }
    for (int i = 0; i < 24; ++i) {
        const int v = 8 + i * 10;
        p[static_cast<size_t>(232 + i)] = Color::rgb8(v, v, v);
    }
    return p;
}

void Config::load(mayo::Settings& s) {
    profile = s.getString("profile", profile);
    if (std::find(profileNames().begin(), profileNames().end(), profile) == profileNames().end()) profile = "Basic";
    fontFamily = s.getString("fontFamily", fontFamily);
    fontSize = std::clamp(s.getFloat("fontSize", fontSize), 8.0f, 36.0f);
    lineSpacing = std::clamp(s.getFloat("lineSpacing", lineSpacing), 0.9f, 1.6f);
    boldIsBright = s.getBool("boldIsBright", boldIsBright);
    cursorShape = std::clamp(s.getInt("cursorShape", cursorShape), 0, 2);
    cursorBlink = s.getBool("cursorBlink", cursorBlink);
    scrollback = std::clamp(s.getInt("scrollback", scrollback), 0, 1000000);
    visualBell = s.getBool("visualBell", visualBell);
    onExit = static_cast<ExitAction>(std::clamp(s.getInt("onExit", static_cast<int>(onExit)), 0, 2));
    confirmClose = s.getBool("confirmClose", confirmClose);
}

void Config::save(mayo::Settings& s) const {
    s.set("profile", profile);
    s.set("fontFamily", fontFamily);
    s.set("fontSize", fontSize);
    s.set("lineSpacing", lineSpacing);
    s.set("boldIsBright", boldIsBright);
    s.set("cursorShape", cursorShape);
    s.set("cursorBlink", cursorBlink);
    s.set("scrollback", scrollback);
    s.set("visualBell", visualBell);
    s.set("onExit", static_cast<int>(onExit));
    s.set("confirmClose", confirmClose);
}

}  // namespace term
