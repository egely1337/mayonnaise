#include "mayonnaise/video.hpp"

#include <cstring>

#include "../platform/platform.hpp"
#include "gl.hpp"
#include "mayonnaise/painter.hpp"
#include "renderer2d.hpp"

namespace mayo {

namespace render {
bool glContextAlive();
}

namespace {

// EGL entry points and constants, declared here so the build doesn't need EGL headers.
using EGLDisplay = void*;
using EGLImage = void*;
using EGLint = std::int32_t;
using CreateImageFn = EGLImage (*)(EGLDisplay, void* context, unsigned target, void* buffer, const EGLint* attribs);
using DestroyImageFn = unsigned (*)(EGLDisplay, EGLImage);
using QueryStringFn = const char* (*)(EGLDisplay, EGLint);
using ImageTargetFn = void (*)(GLenum target, void* image);

constexpr EGLint kEglNone = 0x3038;
constexpr EGLint kEglExtensions = 0x3055;
constexpr EGLint kEglWidth = 0x3057;
constexpr EGLint kEglHeight = 0x3056;
constexpr unsigned kEglLinuxDmaBuf = 0x3270;
constexpr EGLint kEglFourcc = 0x3271;
constexpr EGLint kEglPlane0Fd = 0x3272;
constexpr EGLint kEglPlane0Offset = 0x3273;
constexpr EGLint kEglPlane0Pitch = 0x3274;
constexpr EGLint kEglPlane0ModLo = 0x3443;
constexpr EGLint kEglPlane0ModHi = 0x3444;
constexpr std::uint64_t kModInvalid = 0x00ffffffffffffffull;

struct Egl {
    bool loaded = false;
    bool supported = false;
    bool modifiers = false;
    EGLDisplay display = nullptr;
    CreateImageFn createImage = nullptr;
    DestroyImageFn destroyImage = nullptr;
    ImageTargetFn imageTarget = nullptr;

    void load() {
        if (loaded) return;
        loaded = true;
        display = platform::Platform::eglDisplay();
        if (!display) return;  // GLX context: no dma-buf import
        auto query = reinterpret_cast<QueryStringFn>(platform::Platform::eglProcAddress("eglQueryString"));
        createImage = reinterpret_cast<CreateImageFn>(platform::Platform::eglProcAddress("eglCreateImageKHR"));
        destroyImage = reinterpret_cast<DestroyImageFn>(platform::Platform::eglProcAddress("eglDestroyImageKHR"));
        imageTarget = reinterpret_cast<ImageTargetFn>(platform::Platform::glProcAddress("glEGLImageTargetTexture2DOES"));
        const char* ext = query ? query(display, kEglExtensions) : nullptr;
        supported = ext && std::strstr(ext, "EGL_EXT_image_dma_buf_import") && createImage && destroyImage && imageTarget;
        modifiers = ext && std::strstr(ext, "EGL_EXT_image_dma_buf_import_modifiers");
    }
};

Egl& egl() {
    static Egl e;
    e.load();
    return e;
}

}  // namespace

struct VideoTexture::Impl {
    GLuint tex[3] = {0, 0, 0};
    int width = 0;
    int height = 0;
    bool planar = false;  // I420 (three planes) vs NV12 (two)
    VideoColor color;
    // Allocated storage per plane (imports bypass this: they respecify the texture).
    int allocW[3] = {0, 0, 0};
    int allocH[3] = {0, 0, 0};
    GLenum allocFormat[3] = {0, 0, 0};

    ~Impl() {
        if (render::glContextAlive()) glDeleteTextures(3, tex);
    }

    void ensureTextures() {
        if (tex[0]) return;
        glGenTextures(3, tex);
        for (GLuint t : tex) {
            glBindTexture(GL_TEXTURE_2D, t);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
    }

    /// Uploads one plane, honouring its stride. `channels` is 1 (R) or 2 (RG).
    void upload(int index, int w, int h, VideoPlane plane, int channels, bool sixteen) {
        const GLenum internal = channels == 1 ? (sixteen ? GL_R16 : GL_R8) : (sixteen ? GL_RG16 : GL_RG8);
        const GLenum format = channels == 1 ? GL_RED : GL_RG;
        const GLenum type = sixteen ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
        const int bytesPerPixel = channels * (sixteen ? 2 : 1);
        glBindTexture(GL_TEXTURE_2D, tex[index]);
        if (allocW[index] != w || allocH[index] != h || allocFormat[index] != internal) {
            glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internal), w, h, 0, format, type, nullptr);
            allocW[index] = w;
            allocH[index] = h;
            allocFormat[index] = internal;
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, plane.stride / bytesPerPixel);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, type, plane.data);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
};

VideoTexture::VideoTexture() : m_impl(std::make_unique<Impl>()) {}
VideoTexture::~VideoTexture() = default;

std::shared_ptr<VideoTexture> VideoTexture::create() { return std::shared_ptr<VideoTexture>(new VideoTexture()); }

int VideoTexture::width() const { return m_impl->width; }
int VideoTexture::height() const { return m_impl->height; }
bool VideoTexture::isEmpty() const { return m_impl->width == 0; }

bool VideoTexture::dmaBufSupported() { return egl().supported; }

void VideoTexture::uploadI420(int w, int h, VideoPlane y, VideoPlane u, VideoPlane v, VideoColor color) {
    auto& d = *m_impl;
    d.ensureTextures();
    const int cw = (w + 1) / 2;
    const int ch = (h + 1) / 2;
    d.upload(0, w, h, y, 1, false);
    d.upload(1, cw, ch, u, 1, false);
    d.upload(2, cw, ch, v, 1, false);
    d.width = w;
    d.height = h;
    d.planar = true;
    d.color = color;
}

void VideoTexture::uploadNV12(int w, int h, VideoPlane y, VideoPlane uv, VideoColor color, bool sixteenBit) {
    auto& d = *m_impl;
    d.ensureTextures();
    d.upload(0, w, h, y, 1, sixteenBit);
    d.upload(1, (w + 1) / 2, (h + 1) / 2, uv, 2, sixteenBit);
    d.width = w;
    d.height = h;
    d.planar = false;
    d.color = color;
}

bool VideoTexture::importDmaBuf(int w, int h, std::span<const DmaBufLayer> layers, VideoColor color) {
    Egl& e = egl();
    if (!e.supported || layers.size() != 2) return false;
    auto& d = *m_impl;
    d.ensureTextures();
    for (size_t i = 0; i < 2; ++i) {
        const DmaBufLayer& l = layers[i];
        EGLint attribs[20];
        int n = 0;
        attribs[n++] = kEglWidth;
        attribs[n++] = l.width;
        attribs[n++] = kEglHeight;
        attribs[n++] = l.height;
        attribs[n++] = kEglFourcc;
        attribs[n++] = static_cast<EGLint>(l.drmFormat);
        attribs[n++] = kEglPlane0Fd;
        attribs[n++] = l.fd;
        attribs[n++] = kEglPlane0Offset;
        attribs[n++] = static_cast<EGLint>(l.offset);
        attribs[n++] = kEglPlane0Pitch;
        attribs[n++] = static_cast<EGLint>(l.pitch);
        if (e.modifiers && l.modifier != kModInvalid) {
            attribs[n++] = kEglPlane0ModLo;
            attribs[n++] = static_cast<EGLint>(l.modifier & 0xffffffffu);
            attribs[n++] = kEglPlane0ModHi;
            attribs[n++] = static_cast<EGLint>(l.modifier >> 32);
        }
        attribs[n++] = kEglNone;
        EGLImage image = e.createImage(e.display, nullptr, kEglLinuxDmaBuf, nullptr, attribs);
        if (!image) return false;
        glBindTexture(GL_TEXTURE_2D, d.tex[i]);
        e.imageTarget(GL_TEXTURE_2D, image);
        // The texture keeps the buffer referenced; the EGLImage handle itself isn't needed any more.
        e.destroyImage(e.display, image);
        d.allocW[i] = d.allocH[i] = 0;  // storage now belongs to the import
    }
    d.width = w;
    d.height = h;
    d.planar = false;
    d.color = color;
    return true;
}

void Painter::drawVideo(const Rect& r, const VideoTexture& video, const CornerRadii& radii, float opacity) {
    const auto& d = video.impl();
    if (d.width == 0) return;
    const GLuint tex[3] = {d.tex[0], d.tex[1], d.planar ? d.tex[2] : 0};
    m_renderer.video(r, tex, d.planar, static_cast<int>(d.color.matrix), d.color.fullRange, radii, opacity);
}

}  // namespace mayo
