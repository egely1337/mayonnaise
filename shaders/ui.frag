#version 330 core
// Uber-shader for every 2D primitive. Shapes are signed distance fields evaluated in pixels, so
// edges are anti-aliased over exactly one device pixel at any scale.

in vec2 v_pixel;
in vec2 v_uv;
flat in vec4 v_shape;
flat in vec4 v_radii;
flat in vec4 v_color;
flat in vec4 v_color2;
flat in vec4 v_gradient;
flat in vec4 v_params;
flat in vec4 v_clip;
flat in vec4 v_clip2;

uniform sampler2D u_texture;
uniform vec2 u_viewport;

out vec4 o_color;

const int kFill = 0;
const int kShadow = 1;
const int kImage = 2;
const int kGlyph = 3;
const int kLine = 4;
const int kGradient = 5;
const int kBackdrop = 6;
const int kArc = 7;

// Rounded box with per-corner radii (tl, tr, br, bl); y grows downwards.
float sdRoundBox(vec2 p, vec2 halfSize, vec4 r) {
    float radius = p.x > 0.0 ? (p.y > 0.0 ? r.z : r.y) : (p.y > 0.0 ? r.w : r.x);
    vec2 q = abs(p) - halfSize + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

float sdSegment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}

// Arc of a circle centred at the origin, from angle a0 sweeping `sweep` radians (clockwise on screen).
float sdArc(vec2 p, float radius, float a0, float sweep) {
    float a = atan(p.y, p.x) - a0;
    a = mod(a, 6.28318530718);
    if (a <= sweep) return abs(length(p) - radius);
    vec2 e0 = radius * vec2(cos(a0), sin(a0));
    vec2 e1 = radius * vec2(cos(a0 + sweep), sin(a0 + sweep));
    return min(length(p - e0), length(p - e1));
}

// --- Gaussian rounded-box shadow (Evan Wallace, "Fast rounded rectangle shadows") -------------
float gaussian(float x, float sigma) {
    return exp(-(x * x) / (2.0 * sigma * sigma)) / (2.50662827463 * sigma);
}
vec2 erf2(vec2 x) {
    vec2 s = sign(x), a = abs(x);
    x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    x *= x;
    return s - s / (x * x);
}
float shadowX(float x, float y, float sigma, float corner, vec2 halfSize) {
    float delta = min(halfSize.y - corner - abs(y), 0.0);
    float curved = halfSize.x - corner + sqrt(max(0.0, corner * corner - delta * delta));
    vec2 integral = 0.5 + 0.5 * erf2((x + vec2(-curved, curved)) * (0.70710678 / sigma));
    return integral.y - integral.x;
}
float roundedBoxShadow(vec2 p, vec2 halfSize, float sigma, float corner) {
    float low = p.y - halfSize.y;
    float high = p.y + halfSize.y;
    float start = clamp(-3.0 * sigma, low, high);
    float end = clamp(3.0 * sigma, low, high);
    float stepSize = (end - start) / 4.0;
    float y = start + stepSize * 0.5;
    float value = 0.0;
    for (int i = 0; i < 4; i++) {
        value += shadowX(p.x, p.y - y, sigma, corner, halfSize) * gaussian(y, sigma) * stepSize;
        y += stepSize;
    }
    return value;
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

float coverage(float d) { return clamp(0.5 - d, 0.0, 1.0); }

void main() {
    int type = int(v_params.x + 0.5);
    vec2 p = v_pixel - v_shape.xy;
    vec4 color = v_color;
    float alpha = 1.0;

    if (type == kFill) {
        float d = sdRoundBox(p, v_shape.zw, v_radii);
        float stroke = v_params.y;
        alpha = coverage(d);
        if (stroke > 0.0) {
            // Inner border: blend border colour over fill within `stroke` pixels of the edge.
            float inner = coverage(d + stroke);
            color = mix(v_color2, v_color, inner);
        }
    } else if (type == kShadow) {
        alpha = roundedBoxShadow(p, v_shape.zw, max(v_params.z, 0.5), v_radii.x);
    } else if (type == kImage) {
        alpha = coverage(sdRoundBox(p, v_shape.zw, v_radii));
        color = texture(u_texture, v_uv) * v_color.a;
    } else if (type == kGlyph) {
        float a = texture(u_texture, v_uv).r;
        // Gamma tweak per text colour: dark-on-light text needs a little emboldening to match
        // the weight of macOS text rendering, light-on-dark slightly less.
        alpha = pow(a, v_params.w);
    } else if (type == kLine) {
        float d = sdSegment(v_pixel, v_shape.xy, v_shape.zw) - v_params.y;
        alpha = coverage(d);
    } else if (type == kGradient) {
        alpha = coverage(sdRoundBox(p, v_shape.zw, v_radii));
        vec2 g0 = v_gradient.xy;
        vec2 g1 = v_gradient.zw;
        vec2 dir = g1 - g0;
        float t = clamp(dot(v_pixel - g0, dir) / max(dot(dir, dir), 1e-6), 0.0, 1.0);
        color = mix(v_color, v_color2, t);
    } else if (type == kBackdrop) {
        alpha = coverage(sdRoundBox(p, v_shape.zw, v_radii));
        vec2 uv = vec2(v_pixel.x / u_viewport.x, 1.0 - v_pixel.y / u_viewport.y);
        vec3 bg = texture(u_texture, uv).rgb;
        // Vibrancy: push the blurred backdrop away from its luminance.
        float l = dot(bg, vec3(0.2126, 0.7152, 0.0722));
        bg = clamp(mix(vec3(l), bg, v_params.z), 0.0, 1.0);
        vec3 tinted = bg * (1.0 - v_color.a) + v_color.rgb;  // tint is premultiplied
        tinted += (hash(floor(v_pixel)) - 0.5) * v_params.w;
        color = vec4(tinted, 1.0);
    } else if (type == kArc) {
        float d = sdArc(p, v_shape.z, v_gradient.x, v_gradient.y) - v_params.y;
        alpha = coverage(d);
    }

    // Clip: rounded rectangle in pixels.
    vec2 clipCenter = (v_clip.xy + v_clip.zw) * 0.5;
    vec2 clipHalf = (v_clip.zw - v_clip.xy) * 0.5;
    float cr = v_clip2.x;
    float clipD = sdRoundBox(v_pixel - clipCenter, clipHalf, vec4(cr));
    alpha *= coverage(clipD);

    o_color = color * alpha;
}
