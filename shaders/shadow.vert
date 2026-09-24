#version 450
// cascaded shadow map caster
#include "common.glsl"
#include "mesh_vertex.glsl"

layout(location = 0) out vec2 vUV;

void main() {
    InstanceData inst = fetchInstance();
    vec3 wp, wn;
    vec4 wt;
    meshVertex(inst, wp, wn, wt);
    vUV = inUV;
    gl_Position = frame.shadowMatrices[draw.y] * vec4(wp, 1.0);
}
