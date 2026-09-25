#version 450
// depth aware separable blur for SSAO (r) and contact shadows (g)
#include "common.glsl"
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outAO;
layout(set = 2, binding = 0) uniform sampler2D texAO;
layout(set = 2, binding = 1) uniform sampler2D texDepth;
layout(set = 3, binding = 0) uniform FrameUBO { FrameData frame; };
layout(set = 3, binding = 1) uniform BlurParams { vec4 params; } blur;  // xy = texel step, z = near

void main() {
    float d0 = linearizeReverseZ(textureLod(texDepth, vUV, 0.0).r, blur.params.z);
    vec2 sum = vec2(0.0);
    float wsum = 0.0;
    for (int i = -3; i <= 3; ++i) {
        vec2 uv = vUV + blur.params.xy * float(i);
        float d = linearizeReverseZ(textureLod(texDepth, uv, 0.0).r, blur.params.z);
        float w = exp(-float(i * i) / 8.0) * (1.0 / (1e-3 + abs(d - d0) / max(d0, 0.1) * 20.0));
        sum += textureLod(texAO, uv, 0.0).rg * w;
        wsum += w;
    }
    outAO = vec4(sum / max(wsum, 1e-5), 0.0, 1.0);
}
