#include "screen.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace term {

namespace {

constexpr char32_t kReplacement = 0xFFFD;

/// DEC special graphics (ESC ( 0): line drawing used by ncurses' ACS characters.
char32_t decGraphics(char32_t c) {
    static constexpr char32_t kMap[] = {
        0x00A0, 0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0, 0x00B1, 0x2424, 0x240B, 0x2518, 0x2510, 0x250C, 0x2514, 0x253C,
        0x23BA, 0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524, 0x2534, 0x252C, 0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7,
    };
    return c >= 0x5F && c <= 0x7E ? kMap[c - 0x5F] : c;
}

bool inRanges(char32_t cp, const char32_t (*ranges)[2], size_t n) {
    size_t lo = 0;
    size_t hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (cp < ranges[mid][0]) hi = mid;
        else if (cp > ranges[mid][1]) lo = mid + 1;
        else return true;
    }
    return false;
}

std::string base64Decode(std::string_view in) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    int acc = 0;
    int bits = 0;
    for (char c : in) {
        int v = value(c);
        if (v < 0) continue;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

std::string colorReply(std::uint32_t rgb) {
    char buf[40];
    const unsigned r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    std::snprintf(buf, sizeof buf, "rgb:%02x%02x/%02x%02x/%02x%02x", r, r, g, g, b, b);
    return buf;
}

/// "rgb:rr/gg/bb" (1-4 hex digits per channel) or "#rrggbb".
bool parseColorSpec(std::string_view s, std::uint32_t& rgb) {
    auto hex = [](std::string_view h, unsigned& v) {
        if (h.empty() || h.size() > 4) return false;
        v = 0;
        for (char c : h) {
            int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (d < 0) return false;
            v = v * 16 + static_cast<unsigned>(d);
        }
        // Scale to 8 bits.
        const unsigned max = (1u << (4 * h.size())) - 1;
        v = v * 255 / max;
        return true;
    };
    unsigned r = 0, g = 0, b = 0;
    if (s.rfind("rgb:", 0) == 0) {
        s.remove_prefix(4);
        size_t a = s.find('/');
        size_t c = s.find('/', a + 1);
        if (a == std::string_view::npos || c == std::string_view::npos) return false;
        if (!hex(s.substr(0, a), r) || !hex(s.substr(a + 1, c - a - 1), g) || !hex(s.substr(c + 1), b)) return false;
    } else if (s.size() == 7 && s[0] == '#') {
        if (!hex(s.substr(1, 2), r) || !hex(s.substr(3, 2), g) || !hex(s.substr(5, 2), b)) return false;
    } else {
        return false;
    }
    rgb = (r << 16) | (g << 8) | b;
    return true;
}

}  // namespace

int cellWidth(char32_t cp) {
    static constexpr char32_t kZero[][2] = {
        {0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD}, {0x0610, 0x061A}, {0x064B, 0x065F}, {0x0E31, 0x0E31},
        {0x0E34, 0x0E3A}, {0x0E47, 0x0E4E}, {0x1AB0, 0x1AFF}, {0x1DC0, 0x1DFF}, {0x200B, 0x200F}, {0x2028, 0x202E},
        {0x2060, 0x2064}, {0x20D0, 0x20FF}, {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F}, {0xFEFF, 0xFEFF}, {0xE0100, 0xE01EF},
    };
    static constexpr char32_t kWideRanges[][2] = {
        {0x1100, 0x115F}, {0x231A, 0x231B}, {0x2329, 0x232A}, {0x23E9, 0x23EC}, {0x23F0, 0x23F0}, {0x23F3, 0x23F3},
        {0x25FD, 0x25FE}, {0x2614, 0x2615}, {0x2648, 0x2653}, {0x267F, 0x267F}, {0x2693, 0x2693}, {0x26A1, 0x26A1},
        {0x26AA, 0x26AB}, {0x26BD, 0x26BE}, {0x26C4, 0x26C5}, {0x26CE, 0x26CE}, {0x26D4, 0x26D4}, {0x26EA, 0x26EA},
        {0x26F2, 0x26F3}, {0x26F5, 0x26F5}, {0x26FA, 0x26FA}, {0x26FD, 0x26FD}, {0x2705, 0x2705}, {0x270A, 0x270B},
        {0x2728, 0x2728}, {0x274C, 0x274C}, {0x274E, 0x274E}, {0x2753, 0x2755}, {0x2757, 0x2757}, {0x2795, 0x2797},
        {0x27B0, 0x27B0}, {0x27BF, 0x27BF}, {0x2B1B, 0x2B1C}, {0x2B50, 0x2B50}, {0x2B55, 0x2B55}, {0x2E80, 0x303E},
        {0x3041, 0x33FF}, {0x3400, 0x4DBF}, {0x4E00, 0x9FFF}, {0xA000, 0xA4CF}, {0xA960, 0xA97F}, {0xAC00, 0xD7A3},
        {0xF900, 0xFAFF}, {0xFE10, 0xFE19}, {0xFE30, 0xFE6F}, {0xFF00, 0xFF60}, {0xFFE0, 0xFFE6}, {0x16FE0, 0x16FE4},
        {0x17000, 0x18AFF}, {0x1B000, 0x1B2FF}, {0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF}, {0x1F18E, 0x1F18E},
        {0x1F191, 0x1F19A}, {0x1F200, 0x1F251}, {0x1F300, 0x1F320}, {0x1F32D, 0x1F335}, {0x1F337, 0x1F37C},
        {0x1F37E, 0x1F393}, {0x1F3A0, 0x1F3CA}, {0x1F3CF, 0x1F3D3}, {0x1F3E0, 0x1F3F0}, {0x1F3F4, 0x1F3F4},
        {0x1F3F8, 0x1F43E}, {0x1F440, 0x1F440}, {0x1F442, 0x1F4FC}, {0x1F4FF, 0x1F53D}, {0x1F54B, 0x1F54E},
        {0x1F550, 0x1F567}, {0x1F57A, 0x1F57A}, {0x1F595, 0x1F596}, {0x1F5A4, 0x1F5A4}, {0x1F5FB, 0x1F64F},
        {0x1F680, 0x1F6C5}, {0x1F6CC, 0x1F6CC}, {0x1F6D0, 0x1F6D2}, {0x1F6D5, 0x1F6D7}, {0x1F6EB, 0x1F6EC},
        {0x1F6F4, 0x1F6FC}, {0x1F7E0, 0x1F7EB}, {0x1F90C, 0x1F93A}, {0x1F93C, 0x1F945}, {0x1F947, 0x1F9FF},
        {0x1FA70, 0x1FAFF}, {0x20000, 0x2FFFD}, {0x30000, 0x3FFFD},
    };
    if (cp < 0x300) return cp >= 0x20 ? 1 : 0;
    if (inRanges(cp, kZero, std::size(kZero))) return 0;
    if (inRanges(cp, kWideRanges, std::size(kWideRanges))) return 2;
    return 1;
}

// --- construction, sizes ---------------------------------------------------------------------------

Screen::Screen(int cols, int rows, int scrollbackLimit)
    : m_cols(std::max(1, cols)), m_rows(std::max(1, rows)), m_scrollbackLimit(scrollbackLimit) {
    m_lines.assign(static_cast<size_t>(m_rows), blankLine());
    m_altLines.assign(static_cast<size_t>(m_rows), blankLine());
    m_top = 0;
    m_bottom = m_rows - 1;
    m_tabs.assign(static_cast<size_t>(m_cols), false);
    for (int i = 8; i < m_cols; i += 8) m_tabs[static_cast<size_t>(i)] = true;
}

Cell Screen::blankCell() const {
    Cell c;
    c.bg = m_cursor.pen.bg;  // erased cells take the current background (BCE), like xterm
    return c;
}

Line Screen::blankLine() const {
    Line l;
    l.cells.assign(static_cast<size_t>(m_cols), Cell{});
    return l;
}

const Line& Screen::lineAt(int absolute) const {
    if (absolute < scrollbackSize()) return m_scrollback[static_cast<size_t>(absolute)];
    return line(std::clamp(absolute - scrollbackSize(), 0, m_rows - 1));
}

void Screen::clearScrollback() {
    m_scrollback.clear();
    touch();
}

void Screen::setScrollbackLimit(int lines) {
    m_scrollbackLimit = std::max(0, lines);
    while (static_cast<int>(m_scrollback.size()) > m_scrollbackLimit) {
        m_scrollback.pop_front();
        ++m_dropped;
    }
    touch();
}

void Screen::resize(int cols, int rows) {
    cols = std::max(2, cols);
    rows = std::max(1, rows);
    if (cols == m_cols && rows == m_rows) return;
    // The alternate screen belongs to full-screen programs that redraw on SIGWINCH: just crop or pad it.
    for (auto& l : m_altLines) l.cells.resize(static_cast<size_t>(cols), Cell{});
    m_altLines.resize(static_cast<size_t>(rows), Line{std::vector<Cell>(static_cast<size_t>(cols)), false});
    // The main screen and its scrollback are re-wrapped to the new width, like macOS Terminal.
    m_rows = rows;
    if (m_alt) {
        // The main screen's cursor is the one saved when the program switched to the alternate screen.
        Cursor current = m_cursor;
        m_cursor = m_saved[0];
        reflow(cols);
        m_saved[0] = m_cursor;
        m_cursor = current;
    } else {
        reflow(cols);
    }
    m_cols = cols;
    m_tabs.assign(static_cast<size_t>(m_cols), false);
    for (int i = 8; i < m_cols; i += 8) m_tabs[static_cast<size_t>(i)] = true;
    m_top = 0;
    m_bottom = m_rows - 1;
    clampCursor();
    for (auto& s : m_saved) {
        s.x = std::clamp(s.x, 0, m_cols - 1);
        s.y = std::clamp(s.y, 0, m_rows - 1);
    }
    touch();
}

void Screen::reflow(int newCols) {
    const auto isBlank = [](const Cell& c) { return c == Cell{}; };
    // 1. Join soft-wrapped rows into logical lines, remembering where the cursor is.
    struct Logical {
        std::vector<Cell> cells;
        bool hasCursor = false;
        size_t cursorOffset = 0;
    };
    std::vector<Logical> logical;
    Logical current;
    auto take = [&](Line& l, bool cursorHere, int cursorX) {
        if (cursorHere) {
            current.hasCursor = true;
            current.cursorOffset = current.cells.size() + static_cast<size_t>(cursorX);
        }
        current.cells.insert(current.cells.end(), l.cells.begin(), l.cells.end());
        if (!l.wrapped) {
            logical.push_back(std::move(current));
            current = Logical{};
        }
    };
    for (auto& l : m_scrollback) take(l, false, 0);
    for (int y = 0; y < static_cast<int>(m_lines.size()); ++y) take(m_lines[static_cast<size_t>(y)], y == m_cursor.y, m_cursor.x);
    if (!current.cells.empty() || current.hasCursor) logical.push_back(std::move(current));

    // 2. Trim trailing blanks (but never past the cursor), and drop empty lines below the cursor.
    size_t cursorLine = 0;
    for (size_t i = 0; i < logical.size(); ++i) {
        auto& c = logical[i].cells;
        size_t keep = c.size();
        while (keep > 0 && isBlank(c[keep - 1])) --keep;
        if (logical[i].hasCursor) {
            keep = std::max(keep, logical[i].cursorOffset);
            cursorLine = i;
        }
        c.resize(keep);
    }
    while (logical.size() > cursorLine + 1 && logical.back().cells.empty()) logical.pop_back();

    // 3. Re-split at the new width.
    std::vector<Line> lines;
    int cursorRow = 0;
    int cursorCol = 0;
    const auto width = static_cast<size_t>(newCols);
    for (auto& lg : logical) {
        size_t pos = 0;
        bool first = true;
        while (first || pos < lg.cells.size()) {
            first = false;
            size_t end = std::min(lg.cells.size(), pos + width);
            // Don't split a wide character from its spacer.
            if (end < lg.cells.size() && end > pos + 1 && (lg.cells[end - 1].attrs() & kWide)) --end;
            Line out;
            out.cells.assign(lg.cells.begin() + static_cast<std::ptrdiff_t>(pos), lg.cells.begin() + static_cast<std::ptrdiff_t>(end));
            out.cells.resize(width, Cell{});
            out.wrapped = end < lg.cells.size();
            if (lg.hasCursor && lg.cursorOffset >= pos && (lg.cursorOffset < end || !out.wrapped)) {
                cursorRow = static_cast<int>(lines.size());
                cursorCol = static_cast<int>(std::min(lg.cursorOffset - pos, width - 1));
                lg.hasCursor = false;
            }
            lines.push_back(std::move(out));
            pos = end;
        }
    }

    // 4. The last rows (ending at least at the cursor) form the screen; everything above is scrollback.
    const int total = static_cast<int>(lines.size());
    int screenTop = std::max(0, std::max(total, cursorRow + 1) - m_rows);
    screenTop = std::min(screenTop, cursorRow);
    m_scrollback.clear();
    for (int i = 0; i < screenTop; ++i) {
        Line& l = lines[static_cast<size_t>(i)];
        while (!l.cells.empty() && isBlank(l.cells.back()) && !l.wrapped) l.cells.pop_back();
        m_scrollback.push_back(std::move(l));
    }
    while (static_cast<int>(m_scrollback.size()) > m_scrollbackLimit) m_scrollback.pop_front();
    m_lines.clear();
    for (int i = screenTop; i < total && static_cast<int>(m_lines.size()) < m_rows; ++i) m_lines.push_back(std::move(lines[static_cast<size_t>(i)]));
    while (static_cast<int>(m_lines.size()) < m_rows) m_lines.push_back(Line{std::vector<Cell>(width), false});
    m_cursor.y = std::clamp(cursorRow - screenTop, 0, m_rows - 1);
    m_cursor.x = cursorCol;
    m_cursor.pendingWrap = false;
}

// --- parser ---------------------------------------------------------------------------------------------

void Screen::feed(std::string_view bytes) {
    for (unsigned char c : bytes) byte(c);
    touch();
}

int Screen::param(size_t i, int fallback) const {
    if (i >= m_params.size() || m_params[i] < 0) return fallback;
    return m_params[i];
}

void Screen::byte(unsigned char c) {
    // CAN and SUB abort any sequence; ESC restarts one (except where it may start ST).
    if (m_state != State::Ground && m_state != State::Osc && m_state != State::Dcs && m_state != State::Ignore) {
        if (c == 0x18 || c == 0x1A) {
            m_state = State::Ground;
            return;
        }
    }
    switch (m_state) {
        case State::Ground:
            if (m_utf8Left > 0) {
                if ((c & 0xC0) == 0x80) {
                    m_utf8 = (m_utf8 << 6) | (c & 0x3F);
                    if (--m_utf8Left == 0) print(m_utf8 > 0x10FFFF ? kReplacement : m_utf8);
                    return;
                }
                m_utf8Left = 0;
                print(kReplacement);  // truncated sequence; fall through to handle c
            }
            if (c < 0x20 || c == 0x7F) {
                if (c == 0x1B) {
                    m_state = State::Escape;
                    m_intermediate.clear();
                } else {
                    control(c);
                }
            } else if (c < 0x80) {
                print(c);
            } else if ((c & 0xE0) == 0xC0) {
                m_utf8 = c & 0x1F;
                m_utf8Left = 1;
            } else if ((c & 0xF0) == 0xE0) {
                m_utf8 = c & 0x0F;
                m_utf8Left = 2;
            } else if ((c & 0xF8) == 0xF0) {
                m_utf8 = c & 0x07;
                m_utf8Left = 3;
            } else {
                print(kReplacement);
            }
            return;

        case State::Escape:
            if (c < 0x20) {
                if (c == 0x1B) return;
                control(c);
                return;
            }
            if (c == '[') {
                m_state = State::Csi;
                m_params.clear();
                m_subparam.clear();
                m_paramStarted = false;
                m_private = 0;
                m_intermediate.clear();
            } else if (c == ']') {
                m_state = State::Osc;
                m_osc.clear();
            } else if (c == 'P') {
                m_state = State::Dcs;
            } else if (c == 'X' || c == '^' || c == '_') {
                m_state = State::Ignore;
            } else if (c >= 0x20 && c <= 0x2F) {
                m_intermediate.push_back(static_cast<char>(c));
                m_state = State::EscapeIntermediate;
            } else {
                m_state = State::Ground;
                escape(c);
            }
            return;

        case State::EscapeIntermediate:
            if (c < 0x20) {
                control(c);
            } else if (c <= 0x2F) {
                m_intermediate.push_back(static_cast<char>(c));
            } else {
                m_state = State::Ground;
                escape(c);
            }
            return;

        case State::Csi:
            if (c < 0x20) {
                if (c == 0x1B) {
                    m_state = State::Escape;
                    m_intermediate.clear();
                } else {
                    control(c);
                }
                return;
            }
            if (c >= '0' && c <= '9') {
                if (!m_paramStarted) {
                    m_params.push_back(0);
                    m_subparam.push_back(false);
                    m_paramStarted = true;
                }
                int& p = m_params.back();
                p = p < 0 ? (c - '0') : std::min(p * 10 + (c - '0'), 99999);
            } else if (c == ';' || c == ':') {
                if (!m_paramStarted) {
                    m_params.push_back(-1);
                    m_subparam.push_back(false);
                }
                m_params.push_back(-1);
                m_subparam.push_back(c == ':');
                m_paramStarted = true;
            } else if (c >= '<' && c <= '?') {
                m_private = static_cast<char>(c);
            } else if (c >= 0x20 && c <= 0x2F) {
                m_intermediate.push_back(static_cast<char>(c));
            } else if (c >= 0x40 && c <= 0x7E) {
                m_state = State::Ground;
                if (m_params.size() > 32) m_params.resize(32);
                csi(c);
            }
            return;

        case State::Osc:
            if (c == 0x07) {
                m_state = State::Ground;
                osc();
            } else if (c == 0x1B) {
                m_state = State::OscEscape;
            } else if (m_osc.size() < (1u << 20)) {
                m_osc.push_back(static_cast<char>(c));
            }
            return;
        case State::OscEscape:
            m_state = State::Ground;
            if (c == '\\') {
                osc();
            } else {
                byte(0x1B);  // not ST: the ESC starts a new sequence
                byte(c);
            }
            return;

        case State::Dcs:
        case State::Ignore:
            if (c == 0x1B) m_state = m_state == State::Dcs ? State::DcsEscape : State::IgnoreEscape;
            else if (c == 0x07 && m_state == State::Ignore) m_state = State::Ground;
            return;
        case State::DcsEscape:
        case State::IgnoreEscape:
            m_state = c == '\\' ? State::Ground : (m_state == State::DcsEscape ? State::Dcs : State::Ignore);
            return;
    }
}

void Screen::control(unsigned char c) {
    switch (c) {
        case 0x07:
            if (onBell) onBell();
            break;
        case 0x08:  // BS
            if (m_cursor.x > 0) --m_cursor.x;
            m_cursor.pendingWrap = false;
            break;
        case 0x09: {  // HT
            int x = m_cursor.x + 1;
            while (x < m_cols - 1 && !m_tabs[static_cast<size_t>(x)]) ++x;
            m_cursor.x = std::min(x, m_cols - 1);
            m_cursor.pendingWrap = false;
            break;
        }
        case 0x0A:
        case 0x0B:
        case 0x0C:
            lineFeed();
            if (m_newlineMode) carriageReturn();
            break;
        case 0x0D: carriageReturn(); break;
        case 0x0E: m_cursor.gl = 1; break;  // SO
        case 0x0F: m_cursor.gl = 0; break;  // SI
        default: break;
    }
}

void Screen::print(char32_t cp) {
    if (m_cursor.charset[m_cursor.gl] == 1) cp = decGraphics(cp);
    const int w = cellWidth(cp);
    if (w == 0) return;  // combining marks are not composed (fonts rarely need them in a terminal)
    auto& lines = active();
    if (m_cursor.pendingWrap && m_autowrap) {
        lines[static_cast<size_t>(m_cursor.y)].wrapped = true;
        carriageReturn();
        lineFeed();
    }
    if (w == 2 && m_cursor.x == m_cols - 1) {
        if (!m_autowrap) return;
        // A wide character doesn't fit in the last column: leave it blank and wrap.
        eraseCells(m_cursor.y, m_cursor.x, m_cursor.x + 1);
        lines[static_cast<size_t>(m_cursor.y)].wrapped = true;
        carriageReturn();
        lineFeed();
    }
    auto& cells = lines[static_cast<size_t>(m_cursor.y)].cells;
    const auto x = static_cast<size_t>(m_cursor.x);
    if (m_insert) {
        cells.insert(cells.begin() + static_cast<std::ptrdiff_t>(x), static_cast<size_t>(w), blankCell());
        cells.resize(static_cast<size_t>(m_cols));
    }
    // Overwriting half of a wide character clears the other half.
    if (cells[x].attrs() & kWideSpacer && x > 0) cells[x - 1] = blankCell();
    const size_t last = x + static_cast<size_t>(w) - 1;
    if ((cells[last].attrs() & kWide) && last + 1 < cells.size()) cells[last + 1] = blankCell();

    Cell& cell = cells[x];
    cell.set(cp, static_cast<std::uint16_t>(m_cursor.pen.attrs | (w == 2 ? kWide : 0)));
    cell.fg = m_cursor.pen.fg;
    cell.bg = m_cursor.pen.bg;
    if (w == 2) {
        Cell& spacer = cells[x + 1];
        spacer.set(' ', static_cast<std::uint16_t>(m_cursor.pen.attrs | kWideSpacer));
        spacer.fg = m_cursor.pen.fg;
        spacer.bg = m_cursor.pen.bg;
    }
    m_lastPrinted = cp;
    m_cursor.x += w;
    if (m_cursor.x >= m_cols) {
        m_cursor.x = m_cols - 1;
        m_cursor.pendingWrap = m_autowrap;
    }
}

// --- cursor and scrolling ---------------------------------------------------------------------------

void Screen::carriageReturn() {
    m_cursor.x = 0;
    m_cursor.pendingWrap = false;
}

void Screen::lineFeed() {
    m_cursor.pendingWrap = false;
    if (m_cursor.y == m_bottom) scrollUp(m_top, m_bottom, 1);
    else if (m_cursor.y < m_rows - 1) ++m_cursor.y;
}

void Screen::reverseIndex() {
    m_cursor.pendingWrap = false;
    if (m_cursor.y == m_top) scrollDown(m_top, m_bottom, 1);
    else if (m_cursor.y > 0) --m_cursor.y;
}

void Screen::scrollUp(int top, int bottom, int n) {
    auto& lines = active();
    n = std::clamp(n, 0, bottom - top + 1);
    for (int i = 0; i < n; ++i) {
        Line& gone = lines[static_cast<size_t>(top + i)];
        // Lines leaving the top of the full main screen go to the scrollback.
        if (!m_alt && top == 0 && m_scrollbackLimit > 0) {
            Line keep = std::move(gone);
            while (!keep.cells.empty() && keep.cells.back() == Cell{} && !keep.wrapped) keep.cells.pop_back();
            keep.cells.shrink_to_fit();
            m_scrollback.push_back(std::move(keep));
            if (static_cast<int>(m_scrollback.size()) > m_scrollbackLimit) {
                m_scrollback.pop_front();
                ++m_dropped;
            }
        }
        gone = blankLine();
        for (auto& c : gone.cells) c = blankCell();
    }
    std::rotate(lines.begin() + top, lines.begin() + top + n, lines.begin() + bottom + 1);
}

void Screen::scrollDown(int top, int bottom, int n) {
    auto& lines = active();
    n = std::clamp(n, 0, bottom - top + 1);
    std::rotate(lines.begin() + top, lines.begin() + bottom + 1 - n, lines.begin() + bottom + 1);
    for (int i = 0; i < n; ++i) {
        Line& l = lines[static_cast<size_t>(top + i)];
        l = blankLine();
        for (auto& c : l.cells) c = blankCell();
    }
}

void Screen::eraseCells(int y, int from, int to) {
    auto& cells = active()[static_cast<size_t>(y)].cells;
    from = std::clamp(from, 0, m_cols);
    to = std::clamp(to, 0, m_cols);
    for (int x = from; x < to; ++x) cells[static_cast<size_t>(x)] = blankCell();
    if (to >= m_cols) active()[static_cast<size_t>(y)].wrapped = false;
}

void Screen::moveTo(int x, int y) {
    const int top = m_cursor.originMode ? m_top : 0;
    const int bottom = m_cursor.originMode ? m_bottom : m_rows - 1;
    m_cursor.x = std::clamp(x, 0, m_cols - 1);
    m_cursor.y = std::clamp(y + top, top, bottom);
    m_cursor.pendingWrap = false;
}

void Screen::clampCursor() {
    m_cursor.x = std::clamp(m_cursor.x, 0, m_cols - 1);
    m_cursor.y = std::clamp(m_cursor.y, 0, m_rows - 1);
}

void Screen::saveCursor() { m_saved[m_alt ? 1 : 0] = m_cursor; }

void Screen::restoreCursor() {
    m_cursor = m_saved[m_alt ? 1 : 0];
    clampCursor();
}

void Screen::switchScreen(bool alt, bool saveRestore, bool clear) {
    if (alt == m_alt) return;
    if (alt) {
        if (saveRestore) saveCursor();
        m_alt = true;
        if (clear) {
            for (auto& l : m_altLines) {
                l = blankLine();
            }
        }
    } else {
        if (clear) {
            for (auto& l : m_altLines) l = blankLine();
        }
        m_alt = false;
        if (saveRestore) restoreCursor();
    }
    m_top = 0;
    m_bottom = m_rows - 1;
}

void Screen::reset() {
    const int cols = m_cols;
    const int rows = m_rows;
    const int limit = m_scrollbackLimit;
    auto respondFn = std::move(respond);
    auto bell = std::move(onBell);
    auto titleFn = std::move(onTitle);
    auto clip = std::move(onClipboard);
    auto query = std::move(queryColor);
    auto palette = std::move(onSetPaletteColor);
    auto scrollback = std::move(m_scrollback);
    *this = Screen(cols, rows, limit);
    m_scrollback = std::move(scrollback);
    respond = std::move(respondFn);
    onBell = std::move(bell);
    onTitle = std::move(titleFn);
    onClipboard = std::move(clip);
    queryColor = std::move(query);
    onSetPaletteColor = std::move(palette);
}

// --- sequences ------------------------------------------------------------------------------------------

void Screen::escape(unsigned char final) {
    if (!m_intermediate.empty()) {
        const char i = m_intermediate[0];
        if (i == '(' || i == ')') m_cursor.charset[i == '(' ? 0 : 1] = final == '0' ? 1 : 0;
        else if (i == '#' && final == '8') {  // DECALN: fill with E
            for (auto& l : active()) {
                for (auto& c : l.cells) c.set('E', 0), c.fg = kDefaultColor, c.bg = kDefaultColor;
            }
        }
        return;
    }
    switch (final) {
        case '7': saveCursor(); break;
        case '8': restoreCursor(); break;
        case 'D': lineFeed(); break;
        case 'E':
            lineFeed();
            carriageReturn();
            break;
        case 'H': m_tabs[static_cast<size_t>(m_cursor.x)] = true; break;
        case 'M': reverseIndex(); break;
        case 'c': reset(); break;
        case '=': m_appKeypad = true; break;
        case '>': m_appKeypad = false; break;
        default: break;
    }
}

void Screen::csi(unsigned char final) {
    const int p0 = param(0, 0);
    const int n = std::max(1, p0);  // counts: 0 and missing mean 1
    const std::string_view inter = m_intermediate;
    if (m_private == '?' && inter.empty() && (final == 'h' || final == 'l')) {
        for (size_t i = 0; i < std::max<size_t>(1, m_params.size()); ++i) setMode(param(i, 0), true, final == 'h');
        return;
    }
    if (m_private == '?' && inter == "$" && final == 'p') {
        reportMode(p0, true);
        return;
    }
    if (m_private == '>' && final == 'c') {
        if (respond) respond("\x1b[>1;10;0c");
        return;
    }
    if (m_private == '>' && final == 'q') {
        if (respond) respond("\x1bP>|Mayonnaise Terminal\x1b\\");
        return;
    }
    if (m_private != 0 && m_private != '?') return;  // kitty keyboard protocol, XTMODKEYS...: not supported
    if (inter == " " && final == 'q') {  // DECSCUSR
        m_cursorStyleSet = p0 != 0;  // 0 = back to the user's default
        switch (p0) {
            case 0:
            case 1: m_cursorShape = CursorShape::Block; m_cursorBlink = true; break;
            case 2: m_cursorShape = CursorShape::Block; m_cursorBlink = false; break;
            case 3: m_cursorShape = CursorShape::Underline; m_cursorBlink = true; break;
            case 4: m_cursorShape = CursorShape::Underline; m_cursorBlink = false; break;
            case 5: m_cursorShape = CursorShape::Bar; m_cursorBlink = true; break;
            case 6: m_cursorShape = CursorShape::Bar; m_cursorBlink = false; break;
            default: break;
        }
        return;
    }
    if (inter == "!" && final == 'p') {  // DECSTR soft reset
        m_cursor.pen = Pen{};
        m_cursor.originMode = false;
        m_cursor.charset[0] = m_cursor.charset[1] = 0;
        m_cursor.gl = 0;
        m_autowrap = true;
        m_insert = false;
        m_cursorVisible = true;
        m_appCursor = false;
        m_appKeypad = false;
        m_top = 0;
        m_bottom = m_rows - 1;
        return;
    }
    if (inter == "$" && final == 'p') {
        reportMode(p0, false);
        return;
    }
    if (!inter.empty()) return;

    auto& lines = active();
    auto& cells = lines[static_cast<size_t>(m_cursor.y)].cells;
    switch (final) {
        case '@': {  // ICH
            const auto x = static_cast<size_t>(m_cursor.x);
            const size_t k = std::min(static_cast<size_t>(n), cells.size() - x);
            cells.insert(cells.begin() + static_cast<std::ptrdiff_t>(x), k, blankCell());
            cells.resize(static_cast<size_t>(m_cols));
            m_cursor.pendingWrap = false;
            break;
        }
        case 'A': m_cursor.y = std::max(m_cursor.y >= m_top ? m_top : 0, m_cursor.y - n), m_cursor.pendingWrap = false; break;
        case 'B':
        case 'e': m_cursor.y = std::min(m_cursor.y <= m_bottom ? m_bottom : m_rows - 1, m_cursor.y + n), m_cursor.pendingWrap = false; break;
        case 'C':
        case 'a': m_cursor.x = std::min(m_cols - 1, m_cursor.x + n), m_cursor.pendingWrap = false; break;
        case 'D': m_cursor.x = std::max(0, m_cursor.x - n), m_cursor.pendingWrap = false; break;
        case 'E':
            m_cursor.y = std::min(m_bottom, m_cursor.y + n);
            carriageReturn();
            break;
        case 'F':
            m_cursor.y = std::max(m_top, m_cursor.y - n);
            carriageReturn();
            break;
        case 'G':
        case '`': m_cursor.x = std::clamp(n - 1, 0, m_cols - 1), m_cursor.pendingWrap = false; break;
        case 'H':
        case 'f': moveTo(std::max(1, param(1, 1)) - 1, n - 1); break;
        case 'I':
            for (int i = 0; i < n; ++i) control(0x09);
            break;
        case 'J':
            if (p0 == 0) {
                eraseCells(m_cursor.y, m_cursor.x, m_cols);
                for (int y = m_cursor.y + 1; y < m_rows; ++y) eraseCells(y, 0, m_cols);
            } else if (p0 == 1) {
                for (int y = 0; y < m_cursor.y; ++y) eraseCells(y, 0, m_cols);
                eraseCells(m_cursor.y, 0, m_cursor.x + 1);
            } else if (p0 == 2) {
                for (int y = 0; y < m_rows; ++y) eraseCells(y, 0, m_cols);
            } else if (p0 == 3) {
                m_scrollback.clear();
            }
            break;
        case 'K':
            if (p0 == 0) eraseCells(m_cursor.y, m_cursor.x, m_cols);
            else if (p0 == 1) eraseCells(m_cursor.y, 0, m_cursor.x + 1);
            else if (p0 == 2) eraseCells(m_cursor.y, 0, m_cols);
            break;
        case 'L':
            if (m_cursor.y >= m_top && m_cursor.y <= m_bottom) scrollDown(m_cursor.y, m_bottom, n), carriageReturn();
            break;
        case 'M':
            if (m_cursor.y >= m_top && m_cursor.y <= m_bottom) {
                // Deleted lines never go to the scrollback.
                const int limit = m_scrollbackLimit;
                m_scrollbackLimit = 0;
                scrollUp(m_cursor.y, m_bottom, n);
                m_scrollbackLimit = limit;
                carriageReturn();
            }
            break;
        case 'P': {  // DCH
            const auto x = static_cast<size_t>(m_cursor.x);
            const size_t k = std::min(static_cast<size_t>(n), cells.size() - x);
            cells.erase(cells.begin() + static_cast<std::ptrdiff_t>(x), cells.begin() + static_cast<std::ptrdiff_t>(x + k));
            cells.resize(static_cast<size_t>(m_cols), blankCell());
            m_cursor.pendingWrap = false;
            break;
        }
        case 'S': {
            const int limit = m_scrollbackLimit;
            m_scrollbackLimit = 0;  // SU from programs scrolls the region, it doesn't feed history
            scrollUp(m_top, m_bottom, n);
            m_scrollbackLimit = limit;
            break;
        }
        case 'T':
            if (m_params.size() <= 1) scrollDown(m_top, m_bottom, n);
            break;
        case 'X': eraseCells(m_cursor.y, m_cursor.x, m_cursor.x + n); break;
        case 'Z':
            for (int i = 0; i < n && m_cursor.x > 0; ++i) {
                do {
                    --m_cursor.x;
                } while (m_cursor.x > 0 && !m_tabs[static_cast<size_t>(m_cursor.x)]);
            }
            break;
        case 'b':  // REP
            for (int i = 0; i < std::min(n, 65535); ++i) print(m_lastPrinted);
            break;
        case 'c':
            if (p0 == 0 && respond) respond("\x1b[?62;22c");
            break;
        case 'd': m_cursor.y = std::clamp(n - 1 + (m_cursor.originMode ? m_top : 0), 0, m_rows - 1), m_cursor.pendingWrap = false; break;
        case 'g':
            if (p0 == 0) m_tabs[static_cast<size_t>(m_cursor.x)] = false;
            else if (p0 == 3) std::fill(m_tabs.begin(), m_tabs.end(), false);
            break;
        case 'h':
        case 'l':
            for (size_t i = 0; i < std::max<size_t>(1, m_params.size()); ++i) setMode(param(i, 0), false, final == 'h');
            break;
        case 'm': sgr(); break;
        case 'n':
            if (!respond) break;
            if (p0 == 5) {
                respond("\x1b[0n");
            } else if (p0 == 6) {
                const int row = m_cursor.y - (m_cursor.originMode ? m_top : 0) + 1;
                respond("\x1b[" + std::string(m_private == '?' ? "?" : "") + std::to_string(row) + ";" + std::to_string(m_cursor.x + 1) + "R");
            }
            break;
        case 'r': {
            const int top = std::max(1, param(0, 1)) - 1;
            const int bottom = std::min(m_rows, param(1, m_rows) <= 0 ? m_rows : param(1, m_rows)) - 1;
            if (top < bottom) {
                m_top = top;
                m_bottom = bottom;
                moveTo(0, 0);
            }
            break;
        }
        case 's':
            if (m_private == 0) saveCursor();
            break;
        case 'u':
            if (m_private == 0) restoreCursor();
            break;
        case 't':
            if (p0 == 18 && respond) respond("\x1b[8;" + std::to_string(m_rows) + ";" + std::to_string(m_cols) + "t");
            break;
        default: break;
    }
}

void Screen::setMode(int mode, bool priv, bool on) {
    if (!priv) {
        if (mode == 4) m_insert = on;
        else if (mode == 20) m_newlineMode = on;
        return;
    }
    switch (mode) {
        case 1: m_appCursor = on; break;
        case 5: m_reverseVideo = on; break;
        case 6:
            m_cursor.originMode = on;
            moveTo(0, 0);
            break;
        case 7: m_autowrap = on; break;
        case 12: m_cursorBlink = on; break;
        case 25: m_cursorVisible = on; break;
        case 47: switchScreen(on, false, false); break;
        case 66: m_appKeypad = on; break;
        case 1000: m_mouse = on ? MouseMode::Click : MouseMode::None; break;
        case 1002: m_mouse = on ? MouseMode::Drag : MouseMode::None; break;
        case 1003: m_mouse = on ? MouseMode::Motion : MouseMode::None; break;
        case 1004: m_focusEvents = on; break;
        case 1006: m_sgrMouse = on; break;
        case 1047: switchScreen(on, false, !on); break;
        case 1048: on ? saveCursor() : restoreCursor(); break;
        case 1049: switchScreen(on, true, true); break;
        case 2004: m_bracketedPaste = on; break;
        default: break;
    }
}

void Screen::reportMode(int mode, bool priv) {
    if (!respond) return;
    int state = 0;  // not recognized
    auto flag = [&](bool v) { state = v ? 1 : 2; };
    if (priv) {
        switch (mode) {
            case 1: flag(m_appCursor); break;
            case 5: flag(m_reverseVideo); break;
            case 6: flag(m_cursor.originMode); break;
            case 7: flag(m_autowrap); break;
            case 12: flag(m_cursorBlink); break;
            case 25: flag(m_cursorVisible); break;
            case 1000: flag(m_mouse == MouseMode::Click); break;
            case 1002: flag(m_mouse == MouseMode::Drag); break;
            case 1003: flag(m_mouse == MouseMode::Motion); break;
            case 1004: flag(m_focusEvents); break;
            case 1006: flag(m_sgrMouse); break;
            case 47:
            case 1047:
            case 1049: flag(m_alt); break;
            case 2004: flag(m_bracketedPaste); break;
            default: break;
        }
    } else if (mode == 4) {
        flag(m_insert);
    } else if (mode == 20) {
        flag(m_newlineMode);
    }
    respond("\x1b[" + std::string(priv ? "?" : "") + std::to_string(mode) + ";" + std::to_string(state) + "$y");
}

void Screen::sgr() {
    Pen& pen = m_cursor.pen;
    if (m_params.empty()) {
        pen = Pen{};
        return;
    }
    for (size_t i = 0; i < m_params.size(); ++i) {
        const int p = m_params[i] < 0 ? 0 : m_params[i];
        // Extended colours: 38;5;n / 38;2;r;g;b and the colon forms 38:5:n / 38:2:[cs]:r:g:b.
        auto extended = [&](ColorRef& target) {
            size_t j = i + 1;
            const bool colon = j < m_params.size() && m_subparam[j];
            const int kind = param(j, -1);
            if (kind == 5) {
                if (j + 1 < m_params.size()) target = paletteColor(param(j + 1, 0));
                i = j + 1;
            } else if (kind == 2) {
                size_t first = j + 1;
                if (colon) {
                    size_t count = 0;
                    while (first + count < m_params.size() && m_subparam[first + count]) ++count;
                    if (count >= 4) ++first;  // colour space id
                }
                if (first + 2 < m_params.size()) target = rgbColor(param(first, 0), param(first + 1, 0), param(first + 2, 0));
                i = std::min(first + 2, m_params.size() - 1);
            } else if (colon) {
                while (i + 1 < m_params.size() && m_subparam[i + 1]) ++i;
            }
        };
        switch (p) {
            case 0: pen = Pen{}; break;
            case 1: pen.attrs |= kBold; break;
            case 2: pen.attrs |= kDim; break;
            case 3: pen.attrs |= kItalic; break;
            case 4: {
                // 4:0 none, 4:2 double, other styles (curly, dotted) shown as single.
                int style = i + 1 < m_params.size() && m_subparam[i + 1] ? param(++i, 1) : 1;
                pen.attrs &= static_cast<std::uint16_t>(~(kUnderline | kDoubleUnderline));
                if (style == 2) pen.attrs |= kDoubleUnderline;
                else if (style != 0) pen.attrs |= kUnderline;
                break;
            }
            case 5:
            case 6: pen.attrs |= kBlink; break;
            case 7: pen.attrs |= kInverse; break;
            case 8: pen.attrs |= kHidden; break;
            case 9: pen.attrs |= kStrike; break;
            case 21: pen.attrs |= kDoubleUnderline; break;
            case 22: pen.attrs &= static_cast<std::uint16_t>(~(kBold | kDim)); break;
            case 23: pen.attrs &= static_cast<std::uint16_t>(~kItalic); break;
            case 24: pen.attrs &= static_cast<std::uint16_t>(~(kUnderline | kDoubleUnderline)); break;
            case 25: pen.attrs &= static_cast<std::uint16_t>(~kBlink); break;
            case 27: pen.attrs &= static_cast<std::uint16_t>(~kInverse); break;
            case 28: pen.attrs &= static_cast<std::uint16_t>(~kHidden); break;
            case 29: pen.attrs &= static_cast<std::uint16_t>(~kStrike); break;
            case 38: extended(pen.fg); break;
            case 39: pen.fg = kDefaultColor; break;
            case 48: extended(pen.bg); break;
            case 49: pen.bg = kDefaultColor; break;
            case 58: {  // underline colour: parsed and ignored
                ColorRef ignored = 0;
                extended(ignored);
                break;
            }
            default:
                if (p >= 30 && p <= 37) pen.fg = paletteColor(p - 30);
                else if (p >= 40 && p <= 47) pen.bg = paletteColor(p - 40);
                else if (p >= 90 && p <= 97) pen.fg = paletteColor(p - 90 + 8);
                else if (p >= 100 && p <= 107) pen.bg = paletteColor(p - 100 + 8);
                break;
        }
    }
}

void Screen::osc() {
    const size_t semi = m_osc.find(';');
    const std::string code = m_osc.substr(0, semi);
    const std::string arg = semi == std::string::npos ? std::string() : m_osc.substr(semi + 1);
    if (code == "0" || code == "2") {
        m_title = arg;
        if (onTitle) onTitle(m_title);
    } else if (code == "7") {
        // file://host/path, percent-encoded.
        std::string path = arg;
        if (path.rfind("file://", 0) == 0) {
            const size_t slash = path.find('/', 7);
            path = slash == std::string::npos ? std::string() : path.substr(slash);
        }
        std::string decoded;
        for (size_t i = 0; i < path.size(); ++i) {
            if (path[i] == '%' && i + 2 < path.size()) {
                decoded.push_back(static_cast<char>(std::strtol(path.substr(i + 1, 2).c_str(), nullptr, 16)));
                i += 2;
            } else {
                decoded.push_back(path[i]);
            }
        }
        m_cwd = decoded;
    } else if (code == "10" || code == "11" || code == "12") {
        if (arg == "?" && queryColor && respond) {
            const int which = code == "10" ? -1 : code == "11" ? -2 : -3;
            respond("\x1b]" + code + ";" + colorReply(queryColor(which)) + "\x1b\\");
        }
    } else if (code == "4") {
        // 4;index;spec[;index;spec...]
        size_t pos = 0;
        while (pos < arg.size()) {
            size_t a = arg.find(';', pos);
            if (a == std::string::npos) break;
            size_t b = arg.find(';', a + 1);
            const int index = std::atoi(arg.substr(pos, a - pos).c_str());
            const std::string spec = arg.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
            if (spec == "?") {
                if (queryColor && respond) respond("\x1b]4;" + std::to_string(index) + ";" + colorReply(queryColor(index)) + "\x1b\\");
            } else if (std::uint32_t rgb = 0; parseColorSpec(spec, rgb) && onSetPaletteColor) {
                onSetPaletteColor(index, rgb);
            }
            if (b == std::string::npos) break;
            pos = b + 1;
        }
    } else if (code == "104") {
        if (onSetPaletteColor) onSetPaletteColor(-1, 0);  // reset all
    } else if (code == "52") {
        // 52;targets;base64 ("?" queries are refused: programs must not read the clipboard).
        const size_t s2 = arg.find(';');
        if (s2 != std::string::npos && arg.substr(s2 + 1) != "?" && onClipboard) onClipboard(base64Decode(arg.substr(s2 + 1)));
    }
}

}  // namespace term
