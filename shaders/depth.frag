#version 450
// depth prepass: writes view space normals (octahedral) for SSAO
#include "common.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vTangent;
layout(location = 3) in vec2 vUV;
layout(location = 4) in vec4 vTint;
layout(location = 5) in vec4 vMisc;

layout(location = 0) out vec2 outNormal;

layout(set = 2, binding = 0) uniform sampler2D texBaseColor;
layout(set = 3, binding = 0) uniform FrameUBO { FrameData frame; };
layout(set = 3, binding = 1) uniform MaterialUBO { vec4 baseColorFactor; vec4 emissive; vec4 params; vec4 params2; } mat;

void main() {
    if (mat.params2.y >= 0.0) {
        vec2 uv = vUV * mat.params2.x;
        float a = texture(texBaseColor, uv).a * mat.baseColorFactor.a;
        // same mip compensated alpha test as pbr.frag (surface.glsl alphaTestValue)
        a *= 1.0 + max(textureQueryLod(texBaseColor, uv).x, 0.0) * 0.25;
        if (a < mat.params2.y) discard;
    }
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) n = -n;
    vec3 vn = normalize(mat3(frame.view) * n);
    outNormal = octEncode(vn);
}
