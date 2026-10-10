#include "gl.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "../platform/platform.hpp"

namespace mayo::gl {

namespace {

#ifndef NDEBUG
void GLAD_API_PTR debugCallback(GLenum /*source*/, GLenum type, GLuint id, GLenum severity, GLsizei /*length*/,
                                const GLchar* message, const void* /*user*/) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    std::fprintf(stderr, "mayonnaise GL %s (id %u, type 0x%x): %s\n",
                 severity == GL_DEBUG_SEVERITY_HIGH ? "error" : "warning", id, type, message);
}
#endif

GLuint compile(GLenum stage, std::string_view source, std::string_view name) {
    GLuint shader = glCreateShader(stage);
    const char* src = source.data();
    auto len = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &src, &len);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(shader, sizeof log, nullptr, log);
        std::fprintf(stderr, "mayonnaise: failed to compile %.*s (%s):\n%s\n", static_cast<int>(name.size()),
                     name.data(), stage == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        std::abort();
    }
    return shader;
}

}  // namespace

bool load() {
    int version = gladLoadGL(reinterpret_cast<GLADloadfunc>(&platform::Platform::glProcAddress));
    if (!version) return false;
#ifndef NDEBUG
    if (GLAD_GL_KHR_debug) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(debugCallback, nullptr);
    }
#endif
    return true;
}

Buffer makeBuffer() {
    GLuint id = 0;
    glGenBuffers(1, &id);
    return Buffer(id);
}
VertexArray makeVertexArray() {
    GLuint id = 0;
    glGenVertexArrays(1, &id);
    return VertexArray(id);
}
Texture makeTexture() {
    GLuint id = 0;
    glGenTextures(1, &id);
    return Texture(id);
}
Framebuffer makeFramebuffer() {
    GLuint id = 0;
    glGenFramebuffers(1, &id);
    return Framebuffer(id);
}

Program makeProgram(std::string_view vertexSource, std::string_view fragmentSource, std::string_view name) {
    GLuint vs = compile(GL_VERTEX_SHADER, vertexSource, name);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentSource, name);
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(program, sizeof log, nullptr, log);
        std::fprintf(stderr, "mayonnaise: failed to link %.*s:\n%s\n", static_cast<int>(name.size()), name.data(), log);
        std::abort();
    }
    return Program(program);
}

bool RenderTarget::ensure(int w, int h, GLenum filter) {
    w = w < 1 ? 1 : w;
    h = h < 1 ? 1 : h;
    if (fbo && w == width && h == height) return false;
    if (!fbo) fbo = makeFramebuffer();
    if (!texture) texture = makeTexture();
    width = w;
    height = h;
    glBindTexture(GL_TEXTURE_2D, texture.id());
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo.id());
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.id(), 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

}  // namespace mayo::gl
