#include <doctest/doctest.h>

#include <cmath>

#include "mayonnaise/font.hpp"
#include "render/font.hpp"

using namespace mayo;

TEST_CASE("system monospace family resolves and keeps a fixed advance") {
    auto mono = systemFontFamily("monospace");
    if (!mono) {
        MESSAGE("no fontconfig / monospace font on this machine; skipped");
        return;
    }
    CHECK(*mono != 0);
    CHECK(systemFontFamily("monospace") == mono);  // cached
    Font f{13, FontWeight::Regular, 16, *mono};
    const float m = glyphAdvance('M', f);
    CHECK(m > 4);
    CHECK(glyphAdvance('i', f) == doctest::Approx(m));
    CHECK(glyphAdvance('M', f.withWeight(FontWeight::Bold)) == doctest::Approx(m).epsilon(0.02));
    CHECK(glyphAdvance('M', f.withItalic()) == doctest::Approx(m).epsilon(0.02));
    // Inter's tracking is not applied to other families: the advance scales linearly with size.
    CHECK(glyphAdvance('M', f.withSize(26)) == doctest::Approx(m * 2).epsilon(0.001));
    const LineMetrics lm = fontMetrics(f);
    CHECK(lm.ascender > 0);
    CHECK(lm.descender > 0);
    render::GlyphBitmap bm;
    CHECK(render::FontLibrary::instance().rasterize('A', f.withWeight(FontWeight::Bold).withItalic(), 26, 0, bm));
    CHECK(bm.width > 0);
}

TEST_CASE("unknown families fall back to Inter") {
    Font f{13, FontWeight::Regular, 16, 9999};
    CHECK(glyphAdvance('M', f) == doctest::Approx(glyphAdvance('M', Font{13, FontWeight::Regular, 16})));
}

TEST_CASE("registering a missing file fails") {
    CHECK_FALSE(registerFontFamily({{"/nonexistent/font.ttf"}, {}, {}, {}}).has_value());
}

TEST_CASE("characters missing from a font come from an installed fallback") {
    render::GlyphBitmap bm;
    // Inter has no CJK; with fontconfig and any CJK font installed the glyph is found elsewhere.
    if (!systemFontFamily("sans-serif")) {
        MESSAGE("no fontconfig; skipped");
        return;
    }
    const bool rendered = render::FontLibrary::instance().rasterize(0x4E2D, Font{13}, 26, 0, bm);
    if (!rendered || bm.width == 0) {
        MESSAGE("no CJK font installed; skipped");
        return;
    }
    CHECK(glyphAdvance(0x4E2D, Font{13}) > glyphAdvance('a', Font{13}));  // wide CJK advance, not .notdef
}
