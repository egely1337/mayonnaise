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
// One instance per primitive; the quad corners are generated from VERTEX_ID (triangle strip).

layout(location = 0) in vec4 a_quad;     // l, t, r, b (pixels) — area to cover
layout(location = 1) in vec4 a_shape;    // cx, cy, hw, hh  (lines: ax, ay, bx, by)
layout(location = 2) in vec4 a_radii;    // tl, tr, br, bl
layout(location = 3) in vec4 a_color;    // premultiplied
layout(location = 4) in vec4 a_color2;   // premultiplied
layout(location = 5) in vec4 a_uv;       // u0, v0, u1, v1 (gradients: x0, y0, x1, y1 in pixels)
layout(location = 6) in vec4 a_params;   // type, stroke, sigma, extra
layout(location = 7) in vec4 a_clip;     // plain clip l, t, r, b
layout(location = 8) in vec4 a_rclip;    // rounded clip l, t, r, b
layout(location = 9) in vec4 a_clip2;    // rounded clip radius, group opacity, -, -

#ifdef VULKAN
layout(push_constant) uniform Push { vec2 u_viewport; };
#else
uniform vec2 u_viewport;
#endif

LOC(0) out vec2 v_pixel;
LOC(1) out vec2 v_uv;
LOC(2) flat out vec4 v_shape;
LOC(3) flat out vec4 v_radii;
LOC(4) flat out vec4 v_color;
LOC(5) flat out vec4 v_color2;
LOC(6) flat out vec4 v_gradient;
LOC(7) flat out vec4 v_params;
LOC(8) flat out vec4 v_clip;
LOC(9) flat out vec4 v_rclip;
LOC(10) flat out vec4 v_clip2;

void main() {
    vec2 corner = vec2(float(VERTEX_ID & 1), float(VERTEX_ID >> 1));
    vec2 pos = mix(a_quad.xy, a_quad.zw, corner);
    v_pixel = pos;
    v_uv = mix(a_uv.xy, a_uv.zw, corner);
    v_shape = a_shape;
    v_radii = a_radii;
    v_color = a_color;
    v_color2 = a_color2;
    v_gradient = a_uv;
    v_params = a_params;
    v_clip = a_clip;
    v_rclip = a_rclip;
    v_clip2 = a_clip2;
    vec2 ndc = pos / u_viewport * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
