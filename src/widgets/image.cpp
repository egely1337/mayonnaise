#include "mayonnaise/widgets/image.hpp"

#include <algorithm>

#include "../render/gl.hpp"
#include "../render/image_io.hpp"
#include "../render/renderer2d.hpp"
#include "mayonnaise/painter.hpp"

namespace mayo {

namespace render {
/// False once the GL context is gone, so late ImageData destruction doesn't call into GL.
bool glContextAlive();
}  // namespace render

struct ImageData::Impl {
    render::DecodedImage pixels;
    GLuint texture = 0;

    GLuint textureId() {
        if (texture || pixels.pixels.empty()) return texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pixels.width, pixels.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     pixels.pixels.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return texture;
    }
};

ImageData::ImageData() : m_impl(std::make_unique<Impl>()) {}

ImageData::~ImageData() {
    if (m_impl->texture && render::glContextAlive()) glDeleteTextures(1, &m_impl->texture);
}

std::shared_ptr<ImageData> ImageData::load(const std::string& path) {
    std::shared_ptr<ImageData> img(new ImageData());
    if (!render::loadImageFile(path, img->m_impl->pixels)) return nullptr;
    return img;
}

std::shared_ptr<ImageData> ImageData::decode(std::span<const std::uint8_t> encoded) {
    std::shared_ptr<ImageData> img(new ImageData());
    if (!render::decodeImage(encoded, img->m_impl->pixels)) return nullptr;
    return img;
}

std::shared_ptr<ImageData> ImageData::fromPixels(int width, int height, std::span<const std::uint8_t> rgba) {
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 4)
        return nullptr;
    std::shared_ptr<ImageData> img(new ImageData());
    auto& d = img->m_impl->pixels;
    d.width = width;
    d.height = height;
    d.pixels.assign(rgba.begin(), rgba.begin() + static_cast<std::ptrdiff_t>(width) * height * 4);
    for (size_t i = 0; i < d.pixels.size(); i += 4) {
        unsigned a = d.pixels[i + 3];
        for (size_t c = 0; c < 3; ++c) d.pixels[i + c] = static_cast<std::uint8_t>((d.pixels[i + c] * a + 127) / 255);
    }
    return img;
}

int ImageData::width() const { return m_impl->pixels.width; }
int ImageData::height() const { return m_impl->pixels.height; }

void Painter::drawImage(const Rect& r, const ImageData& image, const CornerRadii& radii, float opacity,
                        const Rect& source) {
    GLuint tex = image.impl().textureId();
    if (!tex) return;
    m_renderer.image(r, tex, source, radii, opacity);
}

ImageView::ImageView(std::shared_ptr<ImageData> data) : m_data(std::move(data)) {}

void ImageView::setData(std::shared_ptr<ImageData> data) {
    m_data = std::move(data);
    invalidateLayout();
}

void ImageView::setContentMode(ContentMode mode) {
    m_mode = mode;
    invalidate();
}

void ImageView::setSourceScale(float scale) {
    m_sourceScale = std::max(0.01f, scale);
    invalidateLayout();
}

Size ImageView::measureContent(Size available) {
    if (!m_data) return {};
    Size natural{static_cast<float>(m_data->width()) / m_sourceScale,
                 static_cast<float>(m_data->height()) / m_sourceScale};
    // Shrink to fit the offered width, keeping the aspect ratio.
    if (std::isfinite(available.width) && natural.width > available.width && natural.width > 0) {
        float k = available.width / natural.width;
        natural = {natural.width * k, natural.height * k};
    }
    return natural;
}

void ImageView::draw(Painter& p) {
    if (!m_data || m_data->width() <= 0) return;
    const Rect box = contentRect();
    const float iw = static_cast<float>(m_data->width());
    const float ih = static_cast<float>(m_data->height());
    Rect target = box;
    Rect source{0, 0, 1, 1};
    if (m_mode == ContentMode::Fit) {
        float k = std::min(box.width / iw, box.height / ih);
        target = {box.center().x - iw * k * 0.5f, box.center().y - ih * k * 0.5f, iw * k, ih * k};
    } else if (m_mode == ContentMode::Fill) {
        // Crop in texture space so rounded corners stay on the visible rect.
        float k = std::max(box.width / iw, box.height / ih);
        float sw = box.width / (iw * k);
        float sh = box.height / (ih * k);
        source = {(1 - sw) * 0.5f, (1 - sh) * 0.5f, sw, sh};
    }
    p.drawImage(target, *m_data, cornerRadius(), 1.0f, source);
}

}  // namespace mayo
