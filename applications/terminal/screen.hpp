#pragma once

// Terminal emulation core: the cell grid, scrollback and an xterm-compatible escape sequence parser.
// No UI or OS dependencies, so it can be tested by feeding bytes and inspecting cells.

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace term {

/// Colours: 0 = default, 0x01'0000nn = palette index nn, 0x02'rrggbb = true colour.
using ColorRef = std::uint32_t;
constexpr ColorRef kDefaultColor = 0;
constexpr ColorRef paletteColor(int index) { return 0x01000000u | static_cast<std::uint32_t>(index & 0xFF); }
constexpr ColorRef rgbColor(int r, int g, int b) {
    return 0x02000000u | (static_cast<std::uint32_t>(r & 0xFF) << 16) | (static_cast<std::uint32_t>(g & 0xFF) << 8) |
           static_cast<std::uint32_t>(b & 0xFF);
}
constexpr bool isPalette(ColorRef c) { return (c >> 24) == 1; }
constexpr bool isRgb(ColorRef c) { return (c >> 24) == 2; }

enum Attr : std::uint16_t {
    kBold = 1 << 0,
    kDim = 1 << 1,
    kItalic = 1 << 2,
    kUnderline = 1 << 3,
    kBlink = 1 << 4,
    kInverse = 1 << 5,
    kHidden = 1 << 6,
    kStrike = 1 << 7,
    kWide = 1 << 8,          // first half of a double-width character
    kWideSpacer = 1 << 9,    // second half (draws nothing)
    kDoubleUnderline = 1 << 10,
};

/// 12 bytes: scrollback of 10,000 lines stays a few MB.
struct Cell {
    std::uint32_t ch = ' ';  // code point (21 bits) | attributes << 21
    ColorRef fg = kDefaultColor;
    ColorRef bg = kDefaultColor;

    [[nodiscard]] char32_t codepoint() const { return ch & 0x1FFFFF; }
    [[nodiscard]] std::uint16_t attrs() const { return static_cast<std::uint16_t>(ch >> 21); }
    void set(char32_t cp, std::uint16_t a) { ch = (static_cast<std::uint32_t>(cp) & 0x1FFFFF) | (static_cast<std::uint32_t>(a) << 21); }
    bool operator==(const Cell&) const = default;
};

struct Line {
    std::vector<Cell> cells;
    bool wrapped = false;  // continues on the next line (soft wrap), used by reflow and selection
};

enum class MouseMode : std::uint8_t { None, Click, Drag, Motion };
enum class CursorShape : std::uint8_t { Block, Underline, Bar };

/// Number of terminal cells a character occupies (0 for combining marks, 2 for wide CJK/emoji).
int cellWidth(char32_t cp);

class Screen {
public:
    Screen(int cols, int rows, int scrollbackLimit = 10000);

    /// Feeds output from the program.
    void feed(std::string_view bytes);
    void resize(int cols, int rows);
    void setScrollbackLimit(int lines);

    [[nodiscard]] int cols() const { return m_cols; }
    [[nodiscard]] int rows() const { return m_rows; }
    /// Visible line `row` (0 = top of the screen).
    [[nodiscard]] const Line& line(int row) const { return active()[static_cast<size_t>(row)]; }
    [[nodiscard]] int scrollbackSize() const { return static_cast<int>(m_scrollback.size()); }
    /// Scrollback line, 0 = oldest.
    [[nodiscard]] const Line& scrollbackLine(int index) const { return m_scrollback[static_cast<size_t>(index)]; }
    /// Line by absolute index: scrollback first, then the screen.
    [[nodiscard]] const Line& lineAt(int absolute) const;
    void clearScrollback();

    [[nodiscard]] int cursorX() const { return m_cursor.x; }
    [[nodiscard]] int cursorY() const { return m_cursor.y; }
    [[nodiscard]] bool cursorVisible() const { return m_cursorVisible; }
    [[nodiscard]] CursorShape cursorShape() const { return m_cursorShape; }
    [[nodiscard]] bool cursorBlinks() const { return m_cursorBlink; }
    /// The program chose a cursor style (DECSCUSR); otherwise the user's preference applies.
    [[nodiscard]] bool cursorStyleOverridden() const { return m_cursorStyleSet; }
    [[nodiscard]] bool altScreen() const { return m_alt; }
    [[nodiscard]] bool appCursorKeys() const { return m_appCursor; }
    [[nodiscard]] bool appKeypad() const { return m_appKeypad; }
    [[nodiscard]] bool bracketedPaste() const { return m_bracketedPaste; }
    [[nodiscard]] MouseMode mouseMode() const { return m_mouse; }
    [[nodiscard]] bool sgrMouse() const { return m_sgrMouse; }
    [[nodiscard]] bool focusReporting() const { return m_focusEvents; }
    [[nodiscard]] bool reverseVideo() const { return m_reverseVideo; }
    [[nodiscard]] const std::string& title() const { return m_title; }
    [[nodiscard]] const std::string& workingDirectory() const { return m_cwd; }
    /// Scrollback lines discarded at the limit so far: absolute line numbers stay stable as
    /// `droppedLines() + index` (selections use them).
    [[nodiscard]] std::uint64_t droppedLines() const { return m_dropped; }
    /// Incremented on every change, so views know when to redraw.
    [[nodiscard]] std::uint64_t generation() const { return m_generation; }

    /// Replies to queries (device attributes, cursor position...) go back to the program.
    std::function<void(const std::string&)> respond;
    std::function<void()> onBell;
    std::function<void(const std::string&)> onTitle;
    /// OSC 52: the program wants to set the clipboard.
    std::function<void(const std::string&)> onClipboard;
    /// OSC 10/11/12 and OSC 4 queries: the view supplies its colours (index: -1 fg, -2 bg, -3 cursor).
    std::function<std::uint32_t(int index)> queryColor;
    /// OSC 4 sets: palette overrides.
    std::function<void(int index, std::uint32_t rgb)> onSetPaletteColor;

private:
    struct Pen {
        std::uint16_t attrs = 0;
        ColorRef fg = kDefaultColor;
        ColorRef bg = kDefaultColor;
    };
    struct Cursor {
        int x = 0;
        int y = 0;
        bool pendingWrap = false;  // after writing the last column; the next character wraps
        Pen pen;
        bool originMode = false;
        int charset[2] = {0, 0};  // 0 = ASCII, 1 = DEC special graphics
        int gl = 0;               // which of G0/G1 is in use
    };
    enum class State : std::uint8_t { Ground, Escape, EscapeIntermediate, Csi, Osc, OscEscape, Dcs, DcsEscape, Ignore, IgnoreEscape };

    [[nodiscard]] std::vector<Line>& active() { return m_alt ? m_altLines : m_lines; }
    [[nodiscard]] const std::vector<Line>& active() const { return m_alt ? m_altLines : m_lines; }
    [[nodiscard]] Line blankLine() const;
    [[nodiscard]] Cell blankCell() const;

    void byte(unsigned char c);
    void control(unsigned char c);
    void print(char32_t cp);
    void escape(unsigned char final);
    void csi(unsigned char final);
    void osc();
    void sgr();
    void setMode(int mode, bool priv, bool on);
    void reportMode(int mode, bool priv);

    void lineFeed();
    void reverseIndex();
    void carriageReturn();
    void scrollUp(int top, int bottom, int n);
    void scrollDown(int top, int bottom, int n);
    void eraseCells(int y, int from, int to);
    void moveTo(int x, int y);
    void clampCursor();
    void saveCursor();
    void restoreCursor();
    void switchScreen(bool alt, bool saveRestore, bool clear);
    void reset();
    void reflow(int newCols);
    [[nodiscard]] int param(size_t i, int fallback) const;
    void touch() { ++m_generation; }

    int m_cols;
    int m_rows;
    int m_scrollbackLimit;
    std::vector<Line> m_lines;
    std::vector<Line> m_altLines;
    std::deque<Line> m_scrollback;
    bool m_alt = false;
    Cursor m_cursor;
    Cursor m_saved[2];  // per screen (main, alt)
    int m_top = 0;      // scroll region, inclusive
    int m_bottom = 0;
    std::vector<bool> m_tabs;

    bool m_autowrap = true;
    bool m_insert = false;
    bool m_newlineMode = false;
    bool m_cursorVisible = true;
    bool m_cursorBlink = false;
    bool m_cursorStyleSet = false;
    CursorShape m_cursorShape = CursorShape::Block;
    bool m_appCursor = false;
    bool m_appKeypad = false;
    bool m_bracketedPaste = false;
    MouseMode m_mouse = MouseMode::None;
    bool m_sgrMouse = false;
    bool m_focusEvents = false;
    bool m_reverseVideo = false;
    std::string m_title;
    std::string m_cwd;
    char32_t m_lastPrinted = ' ';

    // Parser.
    State m_state = State::Ground;
    std::uint32_t m_utf8 = 0;
    int m_utf8Left = 0;
    std::vector<int> m_params;
    std::vector<bool> m_subparam;  // param i was introduced by ':' (SGR colour forms)
    bool m_paramStarted = false;
    char m_private = 0;            // '?', '>', '=', '<'
    std::string m_intermediate;
    std::string m_osc;
    std::uint64_t m_generation = 0;
    std::uint64_t m_dropped = 0;
};

}  // namespace term
