#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "../widget.hpp"

namespace mayo {

/// Decoded RGBA pixels plus a lazily created GPU texture. Share it between Image widgets freely.
class ImageData {
public:
    /// PNG / JPEG / BMP / TGA from disk; nullptr on failure.
    static std::shared_ptr<ImageData> load(const std::string& path);
    static std::shared_ptr<ImageData> decode(std::span<const std::uint8_t> encoded);
    /// Straight-alpha RGBA8, row-major, top row first.
    static std::shared_ptr<ImageData> fromPixels(int width, int height, std::span<const std::uint8_t> rgba);

    ~ImageData();
    ImageData(const ImageData&) = delete;
    ImageData& operator=(const ImageData&) = delete;

    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;

    struct Impl;
    [[nodiscard]] Impl& impl() const { return *m_impl; }

private:
    ImageData();
    std::unique_ptr<Impl> m_impl;
};

enum class ContentMode : std::uint8_t {
    Fit,      ///< scale to fit inside, keep aspect ratio
    Fill,     ///< scale to cover, keep aspect ratio, crop the overflow
    Stretch,  ///< ignore aspect ratio
};

class ImageView : public Widget {
public:
    explicit ImageView(std::shared_ptr<ImageData> data);
    void setData(std::shared_ptr<ImageData> data);
    void setContentMode(ContentMode mode);
    /// Pixels per point of the source (2 for @2x artwork). Default 1.
    void setSourceScale(float scale);

    Size measureContent(Size available) override;
    void draw(Painter& painter) override;

private:
    std::shared_ptr<ImageData> m_data;
    ContentMode m_mode = ContentMode::Fit;
    float m_sourceScale = 1;
};

/// Displays a bitmap. Without a frame it uses the image's natural size.
class Image : public WidgetHandle<Image, ImageView> {
public:
    explicit Image(const std::string& path) : WidgetHandle(std::make_shared<ImageView>(ImageData::load(path))) {}
    explicit Image(std::shared_ptr<ImageData> data) : WidgetHandle(std::make_shared<ImageView>(std::move(data))) {}
    Image& contentMode(ContentMode m) { return apply([&](ImageView& n) { n.setContentMode(m); }); }
    Image& sourceScale(float s) { return apply([&](ImageView& n) { n.setSourceScale(s); }); }
    void setData(std::shared_ptr<ImageData> data) { m_node->setData(std::move(data)); }
};

}  // namespace mayo
