#include "renderer2d.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "embedded.hpp"

namespace mayo::render {

namespace {
constexpr float kNoClip = 1e7f;
}

Renderer2D::Renderer2D() {
    using embedded::text;
    m_program = gl::makeProgram(text(embedded::ui_vert), text(embedded::ui_frag), "ui");
    m_blurDown = gl::makeProgram(text(embedded::fullscreen_vert), text(embedded::blur_down_frag), "blur_down");
    m_blurUp = gl::makeProgram(text(embedded::fullscreen_vert), text(embedded::blur_up_frag), "blur_up");
    m_uViewport = glGetUniformLocation(m_program.id(), "u_viewport");
    m_uTexture = glGetUniformLocation(m_program.id(), "u_texture");

    m_vao = gl::makeVertexArray();
    m_emptyVao = gl::makeVertexArray();
    m_instanceBuffer = gl::makeBuffer();
    glBindVertexArray(m_vao.id());
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceBuffer.id());
    for (GLuint i = 0; i < 10; ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(Instance),
                              reinterpret_cast<const void*>(static_cast<std::uintptr_t>(i * 4 * sizeof(float))));
        glVertexAttribDivisor(i, 1);
    }
    glBindVertexArray(0);

    // A 1x1 white texture keeps the sampler valid for batches that never sample.
    m_whiteTexture = gl::makeTexture();
    glBindTexture(GL_TEXTURE_2D, m_whiteTexture.id());
    const std::uint8_t white[4] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    m_instances.reserve(4096);
}

Renderer2D::~Renderer2D() = default;

void Renderer2D::beginFrame(int pixelWidth, int pixelHeight, float scale) {
    m_width = std::max(1, pixelWidth);
    m_height = std::max(1, pixelHeight);
    m_scale = scale;
    m_scene.ensure(m_width, m_height);
    m_clips.clear();
    m_clips.push_back({-kNoClip, -kNoClip, kNoClip, kNoClip, -kNoClip, -kNoClip, kNoClip, kNoClip, 0});
    m_opacity.assign(1, 1.0f);
    m_instances.clear();
    m_batchTexture = 0;
    m_drawCalls = 0;

    glBindFramebuffer(GL_FRAMEBUFFER, m_scene.fbo.id());
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer2D::endFrame() {
    flush();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_scene.fbo.id());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_drawCallsLast = m_drawCalls;
}

std::vector<std::uint8_t> Renderer2D::readPixels(int& width, int& height) {
    flush();
    width = m_scene.width;
    height = m_scene.height;
    std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, m_scene.fbo.id());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    // GL rows are bottom-up.
    const size_t stride = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height / 2; ++y) {
        std::swap_ranges(pixels.begin() + static_cast<std::ptrdiff_t>(y * stride),
                         pixels.begin() + static_cast<std::ptrdiff_t>((y + 1) * stride),
                         pixels.begin() + static_cast<std::ptrdiff_t>((height - 1 - y) * stride));
    }
    return pixels;
}

// --- submission ------------------------------------------------------------------------------

void Renderer2D::setColor(float* out, const Color& c) const {
    float a = c.a * m_opacity.back();
    out[0] = c.r * a;
    out[1] = c.g * a;
    out[2] = c.b * a;
    out[3] = a;
}

bool Renderer2D::culled(float l, float t, float r, float b) const {
    const Clip& c = m_clips.back();
    return r <= c.l || b <= c.t || l >= c.r || t >= c.b || r <= l || b <= t;
}

Renderer2D::Instance& Renderer2D::push(Type type, float l, float t, float r, float b, GLuint texture) {
    if (texture && texture != m_batchTexture) {
        if (!m_instances.empty() && m_batchTexture) flush();
        m_batchTexture = texture;
    }
    const Clip& c = m_clips.back();
    Instance& in = m_instances.emplace_back();
    // Shrinking the quad to the clip saves fill rate; textured quads keep theirs so UVs stay valid.
    if (type != Type::Image && type != Type::Glyph) {
        l = std::max(l, c.l);
        t = std::max(t, c.t);
        r = std::min(r, c.r);
        b = std::min(b, c.b);
    }
    in.quad[0] = l;
    in.quad[1] = t;
    in.quad[2] = r;
    in.quad[3] = b;
    in.params[0] = static_cast<float>(type);
    in.params[1] = 0;
    in.params[2] = 0;
    in.params[3] = 1;
    in.clip[0] = c.l;
    in.clip[1] = c.t;
    in.clip[2] = c.r;
    in.clip[3] = c.b;
    in.rclip[0] = c.rl;
    in.rclip[1] = c.rt;
    in.rclip[2] = c.rr;
    in.rclip[3] = c.rb;
    in.clip2[0] = c.radius;
    in.clip2[1] = in.clip2[2] = in.clip2[3] = 0;
    return in;
}

void Renderer2D::fillRoundedRect(const Rect& rect, const CornerRadii& radii, const Color& fill, const Color& border,
                                 float borderWidth) {
    const float s = m_scale;
    // Snap edges to device pixels so hairlines and control edges stay crisp.
    float l = std::round(rect.left() * s);
    float t = std::round(rect.top() * s);
    float r = std::round(rect.right() * s);
    float b = std::round(rect.bottom() * s);
    if (r <= l || b <= t) return;
    if (fill.a <= 0 && (border.a <= 0 || borderWidth <= 0)) return;
    if (culled(l - 1, t - 1, r + 1, b + 1)) return;
    Instance& in = push(Type::Fill, l - 1, t - 1, r + 1, b + 1, 0);
    in.shape[0] = (l + r) * 0.5f;
    in.shape[1] = (t + b) * 0.5f;
    in.shape[2] = (r - l) * 0.5f;
    in.shape[3] = (b - t) * 0.5f;
    const float maxR = std::min(in.shape[2], in.shape[3]);
    in.radii[0] = std::min(radii.topLeft * s, maxR);
    in.radii[1] = std::min(radii.topRight * s, maxR);
    in.radii[2] = std::min(radii.bottomRight * s, maxR);
    in.radii[3] = std::min(radii.bottomLeft * s, maxR);
    setColor(in.color, fill);
    setColor(in.color2, border);
    in.params[1] = borderWidth > 0 ? std::max(borderWidth * s, 1.0f) : 0.0f;
}

void Renderer2D::strokeRoundedRect(const Rect& rect, const CornerRadii& radii, const Color& color, float width) {
    fillRoundedRect(rect, radii, Color::transparent(), color, width);
}

void Renderer2D::fillGradient(const Rect& rect, const CornerRadii& radii, const Color& from, const Color& to, Point p0,
                              Point p1) {
    const float s = m_scale;
    float l = std::round(rect.left() * s);
    float t = std::round(rect.top() * s);
    float r = std::round(rect.right() * s);
    float b = std::round(rect.bottom() * s);
    if (r <= l || b <= t || culled(l - 1, t - 1, r + 1, b + 1)) return;
    Instance& in = push(Type::Gradient, l - 1, t - 1, r + 1, b + 1, 0);
    in.shape[0] = (l + r) * 0.5f;
    in.shape[1] = (t + b) * 0.5f;
    in.shape[2] = (r - l) * 0.5f;
    in.shape[3] = (b - t) * 0.5f;
    const float maxR = std::min(in.shape[2], in.shape[3]);
    in.radii[0] = std::min(radii.topLeft * s, maxR);
    in.radii[1] = std::min(radii.topRight * s, maxR);
    in.radii[2] = std::min(radii.bottomRight * s, maxR);
    in.radii[3] = std::min(radii.bottomLeft * s, maxR);
    setColor(in.color, from);
    setColor(in.color2, to);
    in.uv[0] = p0.x * s;
    in.uv[1] = p0.y * s;
    in.uv[2] = p1.x * s;
    in.uv[3] = p1.y * s;
}

void Renderer2D::shadow(const Rect& rect, float radius, const Color& color, float blur, Vec2 offset, float spread) {
    if (color.a <= 0) return;
    const float s = m_scale;
    Rect shape = rect.translated(offset).outset(spread);
    float sigma = std::max(blur * 0.5f * s, 0.5f);
    float margin = sigma * 3.0f;
    float l = shape.left() * s - margin;
    float t = shape.top() * s - margin;
    float r = shape.right() * s + margin;
    float b = shape.bottom() * s + margin;
    if (culled(l, t, r, b)) return;
    Instance& in = push(Type::Shadow, l, t, r, b, 0);
    in.shape[0] = shape.center().x * s;
    in.shape[1] = shape.center().y * s;
    in.shape[2] = shape.width * 0.5f * s;
    in.shape[3] = shape.height * 0.5f * s;
    float maxR = std::min(in.shape[2], in.shape[3]);
    in.radii[0] = std::min(std::max(0.0f, (radius + spread) * s), maxR);
    setColor(in.color, color);
    in.params[2] = sigma;
}

void Renderer2D::line(Point a, Point b, const Color& color, float width) {
    const float s = m_scale;
    float hw = std::max(width * s * 0.5f, 0.5f);
    float l = std::min(a.x, b.x) * s - hw - 1;
    float t = std::min(a.y, b.y) * s - hw - 1;
    float r = std::max(a.x, b.x) * s + hw + 1;
    float bt = std::max(a.y, b.y) * s + hw + 1;
    if (culled(l, t, r, bt)) return;
    Instance& in = push(Type::Line, l, t, r, bt, 0);
    in.shape[0] = a.x * s;
    in.shape[1] = a.y * s;
    in.shape[2] = b.x * s;
    in.shape[3] = b.y * s;
    setColor(in.color, color);
    in.params[1] = hw;
}

void Renderer2D::triangle(Point a, Point b, Point c, const Color& color, float cornerRadius) {
    const float s = m_scale;
    const float r = std::max(0.0f, cornerRadius) * s;
    float l = std::min({a.x, b.x, c.x}) * s - r - 1;
    float t = std::min({a.y, b.y, c.y}) * s - r - 1;
    float rr = std::max({a.x, b.x, c.x}) * s + r + 1;
    float bb = std::max({a.y, b.y, c.y}) * s + r + 1;
    if (culled(l, t, rr, bb)) return;
    Instance& in = push(Type::Triangle, l, t, rr, bb, 0);
    in.shape[0] = a.x * s;
    in.shape[1] = a.y * s;
    in.shape[2] = b.x * s;
    in.shape[3] = b.y * s;
    in.uv[0] = c.x * s;
    in.uv[1] = c.y * s;
    in.uv[2] = r;
    setColor(in.color, color);
}

void Renderer2D::arc(Point center, float radius, float startAngle, float sweep, const Color& color, float width) {
    const float s = m_scale;
    float hw = std::max(width * s * 0.5f, 0.5f);
    float ext = radius * s + hw + 1;
    float cx = center.x * s;
    float cy = center.y * s;
    if (culled(cx - ext, cy - ext, cx + ext, cy + ext)) return;
    Instance& in = push(Type::Arc, cx - ext, cy - ext, cx + ext, cy + ext, 0);
    in.shape[0] = cx;
    in.shape[1] = cy;
    in.shape[2] = radius * s;
    in.uv[0] = std::fmod(std::fmod(startAngle, 6.2831853f) + 6.2831853f, 6.2831853f);
    in.uv[1] = std::clamp(sweep, 0.0f, 6.2831853f);
    setColor(in.color, color);
    in.params[1] = hw;
}

void Renderer2D::image(const Rect& rect, GLuint texture, const Rect& uv, const CornerRadii& radii, float opacity) {
    const float s = m_scale;
    float l = std::round(rect.left() * s);
    float t = std::round(rect.top() * s);
    float r = std::round(rect.right() * s);
    float b = std::round(rect.bottom() * s);
    if (culled(l, t, r, b)) return;
    Instance& in = push(Type::Image, l, t, r, b, texture);
    in.shape[0] = (l + r) * 0.5f;
    in.shape[1] = (t + b) * 0.5f;
    in.shape[2] = (r - l) * 0.5f;
    in.shape[3] = (b - t) * 0.5f;
    const float maxR = std::min(in.shape[2], in.shape[3]);
    in.radii[0] = std::min(radii.topLeft * s, maxR);
    in.radii[1] = std::min(radii.topRight * s, maxR);
    in.radii[2] = std::min(radii.bottomRight * s, maxR);
    in.radii[3] = std::min(radii.bottomLeft * s, maxR);
    in.uv[0] = uv.left();
    in.uv[1] = uv.top();
    in.uv[2] = uv.right();
    in.uv[3] = uv.bottom();
    in.color[0] = in.color[1] = in.color[2] = 1;
    in.color[3] = opacity * m_opacity.back();
}

void Renderer2D::glyphPx(const Rect& px, GLuint atlas, const Rect& uv, const Color& color, float gamma) {
    if (culled(px.left(), px.top(), px.right(), px.bottom())) return;
    Instance& in = push(Type::Glyph, px.left(), px.top(), px.right(), px.bottom(), atlas);
    in.uv[0] = uv.left();
    in.uv[1] = uv.top();
    in.uv[2] = uv.right();
    in.uv[3] = uv.bottom();
    setColor(in.color, color);
    in.params[3] = gamma;
}

void Renderer2D::backdrop(const Rect& rect, const CornerRadii& radii, const MaterialStyle& style) {
    const float s = m_scale;
    float l = std::round(rect.left() * s);
    float t = std::round(rect.top() * s);
    float r = std::round(rect.right() * s);
    float b = std::round(rect.bottom() * s);
    if (culled(l, t, r, b)) return;
    flush();
    GLuint blurred = blurScene(style.blurRadius * s, Rect::fromLTRB(l, t, r, b));
    Instance& in = push(Type::Backdrop, l - 1, t - 1, r + 1, b + 1, blurred);
    in.shape[0] = (l + r) * 0.5f;
    in.shape[1] = (t + b) * 0.5f;
    in.shape[2] = (r - l) * 0.5f;
    in.shape[3] = (b - t) * 0.5f;
    const float maxR = std::min(in.shape[2], in.shape[3]);
    in.radii[0] = std::min(radii.topLeft * s, maxR);
    in.radii[1] = std::min(radii.topRight * s, maxR);
    in.radii[2] = std::min(radii.bottomRight * s, maxR);
    in.radii[3] = std::min(radii.bottomLeft * s, maxR);
    // Tint is premultiplied but must not be scaled by group opacity twice; backdrop alpha is the shape.
    Color tint = style.tint;
    in.color[0] = tint.r * tint.a;
    in.color[1] = tint.g * tint.a;
    in.color[2] = tint.b * tint.a;
    in.color[3] = tint.a;
    in.params[1] = style.fadeBottom;
    in.clip2[1] = m_opacity.back();
    in.params[2] = style.saturation;
    in.params[3] = style.noise;
    // The blur chain is reused by the next material, so draw this one right away.
    flush();
}

void Renderer2D::pushClip(const Rect& rect, float radius) {
    const float s = m_scale;
    const Clip& cur = m_clips.back();
    const float l = std::round(rect.left() * s);
    const float t = std::round(rect.top() * s);
    const float r = std::round(rect.right() * s);
    const float b = std::round(rect.bottom() * s);
    Clip c = cur;
    c.l = std::max(cur.l, l);
    c.t = std::max(cur.t, t);
    c.r = std::max(c.l, std::min(cur.r, r));
    c.b = std::max(c.t, std::min(cur.b, b));
    // The shader evaluates one rounded clip plus the plain intersection. A nested rounded clip
    // replaces the outer one; nested rectangular clips keep it, so corners stay rounded.
    if (radius > 0) {
        c.rl = l;
        c.rt = t;
        c.rr = r;
        c.rb = b;
        c.radius = radius * s;
    }
    m_clips.push_back(c);
}

void Renderer2D::popClip() {
    if (m_clips.size() > 1) m_clips.pop_back();
}

Rect Renderer2D::currentClip() const {
    const Clip& c = m_clips.back();
    return Rect::fromLTRB(c.l / m_scale, c.t / m_scale, c.r / m_scale, c.b / m_scale);
}

void Renderer2D::pushOpacity(float opacity) { m_opacity.push_back(m_opacity.back() * opacity); }
void Renderer2D::popOpacity() {
    if (m_opacity.size() > 1) m_opacity.pop_back();
}

void Renderer2D::flush() {
    if (m_instances.empty()) return;
    glBindFramebuffer(GL_FRAMEBUFFER, m_scene.fbo.id());
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_program.id());
    glUniform2f(m_uViewport, static_cast<float>(m_width), static_cast<float>(m_height));
    glUniform1i(m_uTexture, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_batchTexture ? m_batchTexture : m_whiteTexture.id());

    glBindVertexArray(m_vao.id());
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceBuffer.id());
    const size_t bytes = m_instances.size() * sizeof(Instance);
    if (m_instances.size() > m_instanceCapacity) {
        m_instanceCapacity = std::max(m_instances.size(), m_instanceCapacity * 2);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_instanceCapacity * sizeof(Instance)), nullptr,
                     GL_STREAM_DRAW);
    } else {
        // Orphan so the driver doesn't stall on the previous draw still reading the buffer.
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_instanceCapacity * sizeof(Instance)), nullptr,
                     GL_STREAM_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), m_instances.data());
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(m_instances.size()));
    glBindVertexArray(0);
    ++m_drawCalls;
    m_instances.clear();
    m_batchTexture = 0;
}

GLuint Renderer2D::blurScene(float radiusPx, const Rect& regionPx) {
    // Each dual-Kawase level roughly doubles the blur radius.
    int levels = std::clamp(static_cast<int>(std::ceil(std::log2(std::max(radiusPx, 2.0f) / 2.5f))), 1, 6);
    if (static_cast<int>(m_blurChain.size()) < levels) m_blurChain.resize(static_cast<size_t>(levels));
    for (int i = 0; i < levels; ++i) {
        m_blurChain[static_cast<size_t>(i)].ensure(std::max(1, m_width >> (i + 1)), std::max(1, m_height >> (i + 1)));
    }

    // Only blur around the region that will be sampled (plus the kernel footprint).
    const float margin = radiusPx * 2.0f + 16.0f;
    Rect area = regionPx.outset(margin).intersected(Rect(0, 0, static_cast<float>(m_width), static_cast<float>(m_height)));
    auto scissorFor = [&](int w, int h) {
        float kx = static_cast<float>(w) / static_cast<float>(m_width);
        float ky = static_cast<float>(h) / static_cast<float>(m_height);
        // Rect is top-down; GL scissor is bottom-up.
        int x0 = static_cast<int>(std::floor(area.left() * kx));
        int x1 = static_cast<int>(std::ceil(area.right() * kx));
        int y0 = static_cast<int>(std::floor((static_cast<float>(m_height) - area.bottom()) * ky));
        int y1 = static_cast<int>(std::ceil((static_cast<float>(m_height) - area.top()) * ky));
        glScissor(x0, y0, x1 - x0, y1 - y0);
    };

    glDisable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    glBindVertexArray(m_emptyVao.id());
    glActiveTexture(GL_TEXTURE0);

    auto pass = [&](const gl::Program& program, GLuint srcTex, int srcW, int srcH, const gl::RenderTarget& dst) {
        glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo.id());
        glViewport(0, 0, dst.width, dst.height);
        scissorFor(dst.width, dst.height);
        glUseProgram(program.id());
        glUniform1i(glGetUniformLocation(program.id(), "u_source"), 0);
        glUniform2f(glGetUniformLocation(program.id(), "u_halfPixel"), 0.5f / static_cast<float>(srcW),
                    0.5f / static_cast<float>(srcH));
        glUniform1f(glGetUniformLocation(program.id(), "u_offset"), 1.0f);
        glBindTexture(GL_TEXTURE_2D, srcTex);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    };

    GLuint src = m_scene.texture.id();
    int sw = m_width;
    int sh = m_height;
    for (int i = 0; i < levels; ++i) {
        auto& dst = m_blurChain[static_cast<size_t>(i)];
        pass(m_blurDown, src, sw, sh, dst);
        src = dst.texture.id();
        sw = dst.width;
        sh = dst.height;
    }
    for (int i = levels - 1; i > 0; --i) {
        auto& from = m_blurChain[static_cast<size_t>(i)];
        auto& dst = m_blurChain[static_cast<size_t>(i - 1)];
        pass(m_blurUp, from.texture.id(), from.width, from.height, dst);
    }

    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, m_scene.fbo.id());
    glViewport(0, 0, m_width, m_height);
    return m_blurChain[0].texture.id();
}

}  // namespace mayo::render
