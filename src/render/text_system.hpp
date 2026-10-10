#pragma once

#include <string_view>
#include <unordered_map>

#include "font.hpp"
#include "gl.hpp"
#include "mayonnaise/color.hpp"
#include "mayonnaise/geometry.hpp"

namespace mayo::render {

class Renderer2D;

/// GPU side of text: an R8 glyph atlas (shelf packed, grows on demand) and glyph-run submission.
class TextSystem {
public:
    TextSystem();

    /// Draws one line; `baseline` is in points. Glyphs are positioned with 1/4 px horizontal precision.
    void drawLine(Renderer2D& renderer, std::string_view text, Point baseline, const Font& font, const Color& color);

private:
    struct Entry {
        Rect uv;
        int width = 0;
        int height = 0;
        int left = 0;
        int top = 0;
    };
    const Entry& glyph(char32_t cp, const Font& font, float pixelSize, int subpixel, Renderer2D& renderer);
    bool allocate(int w, int h, int& x, int& y);
    void reset(int size);

    gl::Texture m_atlas;
    int m_size = 0;
    int m_shelfX = 0;
    int m_shelfY = 0;
    int m_shelfHeight = 0;
    std::unordered_map<std::uint64_t, Entry> m_entries;
    GlyphBitmap m_scratch;
};

}  // namespace mayo::render
