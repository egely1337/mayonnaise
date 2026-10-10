#include "mayonnaise/theme.hpp"


namespace mayo {

namespace {

// Light/dark pairs follow the macOS 26 system palette.
struct Pair {
    Color light;
    Color dark;
    [[nodiscard]] Color pick(bool isDark) const { return isDark ? dark : light; }
};

constexpr Pair kRed{Color::hex(0xFF383C), Color::hex(0xFF4245)};
constexpr Pair kOrange{Color::hex(0xFF8D28), Color::hex(0xFF9230)};
constexpr Pair kYellow{Color::hex(0xFFCC00), Color::hex(0xFFD600)};
constexpr Pair kGreen{Color::hex(0x34C759), Color::hex(0x30D158)};
constexpr Pair kMint{Color::hex(0x00C8B3), Color::hex(0x00DAC3)};
constexpr Pair kTeal{Color::hex(0x00C3D0), Color::hex(0x00D2E0)};
constexpr Pair kCyan{Color::hex(0x00C0E8), Color::hex(0x3CD3FE)};
constexpr Pair kBlue{Color::hex(0x0088FF), Color::hex(0x0091FF)};
constexpr Pair kIndigo{Color::hex(0x6155F5), Color::hex(0x6D7CFF)};
constexpr Pair kPurple{Color::hex(0xCB30E0), Color::hex(0xDB34F2)};
constexpr Pair kPink{Color::hex(0xFF2D55), Color::hex(0xFF375F)};
constexpr Pair kBrown{Color::hex(0xAC7F5E), Color::hex(0xB78A66)};
constexpr Pair kGray{Color::hex(0x8E8E93), Color::hex(0x8E8E93)};

}  // namespace

Font Theme::font(TextStyle style) const {
    switch (style) {
        case TextStyle::LargeTitle: return {26, FontWeight::Bold, 32};
        case TextStyle::Title1: return {22, FontWeight::Bold, 28};
        case TextStyle::Title2: return {17, FontWeight::Semibold, 22};
        case TextStyle::Title3: return {15, FontWeight::Semibold, 20};
        case TextStyle::Headline: return {13, FontWeight::Semibold, 16};
        case TextStyle::Body: return {13, FontWeight::Regular, 16};
        case TextStyle::Callout: return {12, FontWeight::Regular, 15};
        case TextStyle::Subheadline: return {11, FontWeight::Regular, 14};
        case TextStyle::Footnote: return {10, FontWeight::Regular, 13};
        case TextStyle::Caption: return {10, FontWeight::Medium, 13};
    }
    return {};
}

MaterialStyle Theme::material(Material m) const {
    const bool dark = isDark();
    MaterialStyle s;
    s.highlight = dark ? Color::white(0.14f) : Color::white(0.75f);
    s.border = dark ? Color::white(0.10f) : Color::black(0.07f);
    switch (m) {
        case Material::Sidebar:
            s.blurRadius = 30;
            s.tint = dark ? Color::rgb8(44, 44, 48, 0.62f) : Color::rgb8(246, 246, 250, 0.62f);
            break;
        case Material::Titlebar:
            s.blurRadius = 20;
            s.tint = dark ? Color::rgb8(34, 34, 36, 0.72f) : Color::rgb8(250, 250, 252, 0.72f);
            s.highlight = Color::transparent();
            break;
        case Material::Popover:
            s.blurRadius = 30;
            s.tint = dark ? Color::rgb8(46, 46, 50, 0.80f) : Color::rgb8(248, 248, 250, 0.80f);
            break;
        case Material::Menu:
            s.blurRadius = 30;
            s.tint = dark ? Color::rgb8(40, 40, 44, 0.74f) : Color::rgb8(244, 244, 246, 0.74f);
            break;
        case Material::Glass:
            s.blurRadius = 12;
            s.saturation = 1.8f;
            s.tint = dark ? Color::white(0.07f) : Color::white(0.35f);
            s.highlight = dark ? Color::white(0.22f) : Color::white(0.9f);
            break;
    }
    return s;
}

Theme Theme::make(Appearance appearance, Color accent) {
    Theme t;
    t.appearance = appearance;
    const bool dark = appearance == Appearance::Dark;
    ThemeColors& c = t.colors;

    // The default accent has a slightly brighter dark-mode variant, like systemBlue.
    if (accent == kBlue.light && dark) accent = kBlue.dark;
    c.accent = accent;
    c.accentHover = accent.lighter(0.06f);
    c.accentPressed = accent.lighter(-0.14f);
    c.onAccent = Color::white();

    const Color ink = dark ? Color::white() : Color::black();
    c.label = ink.withAlpha(0.85f);
    c.secondaryLabel = ink.withAlpha(dark ? 0.55f : 0.50f);
    c.tertiaryLabel = ink.withAlpha(0.26f);
    c.quaternaryLabel = ink.withAlpha(0.10f);
    c.placeholder = ink.withAlpha(dark ? 0.30f : 0.28f);

    if (dark) {
        c.windowBackground = Color::hex(0x232326);
        c.contentBackground = Color::hex(0x1C1C1E);
        c.controlBackground = Color::white(0.06f);
        c.control = Color::white(0.14f);
        c.controlHover = Color::white(0.18f);
        c.controlPressed = Color::white(0.24f);
        c.controlBorder = Color::white(0.07f);
        c.controlShadow = Color::black(0.35f);
        c.toggleOff = Color::white(0.16f);
        c.knob = Color::hex(0xE8E8EA);
        c.knobBorder = Color::black(0.25f);
        c.shadow = Color::black(0.50f);
        c.windowBorder = Color::black(0.70f);
        c.windowHighlight = Color::white(0.13f);
    } else {
        c.windowBackground = Color::hex(0xF5F5F7);
        c.contentBackground = Color::white();
        c.controlBackground = Color::white();
        c.control = Color::white();
        c.controlHover = Color::white();
        c.controlPressed = Color::hex(0xE4E4E7);
        c.controlBorder = Color::black(0.06f);
        c.controlShadow = Color::black(0.16f);
        c.toggleOff = Color::black(0.09f);
        c.knob = Color::white();
        c.knobBorder = Color::black(0.06f);
        c.shadow = Color::black(0.22f);
        c.windowBorder = Color::black(0.16f);
        c.windowHighlight = Color::white(0.55f);
    }

    c.fill = ink.withAlpha(dark ? 0.12f : 0.09f);
    c.secondaryFill = ink.withAlpha(dark ? 0.08f : 0.06f);
    c.tertiaryFill = ink.withAlpha(0.04f);
    c.separator = ink.withAlpha(dark ? 0.12f : 0.10f);
    c.focusRing = accent.withAlpha(0.5f);
    c.textSelection = accent.withAlpha(dark ? 0.40f : 0.28f);
    c.selection = accent;
    c.unemphasizedSelection = ink.withAlpha(dark ? 0.12f : 0.08f);
    c.hover = ink.withAlpha(dark ? 0.07f : 0.05f);
    c.destructive = kRed.pick(dark);

    c.red = kRed.pick(dark);
    c.orange = kOrange.pick(dark);
    c.yellow = kYellow.pick(dark);
    c.green = kGreen.pick(dark);
    c.mint = kMint.pick(dark);
    c.teal = kTeal.pick(dark);
    c.cyan = kCyan.pick(dark);
    c.blue = kBlue.pick(dark);
    c.indigo = kIndigo.pick(dark);
    c.purple = kPurple.pick(dark);
    c.pink = kPink.pick(dark);
    c.brown = kBrown.pick(dark);
    c.gray = kGray.pick(dark);
    return t;
}

}  // namespace mayo
