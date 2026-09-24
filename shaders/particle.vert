#version 450
// camera facing particles (dust, sparks); vertices are expanded on the CPU
#include "common.glsl"
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec3 vWorld;
layout(set = 1, binding = 0) uniform FrameUBO { FrameData frame; };
void main() {
    vUV = inUV;
    vColor = inColor;
    vWorld = inPos;
    gl_Position = frame.viewProj * vec4(inPos, 1.0);
}
