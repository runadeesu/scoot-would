#version 450
// 13 tap bloom downsample (first pass applies threshold + Karis average)
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D texSrc;
layout(set = 3, binding = 0) uniform BloomParams { vec4 params; } bp;  // xy = src texel, z = first pass, w = threshold

vec3 karis(vec3 c) { return c / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722)) * 0.25); }

void main() {
    vec2 t = bp.params.xy;
    vec3 a = textureLod(texSrc, vUV + t * vec2(-2, -2), 0.0).rgb;
    vec3 b = textureLod(texSrc, vUV + t * vec2(0, -2), 0.0).rgb;
    vec3 c = textureLod(texSrc, vUV + t * vec2(2, -2), 0.0).rgb;
    vec3 d = textureLod(texSrc, vUV + t * vec2(-2, 0), 0.0).rgb;
    vec3 e = textureLod(texSrc, vUV, 0.0).rgb;
    vec3 f = textureLod(texSrc, vUV + t * vec2(2, 0), 0.0).rgb;
    vec3 g = textureLod(texSrc, vUV + t * vec2(-2, 2), 0.0).rgb;
    vec3 h = textureLod(texSrc, vUV + t * vec2(0, 2), 0.0).rgb;
    vec3 i = textureLod(texSrc, vUV + t * vec2(2, 2), 0.0).rgb;
    vec3 j = textureLod(texSrc, vUV + t * vec2(-1, -1), 0.0).rgb;
    vec3 k = textureLod(texSrc, vUV + t * vec2(1, -1), 0.0).rgb;
    vec3 l = textureLod(texSrc, vUV + t * vec2(-1, 1), 0.0).rgb;
    vec3 m = textureLod(texSrc, vUV + t * vec2(1, 1), 0.0).rgb;
    vec3 col;
    if (bp.params.z > 0.5) {
        vec3 g0 = karis((a + b + d + e) * 0.25), g1 = karis((b + c + e + f) * 0.25);
        vec3 g2 = karis((d + e + g + h) * 0.25), g3 = karis((e + f + h + i) * 0.25);
        vec3 g4 = karis((j + k + l + m) * 0.25);
        col = g4 * 0.5 + (g0 + g1 + g2 + g3) * 0.125;
        float br = max(col.r, max(col.g, col.b));
        float soft = clamp(br - bp.params.w * 0.5, 0.0, bp.params.w);
        soft = soft * soft / (4.0 * bp.params.w + 1e-4);
        col *= max(soft, br - bp.params.w) / max(br, 1e-4);
    } else {
        col = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    }
    outColor = vec4(max(col, vec3(0.0)), 1.0);
}
