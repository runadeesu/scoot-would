#version 450
// screen space ambient occlusion (normal oriented hemisphere, 12 samples)
#include "common.glsl"
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outAO;
layout(set = 2, binding = 0) uniform sampler2D texDepth;
layout(set = 2, binding = 1) uniform sampler2D texNormal;
layout(set = 3, binding = 0) uniform FrameUBO { FrameData frame; };
layout(set = 3, binding = 1) uniform SSAOParams { vec4 params; } ssao;  // radius, intensity, bias, power

vec3 viewPos(vec2 uv) {
    float d = textureLod(texDepth, uv, 0.0).r;
    vec4 ndc = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, max(d, 1e-7), 1.0);
    vec4 v = frame.invProj * ndc;
    return v.xyz / v.w;
}

void main() {
    float depth = textureLod(texDepth, vUV, 0.0).r;
    if (depth <= 0.0) { outAO = vec4(1.0); return; }
    vec3 P = viewPos(vUV);
    vec3 N = octDecode(textureLod(texNormal, vUV, 0.0).rg);
    float radius = ssao.params.x;
    float noise = ign(gl_FragCoord.xy) * 6.2831;
    vec3 rv = vec3(cos(noise), sin(noise), 0.0);
    vec3 T = normalize(rv - N * dot(rv, N));
    vec3 B = cross(N, T);
    float occ = 0.0;
    const int S = 12;
    for (int i = 0; i < S; ++i) {
        float fi = float(i);
        float r = (fi + 0.5) / float(S);
        float a = fi * 2.39996;  // golden angle
        float z = sqrt(1.0 - r);
        vec3 h = vec3(cos(a) * sqrt(r), sin(a) * sqrt(r), z);
        float scale = mix(0.15, 1.0, r * r);
        vec3 sp = P + (T * h.x + B * h.y + N * h.z) * radius * scale;
        vec4 clip = frame.proj * vec4(sp, 1.0);
        vec2 suv = vec2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
        if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) continue;
        float sceneZ = viewPos(suv).z;
        float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(P.z - sceneZ), 1e-4));
        occ += (sceneZ >= sp.z + ssao.params.z ? 1.0 : 0.0) * rangeCheck;
    }
    float ao = 1.0 - occ / float(S) * ssao.params.y;
    outAO = vec4(pow(clamp(ao, 0.0, 1.0), ssao.params.w));
}
