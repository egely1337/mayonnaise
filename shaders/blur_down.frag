#version 330 core
#ifdef VULKAN
// The same source is compiled to SPIR-V; GLSL 330 needs these for explicit locations and bindings.
#extension GL_ARB_separate_shader_objects : require
#extension GL_ARB_shading_language_420pack : require
#define LOC(n) layout(location = n)
#define BINDING(n) layout(set = 0, binding = n)
#else
#define LOC(n)
#define BINDING(n)
#endif
// Dual-Kawase downsample pass (Marius Bjørge, SIGGRAPH 2015).
LOC(0) in vec2 v_uv;
BINDING(0) uniform sampler2D u_source;
#ifdef VULKAN
layout(push_constant) uniform Push {
    vec2 u_halfPixel;  // 0.5 / source size
    float u_offset;
};
#else
uniform vec2 u_halfPixel;  // 0.5 / source size
uniform float u_offset;
#endif
LOC(0) out vec4 o_color;
void main() {
    vec2 o = u_halfPixel * u_offset;
    vec4 sum = texture(u_source, v_uv) * 4.0;
    sum += texture(u_source, v_uv - o);
    sum += texture(u_source, v_uv + o);
    sum += texture(u_source, v_uv + vec2(o.x, -o.y));
    sum += texture(u_source, v_uv - vec2(o.x, -o.y));
    o_color = sum / 8.0;
}
