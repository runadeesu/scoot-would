// shared instanced (optionally skinned) mesh vertex fetch
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUV;
#ifdef SKINNED
layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4 inWeights;
#endif

layout(std430, set = 0, binding = 0) readonly buffer Instances { InstanceData instances[]; };
layout(std430, set = 0, binding = 1) readonly buffer Visible { uint visible[]; };
#ifdef SKINNED
layout(std430, set = 0, binding = 2) readonly buffer Bones { mat4 bones[]; };
#endif

layout(set = 1, binding = 0) uniform FrameUBO { FrameData frame; };
layout(set = 1, binding = 1) uniform DrawUBO { uvec4 draw; };  // x = first visible index, y = cascade

InstanceData fetchInstance() { return instances[visible[draw.x + uint(gl_InstanceIndex)]]; }

void meshVertex(InstanceData inst, out vec3 worldPos, out vec3 worldNormal, out vec4 worldTangent) {
    vec4 lp = vec4(inPos, 1.0);
    vec3 ln = inNormal;
    vec3 lt = inTangent.xyz;
#ifdef SKINNED
    uint b = uint(inst.misc.x);
    mat4 skin = inWeights.x * bones[b + inJoints.x] + inWeights.y * bones[b + inJoints.y] +
                inWeights.z * bones[b + inJoints.z] + inWeights.w * bones[b + inJoints.w];
    lp = skin * lp;
    ln = mat3(skin) * ln;
    lt = mat3(skin) * lt;
#endif
    worldPos = (inst.model * lp).xyz;
    mat3 nm = mat3(inst.nrm0.xyz, inst.nrm1.xyz, inst.nrm2.xyz);
    worldNormal = normalize(nm * ln);
    worldTangent = vec4(normalize(mat3(inst.model) * lt), inTangent.w);
}
