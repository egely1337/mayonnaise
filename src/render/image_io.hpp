#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mayo::render {

struct DecodedImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;  // RGBA8, premultiplied
};

/// Decodes PNG/JPEG/GIF/BMP/TGA and applies the JPEG EXIF orientation, so photos come out upright.
bool decodeImage(std::span<const std::uint8_t> bytes, DecodedImage& out);
bool loadImageFile(const std::string& path, DecodedImage& out);
/// Area-averaging downscale (premultiplied pixels average correctly). No-op if already that small.
void resizeBox(DecodedImage& image, int width, int height);
/// EXIF orientation tag (1..8) of a JPEG, 1 if absent.
int exifOrientation(std::span<const std::uint8_t> bytes);
/// Writes premultiplied RGBA8 pixels as a straight-alpha PNG.
bool writePng(const std::string& path, int width, int height, std::vector<std::uint8_t> premultiplied);

}  // namespace mayo::render
