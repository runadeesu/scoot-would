#version 450
// prefilter the environment (equirect HDR) into one face / mip of the specular cube map (GGX importance sampling)
#include "common.glsl"
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D texEquirect;
layout(set = 3, binding = 0) uniform PrefilterParams { vec4 params; } pf;  // x = face, y = roughness, z = src width, w = clamp

vec3 faceDir(int face, vec2 uv) {
    float u = uv.x * 2.0 - 1.0, v = uv.y * 2.0 - 1.0;
    if (face == 0) return normalize(vec3(1.0, -v, -u));
    if (face == 1) return normalize(vec3(-1.0, -v, u));
    if (face == 2) return normalize(vec3(u, 1.0, v));
    if (face == 3) return normalize(vec3(u, -1.0, -v));
    if (face == 4) return normalize(vec3(u, -v, 1.0));
    return normalize(vec3(-u, -v, -1.0));
}

vec2 hammersley(uint i, uint n) {
    uint b = i;
    b = (b << 16u) | (b >> 16u);
    b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
    b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
    b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
    b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
    return vec2(float(i) / float(n), float(b) * 2.3283064365386963e-10);
}

vec3 sampleEnv(vec3 d, float lod) {
    vec3 c = textureLod(texEquirect, dirToEquirect(d, 0.0), lod).rgb;
    return min(c, vec3(pf.params.w));  // clamp the sun so it does not produce fireflies
}

void main() {
    vec3 N = faceDir(int(pf.params.x), vUV);
    float rough = pf.params.y;
    if (rough < 0.01) { outColor = vec4(sampleEnv(N, 0.0), 1.0); return; }
    vec3 up = abs(N.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);
    float a = rough * rough;
    const uint S = 96u;
    vec3 sum = vec3(0.0);
    float w = 0.0;
    float srcTexelSA = 4.0 * PI / (6.0 * pf.params.z * pf.params.z * 0.25);
    for (uint i = 0u; i < S; ++i) {
        vec2 xi = hammersley(i, S);
        float phi = 2.0 * PI * xi.x;
        float cosT = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
        float sinT = sqrt(1.0 - cosT * cosT);
        vec3 H = T * (cos(phi) * sinT) + B * (sin(phi) * sinT) + N * cosT;
        vec3 L = normalize(2.0 * dot(N, H) * H - N);
        float NoL = dot(N, L);
        if (NoL > 0.0) {
            float NoH = max(dot(N, H), 0.0);
            float a2 = a * a;
            float f = (NoH * a2 - NoH) * NoH + 1.0;
            float D = a2 / (PI * f * f);
            float pdf = D * 0.25;
            float sampleSA = 1.0 / (float(S) * pdf + 1e-4);
            float lod = 0.5 * log2(max(sampleSA / srcTexelSA, 1.0)) + 1.0;
            sum += sampleEnv(L, lod) * NoL;
            w += NoL;
        }
    }
    outColor = vec4(sum / max(w, 1e-4), 1.0);
}
