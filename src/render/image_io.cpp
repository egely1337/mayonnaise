#include "image_io.hpp"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace mayo::render {

namespace {
bool finish(unsigned char* data, int w, int h, DecodedImage& out) {
    if (!data) return false;
    out.width = w;
    out.height = h;
    out.pixels.assign(data, data + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    stbi_image_free(data);
    // The renderer blends premultiplied colours.
    for (size_t i = 0; i < out.pixels.size(); i += 4) {
        unsigned a = out.pixels[i + 3];
        for (size_t c = 0; c < 3; ++c) out.pixels[i + c] = static_cast<std::uint8_t>((out.pixels[i + c] * a + 127) / 255);
    }
    return true;
}
}  // namespace

bool decodeImage(std::span<const std::uint8_t> bytes, DecodedImage& out) {
    int w = 0;
    int h = 0;
    int n = 0;
    unsigned char* data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, 4);
    return finish(data, w, h, out);
}

bool loadImageFile(const std::string& path, DecodedImage& out) {
    int w = 0;
    int h = 0;
    int n = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);
    return finish(data, w, h, out);
}

bool writePng(const std::string& path, int width, int height, std::vector<std::uint8_t> px) {
    for (size_t i = 0; i < px.size(); i += 4) {
        unsigned a = px[i + 3];
        if (a == 0 || a == 255) continue;
        for (size_t c = 0; c < 3; ++c) {
            unsigned v = (px[i + c] * 255u + a / 2) / a;
            px[i + c] = static_cast<std::uint8_t>(v > 255 ? 255 : v);
        }
    }
    return stbi_write_png(path.c_str(), width, height, 4, px.data(), width * 4) != 0;
}

}  // namespace mayo::render
