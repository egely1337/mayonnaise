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

bool decodeImage(std::span<const std::uint8_t> bytes, DecodedImage& out);
bool loadImageFile(const std::string& path, DecodedImage& out);
/// Writes premultiplied RGBA8 pixels as a straight-alpha PNG.
bool writePng(const std::string& path, int width, int height, std::vector<std::uint8_t> premultiplied);

}  // namespace mayo::render
