#pragma once

// Colour profiles (macOS Terminal's "Basic", "Pro", "Homebrew", "Ocean" plus Solarized) and the user's
// terminal preferences shared by every tab.

#include <mayonnaise/mayonnaise.hpp>

#include <array>
#include <string>
#include <vector>

namespace term {

struct Profile {
    std::string name;
    mayo::Color background;
    mayo::Color foreground;
    mayo::Color cursor;
    mayo::Color selection;
    std::array<mayo::Color, 16> ansi;
};

/// Profile names in display order.
const std::vector<std::string>& profileNames();
/// "Basic" follows the app's light/dark appearance; the others are fixed.
Profile profileNamed(const std::string& name, bool dark);
/// The xterm 256-colour palette for a profile (16 ANSI + 6×6×6 cube + 24 greys).
std::array<mayo::Color, 256> palette256(const Profile& profile);

enum class ExitAction : std::uint8_t { Close, CloseIfClean, Keep };

struct Config {
    std::string profile = "Basic";
    std::string fontFamily = "monospace";  ///< fontconfig pattern
    float fontSize = 13;
    float lineSpacing = 1.0f;
    bool boldIsBright = true;
    int cursorShape = 0;  ///< 0 block, 1 underline, 2 bar
    bool cursorBlink = true;
    int scrollback = 10000;
    bool visualBell = true;
    ExitAction onExit = ExitAction::CloseIfClean;
    bool confirmClose = true;  ///< ask before closing a tab that is running something

    void load(mayo::Settings& s);
    void save(mayo::Settings& s) const;
};

}  // namespace term
