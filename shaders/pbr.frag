#version 450
// scoot would - physically based forward shading (glTF metallic/roughness)
#include "common.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vTangent;
layout(location = 3) in vec2 vUV;
layout(location = 4) in vec4 vTint;
layout(location = 5) in vec4 vMisc;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D texBaseColor;
layout(set = 2, binding = 1) uniform sampler2D texNormal;
layout(set = 2, binding = 2) uniform sampler2D texORM;
layout(set = 2, binding = 3) uniform sampler2D texEmissive;
layout(set = 2, binding = 4) uniform sampler2DArrayShadow texShadow;
layout(set = 2, binding = 5) uniform sampler2D texSSAO;
layout(set = 2, binding = 6) uniform samplerCube texEnv;
layout(set = 2, binding = 7) uniform sampler2D texBRDF;
layout(std430, set = 2, binding = 8) readonly buffer Lights { LightData lights[]; };

layout(set = 3, binding = 0) uniform FrameUBO { FrameData frame; };
layout(set = 3, binding = 1) uniform MaterialUBO {
    vec4 baseColorFactor;
    vec4 emissive;
    vec4 params;   // metallic, roughness, normalScale, aoStrength
    vec4 params2;  // uvScale, alphaCutoff (<0 = off), wear, tintable
} mat;

#include "lighting.glsl"

void main() {
    vec2 uv = vUV * mat.params2.x;
    vec4 base = texture(texBaseColor, uv) * mat.baseColorFactor;
    if (mat.params2.y >= 0.0 && base.a < mat.params2.y) discard;

    // customization / variation tint
    if (mat.params2.w > 0.5)
        base.rgb *= vTint.rgb;
    else
        base.rgb = mix(base.rgb, base.rgb * vTint.rgb, vTint.a);

    vec3 orm = texture(texORM, uv).rgb;
    float ao = mix(1.0, orm.r, mat.params.w);
    float rough = clamp(orm.g * mat.params.y, 0.03, 1.0);
    float metal = clamp(orm.b * mat.params.x, 0.0, 1.0);

    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 T = vTangent.xyz - N * dot(N, vTangent.xyz);
    if (dot(T, T) > 1e-8) {
        T = normalize(T);
        vec3 B = cross(N, T) * vTangent.w;
        vec3 tn = texture(texNormal, uv).xyz * 2.0 - 1.0;
        tn.xy *= mat.params.z;
        N = normalize(T * tn.x + B * tn.y + N * max(tn.z, 0.05));
    }

    // large scale variation and grime so tiled surfaces do not look uniform
    float wear = mat.params2.z;
    if (wear > 0.0) {
        float macro = fbm(vWorldPos.xz * 0.11 + vWorldPos.y * 0.07);
        float grime = smoothstep(0.35, 0.85, fbm(vWorldPos.xz * 0.9 + vec2(vWorldPos.y * 1.3, 0.0)));
        base.rgb *= mix(1.0 - 0.28 * wear, 1.0 + 0.12 * wear, macro);
        base.rgb *= 1.0 - grime * 0.25 * wear;
        rough = clamp(rough + (grime - 0.3) * 0.15 * wear, 0.03, 1.0);
        ao *= 1.0 - grime * 0.15 * wear;
    }

    vec3 albedo = base.rgb;
    vec3 f0 = mix(vec3(0.04), albedo, metal);
    vec3 V = normalize(frame.cameraPos.xyz - vWorldPos);
    float viewDepth = -(frame.view * vec4(vWorldPos, 1.0)).z;
    vec2 screenUV = gl_FragCoord.xy * frame.viewport.zw;

    vec3 diffuse = vec3(0.0), specular = vec3(0.0);

    // sun
    vec3 L = normalize(frame.sunDir.xyz);
    float shadow = sampleShadow(texShadow, vWorldPos, normalize(vNormal), L, viewDepth, gl_FragCoord.xy);
    {
        vec3 d;
        vec3 s = brdfDirect(N, V, L, albedo, metal, rough, f0, d);
        vec3 radiance = frame.sunColor.rgb * frame.sunDir.w * shadow;
        diffuse += d * radiance;
        specular += s * radiance;
    }

    // local lights (street lamps, spots)
    int lightCount = int(frame.misc.y);
    for (int i = 0; i < lightCount; ++i) {
        LightData li = lights[i];
        vec3 toL = li.posRadius.xyz - vWorldPos;
        float dist = length(toL);
        if (dist > li.posRadius.w) continue;
        vec3 Ld = toL / max(dist, 1e-4);
        float att = lightAttenuation(dist, li.posRadius.w);
        if (li.dirType.w > 0.5) {
            float cd = dot(-Ld, normalize(li.dirType.xyz));
            att *= smoothstep(li.spot.y, li.spot.x, cd);
        }
        if (att <= 0.0) continue;
        vec3 d;
        vec3 s = brdfDirect(N, V, Ld, albedo, metal, rough, f0, d);
        vec3 radiance = li.colorIntensity.rgb * li.colorIntensity.w * att;
        diffuse += d * radiance;
        specular += s * radiance;
    }

    // image based ambient lighting
    float NoV = clamp(dot(N, V), 1e-4, 1.0);
    vec3 F = F_SchlickRoughness(f0, NoV, rough);
    vec2 brdf = texture(texBRDF, vec2(NoV, rough)).rg;
    float ssao = frame.misc.z > 0.5 ? texture(texSSAO, screenUV).r : 1.0;
    float occlusion = min(ao, ssao);
    float iblScale = frame.sunColor.w;
    vec3 irradiance = evalSH(frame.sh, N) * iblScale;
    vec3 R = reflect(-V, N);
    float mip = rough * frame.envParams.y;
    vec3 prefiltered = textureLod(texEnv, envDir(R, frame.envParams.x), mip).rgb * iblScale;
    // horizon occlusion: reflections pointing below the geometric surface are darkened
    float horizon = clamp(1.0 + 1.2 * dot(R, normalize(vNormal)), 0.0, 1.0);
    prefiltered *= horizon * horizon;
    float specOcc = clamp(pow(NoV + occlusion, exp2(-16.0 * rough - 1.0)) - 1.0 + occlusion, 0.0, 1.0);
    // in shadow the sky is partly blocked too: darken ambient slightly
    float ambientShadow = mix(0.72, 1.0, shadow);
    vec3 kd = (1.0 - F) * (1.0 - metal);
    diffuse += kd * albedo * irradiance * occlusion * ambientShadow;
    specular += prefiltered * (F * brdf.x + brdf.y) * specOcc * mix(0.6, 1.0, shadow);

    vec3 emissive = texture(texEmissive, uv).rgb * mat.emissive.rgb;
    vec3 color = diffuse + specular + emissive;

    int debugView = int(frame.misc.w);
    if (debugView == 1) color = albedo;
    else if (debugView == 2) color = N * 0.5 + 0.5;
    else if (debugView == 3) color = vec3(rough);
    else if (debugView == 4) color = vec3(occlusion);
    else if (debugView == 5) color = vec3(shadow);
    else if (debugView == 6) color = prefiltered * (F * brdf.x + brdf.y);
    else if (debugView == 7) color = textureLod(texEnv, envDir(R, frame.envParams.x), 0.0).rgb * iblScale;
    else if (debugView == 8) color = irradiance;

    color = applyFog(color, vWorldPos, V);
    outColor = vec4(color, base.a);
}
