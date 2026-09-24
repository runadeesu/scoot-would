#version 450
// split sum BRDF integration table (x = NoV, y = roughness)
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
#define PI 3.14159265359

vec2 hammersley(uint i, uint n) {
    uint b = i;
    b = (b << 16u) | (b >> 16u);
    b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
    b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
    b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
    b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
    return vec2(float(i) / float(n), float(b) * 2.3283064365386963e-10);
}

void main() {
    float NoV = max(vUV.x, 1e-3);
    float rough = max(vUV.y, 0.02);
    vec3 V = vec3(sqrt(1.0 - NoV * NoV), 0.0, NoV);
    float a = rough * rough;
    float A = 0.0, B = 0.0;
    const uint S = 256u;
    for (uint i = 0u; i < S; ++i) {
        vec2 xi = hammersley(i, S);
        float phi = 2.0 * PI * xi.x;
        float cosT = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
        float sinT = sqrt(1.0 - cosT * cosT);
        vec3 H = vec3(cos(phi) * sinT, sin(phi) * sinT, cosT);
        vec3 L = 2.0 * dot(V, H) * H - V;
        float NoL = clamp(L.z, 0.0, 1.0);
        float NoH = clamp(H.z, 0.0, 1.0);
        float VoH = clamp(dot(V, H), 0.0, 1.0);
        if (NoL > 0.0) {
            float k = a / 2.0;
            float gv = NoV / (NoV * (1.0 - k) + k);
            float gl = NoL / (NoL * (1.0 - k) + k);
            float G = gv * gl;
            float Gv = G * VoH / max(NoH * NoV, 1e-5);
            float Fc = pow(1.0 - VoH, 5.0);
            A += (1.0 - Fc) * Gv;
            B += Fc * Gv;
        }
    }
    outColor = vec4(A / float(S), B / float(S), 0.0, 1.0);
}
