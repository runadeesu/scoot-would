#version 450
// main forward pass / depth prepass vertex shader
#include "common.glsl"
#include "mesh_vertex.glsl"

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vTangent;
layout(location = 3) out vec2 vUV;
layout(location = 4) out vec4 vTint;
layout(location = 5) out vec4 vMisc;
#ifdef PREPASS
layout(location = 6) out vec4 vClip;
layout(location = 7) out vec4 vPrevClip;
#endif

invariant gl_Position;

void main() {
    InstanceData inst = fetchInstance();
    vec3 wp, wn;
    vec4 wt;
    meshVertex(inst, wp, wn, wt);
    vWorldPos = wp;
    vNormal = wn;
    vTangent = wt;
    vUV = inUV;
    vTint = inst.tint;
    vMisc = inst.misc;
    gl_Position = frame.viewProj * vec4(wp, 1.0);
#ifdef PREPASS
    vClip = gl_Position;
    vPrevClip = frame.prevViewProj * vec4(meshPrevPosition(inst), 1.0);
#endif
}
