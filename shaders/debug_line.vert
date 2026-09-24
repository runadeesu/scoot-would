#version 450
// physics / gameplay debug lines
#include "common.glsl"
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 vColor;
layout(set = 1, binding = 0) uniform FrameUBO { FrameData frame; };
void main() {
    vColor = inColor;
    gl_Position = frame.viewProj * vec4(inPos, 1.0);
}
