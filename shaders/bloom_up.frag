#version 450
// 9 tap tent upsample, additively blended into the next larger mip
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D texSrc;
layout(set = 3, binding = 0) uniform BloomParams { vec4 params; } bp;  // xy = src texel, z = radius, w = weight

void main() {
    vec2 t = bp.params.xy * bp.params.z;
    vec3 s = textureLod(texSrc, vUV + vec2(-t.x, -t.y), 0.0).rgb;
    s += textureLod(texSrc, vUV + vec2(0, -t.y), 0.0).rgb * 2.0;
    s += textureLod(texSrc, vUV + vec2(t.x, -t.y), 0.0).rgb;
    s += textureLod(texSrc, vUV + vec2(-t.x, 0), 0.0).rgb * 2.0;
    s += textureLod(texSrc, vUV, 0.0).rgb * 4.0;
    s += textureLod(texSrc, vUV + vec2(t.x, 0), 0.0).rgb * 2.0;
    s += textureLod(texSrc, vUV + vec2(-t.x, t.y), 0.0).rgb;
    s += textureLod(texSrc, vUV + vec2(0, t.y), 0.0).rgb * 2.0;
    s += textureLod(texSrc, vUV + vec2(t.x, t.y), 0.0).rgb;
    outColor = vec4(s / 16.0 * bp.params.w, 1.0);
}
