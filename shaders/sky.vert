#version 450
// sky dome drawn at the far plane (reverse Z: depth 0)
#include "common.glsl"
layout(set = 1, binding = 0) uniform FrameUBO { FrameData frame; };
layout(location = 0) out vec3 vDir;

void main() {
    vec2 ndc = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    vec4 far = frame.invViewProj * vec4(ndc, 1e-6, 1.0);
    vDir = far.xyz / far.w - frame.cameraPos.xyz;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
