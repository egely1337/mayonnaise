#version 330 core
// One instance per primitive; the quad corners are generated from gl_VertexID (triangle strip).

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

uniform vec2 u_viewport;

out vec2 v_pixel;
out vec2 v_uv;
flat out vec4 v_shape;
flat out vec4 v_radii;
flat out vec4 v_color;
flat out vec4 v_color2;
flat out vec4 v_gradient;
flat out vec4 v_params;
flat out vec4 v_clip;
flat out vec4 v_rclip;
flat out vec4 v_clip2;

void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
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
