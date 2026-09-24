#version 450
// subtle camera motion blur (reprojection of depth with the previous view projection)
#include "common.glsl"
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D texColor;
layout(set = 2, binding = 1) uniform sampler2D texDepth;
layout(set = 3, binding = 0) uniform FrameUBO { FrameData frame; };
layout(set = 3, binding = 1) uniform MBParams { vec4 params; } mb;  // x = strength, y = max length (uv)

void main() {
    float d = textureLod(texDepth, vUV, 0.0).r;
    vec4 ndc = vec4(vUV.x * 2.0 - 1.0, 1.0 - vUV.y * 2.0, max(d, 1e-6), 1.0);
    vec4 wp = frame.invViewProj * ndc;
    wp /= wp.w;
    vec4 prev = frame.prevViewProj * wp;
    vec2 prevUV = vec2(prev.x / prev.w * 0.5 + 0.5, 0.5 - prev.y / prev.w * 0.5);
    vec2 vel = (vUV - prevUV) * mb.params.x;
    float len = length(vel);
    if (len > mb.params.y) vel *= mb.params.y / len;
    vec3 sum = vec3(0.0);
    float jitter = ign(gl_FragCoord.xy);
    const int N = 8;
    for (int i = 0; i < N; ++i) {
        float t = (float(i) + jitter) / float(N) - 0.5;
        sum += textureLod(texColor, vUV + vel * t, 0.0).rgb;
    }
    outColor = vec4(sum / float(N), 1.0);
}
