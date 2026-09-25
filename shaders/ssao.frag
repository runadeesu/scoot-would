#version 450
// screen space ambient occlusion (normal oriented hemisphere, 12 samples) and sun contact shadows
// (short ray march towards the sun through the depth buffer: the small scale shadows a shadow map texel is too
// coarse for - shoes on the deck, wheels on the ground, fingers on the grips). With TAA the noise changes
// every frame and is accumulated.
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

float contactShadow(vec3 P, vec3 N, float noise) {
    vec3 L = normalize(mat3(frame.view) * frame.sunDir.xyz);
    if (dot(N, L) <= 0.02 || frame.sunDir.w <= 0.0) return 1.0;  // facing away: the shadow map has it
    float dist = -P.z;
    float len = clamp(dist * 0.025, 0.12, 0.6);  // metres; grows with distance so it stays a few pixels long
    float thickness = 0.14 + dist * 0.01;        // assumed depth of what is seen in the depth buffer
    vec3 origin = P + N * (0.006 + dist * 0.0015);
    const int STEPS = 10;
    for (int i = 0; i < STEPS; ++i) {
        float t = (float(i) + noise) / float(STEPS);
        vec3 sp = origin + L * (len * t * t);  // denser near the surface
        vec4 clip = frame.proj * vec4(sp, 1.0);
        vec2 suv = vec2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
        if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) break;
        float d = viewPos(suv).z - sp.z;  // > 0: a surface in the depth buffer is in front of the ray
        if (d > 0.003 + dist * 0.0012 && d < thickness) {
            float fade = 1.0 - smoothstep(24.0, 40.0, dist);
            return 1.0 - fade * (1.0 - 0.5 * t);  // occluders further along the ray give a softer shadow
        }
    }
    return 1.0;
}

void main() {
    float depth = textureLod(texDepth, vUV, 0.0).r;
    if (depth <= 0.0) { outAO = vec4(1.0); return; }
    vec3 P = viewPos(vUV);
    vec3 N = octDecode(textureLod(texNormal, vUV, 0.0).rg);
    float radius = ssao.params.x;
    float n0 = ign(gl_FragCoord.xy + frame.taa.w * 5.588);
    float noise = n0 * 6.2831;
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
    float cs = contactShadow(P, N, fract(n0 + 0.618));
    outAO = vec4(pow(clamp(ao, 0.0, 1.0), ssao.params.w), cs, 0.0, 1.0);
}
