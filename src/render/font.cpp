#include "font.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H
#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H
#include FT_TRUETYPE_TABLES_H

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include "../core/utf8.hpp"
#include "embedded.hpp"

namespace mayo::render {

namespace {

constexpr FT_ULong tag(char a, char b, char c, char d) {
    return (static_cast<FT_ULong>(a) << 24) | (static_cast<FT_ULong>(b) << 16) | (static_cast<FT_ULong>(c) << 8) |
           static_cast<FT_ULong>(d);
}

int opticalSize(float size) { return static_cast<int>(std::lround(std::clamp(size, 14.0f, 32.0f))); }

struct FaceDeleter {
    void operator()(FT_Face f) const { FT_Done_Face(f); }
};
using FacePtr = std::unique_ptr<std::remove_pointer_t<FT_Face>, FaceDeleter>;

}  // namespace

struct FontLibrary::Impl {
    /// One FreeType face per (weight, optical size); ~70 KB each for the variable font. The pixel size is
    /// switched on demand instead of keeping a face per size (text at many sizes and scales used to open
    /// dozens of faces).
    struct Face {
        FacePtr face;
        FT_F26Dot6 size = -1;  // current char size in 26.6 pixels; -1 = unset
    };

    FT_Library library = nullptr;
    std::unordered_map<std::uint64_t, Face> faces;
    std::unordered_map<std::uint64_t, float> advances;  // em units
    std::unordered_map<std::uint64_t, FontMetrics> metrics;  // per em
    FT_UInt wghtAxis = ~0u;
    FT_UInt opszAxis = ~0u;
    FT_UInt axisCount = 0;
    FT_UShort unitsPerEm = 2048;
    std::vector<FT_Fixed> defaults;

    Impl() {
        if (FT_Init_FreeType(&library)) {
            std::fprintf(stderr, "mayonnaise: FT_Init_FreeType failed\n");
            std::abort();
        }
        FacePtr probe = openFace(400, 14, false);
        unitsPerEm = probe->units_per_EM;
        FT_MM_Var* mm = nullptr;
        if (FT_Get_MM_Var(probe.get(), &mm) == 0) {
            axisCount = mm->num_axis;
            for (FT_UInt i = 0; i < mm->num_axis; ++i) {
                defaults.push_back(mm->axis[i].def);
                if (mm->axis[i].tag == tag('w', 'g', 'h', 't')) wghtAxis = i;
                if (mm->axis[i].tag == tag('o', 'p', 's', 'z')) opszAxis = i;
            }
            FT_Done_MM_Var(library, mm);
        }
    }

    ~Impl() {
        faces.clear();
        FT_Done_FreeType(library);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    FacePtr openFace(int weight, int opsz, bool applyAxes) const {
        FT_Face face = nullptr;
        const auto& bytes = embedded::InterVariable_ttf;
        if (FT_New_Memory_Face(library, bytes.data(), static_cast<FT_Long>(bytes.size()), 0, &face)) {
            std::fprintf(stderr, "mayonnaise: failed to open embedded Inter font\n");
            std::abort();
        }
        if (applyAxes && axisCount > 0) {
            std::vector<FT_Fixed> coords = defaults;
            if (wghtAxis < axisCount) coords[wghtAxis] = static_cast<FT_Fixed>(weight) << 16;
            if (opszAxis < axisCount) coords[opszAxis] = static_cast<FT_Fixed>(opsz) << 16;
            FT_Set_Var_Design_Coordinates(face, axisCount, coords.data());
        }
        return FacePtr(face);
    }

    static std::uint64_t styleKey(const Font& font) {
        return (static_cast<std::uint64_t>(font.weight) << 8) | static_cast<std::uint64_t>(opticalSize(font.size));
    }

    /// The style's face set to `px64` (26.6 pixels).
    FT_Face face(const Font& font, FT_F26Dot6 px64) {
        auto key = styleKey(font);
        auto it = faces.find(key);
        if (it == faces.end()) {
            it = faces.emplace(key, Face{openFace(static_cast<int>(font.weight), opticalSize(font.size), true)}).first;
        }
        Face& f = it->second;
        if (f.size != px64) {
            FT_Set_Char_Size(f.face.get(), 0, px64, 72, 72);
            f.size = px64;
        }
        return f.face.get();
    }

    /// One pixel per font unit: advances come back as exact (variation-adjusted) design units.
    FT_Face metricFace(const Font& font) {
        return face(font, static_cast<FT_F26Dot6>(unitsPerEm) * 64);
    }

    FT_Face rasterFace(const Font& font, float pixelSize) {
        return face(font, static_cast<FT_F26Dot6>(std::lround(pixelSize * 64.0f)));
    }
};

FontLibrary& FontLibrary::instance() {
    static FontLibrary library;
    return library;
}

FontLibrary::FontLibrary() : m_impl(std::make_unique<Impl>()) {}
FontLibrary::~FontLibrary() = default;

float FontLibrary::tracking(float size) {
    // https://rsms.me/inter/dynmetrics/
    constexpr float a = -0.0223f;
    constexpr float b = 0.185f;
    constexpr float c = -0.1745f;
    return a + b * std::exp(c * size);
}

float FontLibrary::advance(char32_t cp, const Font& font) {
    auto key = (Impl::styleKey(font) << 32) | static_cast<std::uint64_t>(cp);
    auto it = m_impl->advances.find(key);
    float em = 0;
    if (it != m_impl->advances.end()) {
        em = it->second;
    } else {
        FT_Face face = m_impl->metricFace(font);
        FT_UInt gi = FT_Get_Char_Index(face, cp);
        FT_Fixed adv = 0;
        FT_Get_Advance(face, gi, FT_LOAD_NO_HINTING, &adv);
        em = static_cast<float>(adv) / 65536.0f / static_cast<float>(face->units_per_EM);
        m_impl->advances.emplace(key, em);
    }
    return (em + tracking(font.size)) * font.size;
}

FontMetrics FontLibrary::metrics(const Font& font) {
    auto key = Impl::styleKey(font);
    auto it = m_impl->metrics.find(key);
    FontMetrics em;
    if (it != m_impl->metrics.end()) {
        em = it->second;
    } else {
        FT_Face face = m_impl->metricFace(font);
        auto upem = static_cast<float>(face->units_per_EM);
        em.ascender = static_cast<float>(face->ascender) / upem;
        em.descender = -static_cast<float>(face->descender) / upem;
        auto* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
        em.capHeight = os2 && os2->version >= 2 ? static_cast<float>(os2->sCapHeight) / upem : 0.727f;
        em.xHeight = os2 && os2->version >= 2 ? static_cast<float>(os2->sxHeight) / upem : 0.546f;
        m_impl->metrics.emplace(key, em);
    }
    return {em.ascender * font.size, em.descender * font.size, em.capHeight * font.size, em.xHeight * font.size};
}

bool FontLibrary::rasterize(char32_t cp, const Font& font, float pixelSize, int subpixel, GlyphBitmap& out) {
    FT_Face face = m_impl->rasterFace(font, pixelSize);
    FT_UInt gi = FT_Get_Char_Index(face, cp);
    // Unhinted outlines keep glyph shapes and spacing faithful to the design, like macOS.
    if (FT_Load_Glyph(face, gi, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP)) return false;
    if (face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) return false;
    FT_Outline_Translate(&face->glyph->outline, subpixel * 16, 0);
    if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) return false;
    const FT_Bitmap& bm = face->glyph->bitmap;
    out.width = static_cast<int>(bm.width);
    out.height = static_cast<int>(bm.rows);
    out.left = face->glyph->bitmap_left;
    out.top = face->glyph->bitmap_top;
    out.pixels.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height));
    for (int y = 0; y < out.height; ++y) {
        const unsigned char* row = bm.buffer + static_cast<std::ptrdiff_t>(y) * bm.pitch;
        std::copy(row, row + out.width, out.pixels.begin() + static_cast<std::ptrdiff_t>(y * out.width));
    }
    return true;
}

// --- layout helpers --------------------------------------------------------------------------

float measureText(std::string_view text, const Font& font) {
    auto& lib = FontLibrary::instance();
    float w = 0;
    size_t i = 0;
    while (i < text.size()) w += lib.advance(utf8::next(text, i), font);
    return w;
}

std::vector<TextLine> wrapText(std::string_view text, const Font& font, float maxWidth) {
    auto& lib = FontLibrary::instance();
    std::vector<TextLine> lines;
    const bool wrap = maxWidth > 0 && std::isfinite(maxWidth);
    size_t lineStart = 0;
    float lineWidth = 0;
    size_t lastBreak = std::string_view::npos;  // byte index just after a space
    float widthAtBreak = 0;                     // width up to (excluding) that space
    size_t i = 0;
    while (i < text.size()) {
        size_t cpStart = i;
        char32_t cp = utf8::next(text, i);
        if (cp == '\n') {
            lines.push_back({lineStart, cpStart, lineWidth});
            lineStart = i;
            lineWidth = 0;
            lastBreak = std::string_view::npos;
            continue;
        }
        float adv = lib.advance(cp, font);
        if (cp == ' ') {
            lastBreak = i;
            widthAtBreak = lineWidth;
            lineWidth += adv;
            continue;
        }
        if (wrap && lineWidth + adv > maxWidth && cpStart > lineStart) {
            if (lastBreak != std::string_view::npos) {
                lines.push_back({lineStart, lastBreak - 1, widthAtBreak});
                lineStart = lastBreak;
                lineWidth = measureText(text.substr(lineStart, cpStart - lineStart), font);
            } else {
                // A single word longer than the line: break it mid-word.
                lines.push_back({lineStart, cpStart, lineWidth});
                lineStart = cpStart;
                lineWidth = 0;
            }
            lastBreak = std::string_view::npos;
        }
        lineWidth += adv;
    }
    lines.push_back({lineStart, text.size(), lineWidth});
    return lines;
}

void caretPositions(std::string_view text, const Font& font, std::vector<size_t>& boundaries,
                    std::vector<float>& positions) {
    auto& lib = FontLibrary::instance();
    boundaries.clear();
    positions.clear();
    float x = 0;
    size_t i = 0;
    boundaries.push_back(0);
    positions.push_back(0);
    while (i < text.size()) {
        x += lib.advance(utf8::next(text, i), font);
        boundaries.push_back(i);
        positions.push_back(x);
    }
}

}  // namespace mayo::render
