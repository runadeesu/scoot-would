#version 450
#include "common.glsl"
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec3 vWorld;
layout(location = 0) out vec4 outColor;
layout(set = 3, binding = 0) uniform FrameUBO { FrameData frame; };
void main() {
    vec2 p = vUV * 2.0 - 1.0;
    float r = dot(p, p);
    if (r > 1.0) discard;
    float a = (1.0 - r) * (1.0 - r) * vColor.a;
    // premultiplied: rgb may be emissive (sparks) or lit dust
    vec3 ambient = evalSH(frame.sh, vec3(0, 1, 0)) * frame.sunColor.w * 0.8 + frame.sunColor.rgb * frame.sunDir.w * 0.25;
    vec3 col = vColor.rgb * (vColor.a < 0.0 ? 1.0 : 1.0);
    outColor = vec4(col * a, a);
}
