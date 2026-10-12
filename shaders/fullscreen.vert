#version 330 core
#ifdef VULKAN
// The same source is compiled to SPIR-V; GLSL 330 needs these for explicit locations and bindings.
#extension GL_ARB_separate_shader_objects : require
#extension GL_ARB_shading_language_420pack : require
#define LOC(n) layout(location = n)
#define VERTEX_ID gl_VertexIndex
#else
#define LOC(n)
#define VERTEX_ID gl_VertexID
#endif
// Fullscreen triangle; no vertex buffer needed.
LOC(0) out vec2 v_uv;
void main() {
    vec2 pos = vec2(float((VERTEX_ID << 1) & 2), float(VERTEX_ID & 2));
    v_uv = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
