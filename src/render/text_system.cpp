#include "text_system.hpp"

#include <cmath>

#include "../core/utf8.hpp"
#include "renderer2d.hpp"

namespace mayo::render {

namespace {
constexpr int kInitialAtlas = 1024;
constexpr int kMaxAtlas = 4096;
constexpr int kPadding = 1;
}  // namespace

TextSystem::TextSystem() { reset(kInitialAtlas); }

void TextSystem::reset(int size) {
    m_size = size;
    m_shelfX = kPadding;
    m_shelfY = kPadding;
    m_shelfHeight = 0;
    m_entries.clear();
    if (!m_atlas) m_atlas = gl::makeTexture();
    glBindTexture(GL_TEXTURE_2D, m_atlas.id());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, size, size, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    // Glyph quads land on whole pixels, so nearest sampling is exact and avoids bleeding.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // glTexImage2D with null data leaves contents undefined; clear so padding stays empty. Clearing through
    // a framebuffer avoids uploading a zero buffer (up to 16 MB) through the driver's staging memory.
    // This can run mid-frame (atlas full), so the caller's framebuffer state is restored afterwards.
    GLint previous = 0;
    GLint viewport[4] = {};
    GLfloat clearColor[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    gl::Framebuffer fb = gl::makeFramebuffer();
    glBindFramebuffer(GL_FRAMEBUFFER, fb.id());
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_atlas.id(), 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, size, size);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
    } else {
        std::vector<std::uint8_t> zeros(static_cast<size_t>(size) * static_cast<size_t>(size), 0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size, size, GL_RED, GL_UNSIGNED_BYTE, zeros.data());
    }
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    if (scissor) glEnable(GL_SCISSOR_TEST);
}

bool TextSystem::allocate(int w, int h, int& x, int& y) {
    if (m_shelfX + w + kPadding > m_size) {
        m_shelfX = kPadding;
        m_shelfY += m_shelfHeight + kPadding;
        m_shelfHeight = 0;
    }
    if (m_shelfY + h + kPadding > m_size) return false;
    x = m_shelfX;
    y = m_shelfY;
    m_shelfX += w + kPadding;
    m_shelfHeight = std::max(m_shelfHeight, h);
    return true;
}

const TextSystem::Entry& TextSystem::glyph(char32_t cp, const Font& font, float pixelSize, int subpixel,
                                           Renderer2D& renderer) {
    std::uint64_t key = (static_cast<std::uint64_t>(cp) << 40) |
                        (static_cast<std::uint64_t>(std::lround(pixelSize * 4.0f)) << 24) |
                        (static_cast<std::uint64_t>(std::lround(std::clamp(font.size, 14.0f, 32.0f))) << 16) |
                        (static_cast<std::uint64_t>(font.weight) << 4) | static_cast<std::uint64_t>(subpixel);
    auto it = m_entries.find(key);
    if (it != m_entries.end()) return it->second;

    Entry e;
    if (FontLibrary::instance().rasterize(cp, font, pixelSize, subpixel, m_scratch) && m_scratch.width > 0 &&
        m_scratch.height > 0) {
        int x = 0;
        int y = 0;
        if (!allocate(m_scratch.width, m_scratch.height, x, y)) {
            // Atlas full: draw what's queued against the old contents, then start over bigger.
            renderer.flushIfUsing(m_atlas.id());
            reset(std::min(m_size * 2, kMaxAtlas));
            allocate(m_scratch.width, m_scratch.height, x, y);
        }
        glBindTexture(GL_TEXTURE_2D, m_atlas.id());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, m_scratch.width, m_scratch.height, GL_RED, GL_UNSIGNED_BYTE,
                        m_scratch.pixels.data());
        auto fs = static_cast<float>(m_size);
        e.uv = Rect(static_cast<float>(x) / fs, static_cast<float>(y) / fs, static_cast<float>(m_scratch.width) / fs,
                    static_cast<float>(m_scratch.height) / fs);
        e.width = m_scratch.width;
        e.height = m_scratch.height;
        e.left = m_scratch.left;
        e.top = m_scratch.top;
    }
    return m_entries.emplace(key, e).first->second;
}

void TextSystem::drawLine(Renderer2D& renderer, std::string_view text, Point baseline, const Font& font,
                          const Color& color) {
    if (text.empty() || color.a <= 0) return;
    auto& lib = FontLibrary::instance();
    const float s = renderer.scale();
    const float pixelSize = font.size * s;
    // Light text on dark backgrounds reads thinner; embolden it a little via coverage gamma.
    const float gamma = color.luminance() > 0.5f ? 0.80f : 0.92f;
    const float baselinePx = std::round(baseline.y * s);
    float penPx = baseline.x * s;
    size_t i = 0;
    while (i < text.size()) {
        char32_t cp = utf8::next(text, i);
        float x = std::floor(penPx);
        int subpixel = std::clamp(static_cast<int>((penPx - x) * 4.0f), 0, 3);
        const Entry& e = glyph(cp, font, pixelSize, subpixel, renderer);
        if (e.width > 0) {
            Rect quad(x + static_cast<float>(e.left), baselinePx - static_cast<float>(e.top),
                      static_cast<float>(e.width), static_cast<float>(e.height));
            renderer.glyphPx(quad, m_atlas.id(), e.uv, color, gamma);
        }
        penPx += lib.advance(cp, font) * s;
    }
}

}  // namespace mayo::render
