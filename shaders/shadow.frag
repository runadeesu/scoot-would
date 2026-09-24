#version 450
// alpha tested shadow caster (foliage, fences)
#include "common.glsl"

layout(location = 0) in vec2 vUV;
layout(set = 2, binding = 0) uniform sampler2D texBaseColor;
layout(set = 3, binding = 0) uniform FrameUBO { FrameData frame; };
layout(set = 3, binding = 1) uniform MaterialUBO { vec4 baseColorFactor; vec4 emissive; vec4 params; vec4 params2; } mat;

void main() {
    vec2 uv = vUV * mat.params2.x;
    float a = texture(texBaseColor, uv).a * mat.baseColorFactor.a;
    a *= 1.0 + max(textureQueryLod(texBaseColor, uv).x, 0.0) * 0.25;
    if (a < max(mat.params2.y, 0.3)) discard;
}
