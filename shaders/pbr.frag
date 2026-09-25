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
layout(set = 2, binding = 8) uniform sampler2D texDetail;
layout(std430, set = 2, binding = 9) readonly buffer Lights { LightData lights[]; };

layout(set = 3, binding = 0) uniform FrameUBO { FrameData frame; };
layout(set = 3, binding = 1) uniform MaterialUBO {
    vec4 baseColorFactor;
    vec4 emissive;
    vec4 params;   // metallic, roughness, normalScale, aoStrength
    vec4 params2;  // uvScale, alphaCutoff (<0 = off), wear, tintable
    vec4 params3;  // shading model (0 std, 1 skin, 2 cloth, 3 interior), room depth, interior light, shop
    vec4 params4;  // 1 / detail scale (0 = off), detail strength, anti tiling, unused
} mat;

#include "lighting.glsl"
#include "surface.glsl"

void main() {
    vec2 uv = vUV * mat.params2.x;
    bool antiTile = mat.params4.z > 0.5;
    TileSample ts = tileSample(uv, antiTile);
    vec4 base = sampleTile(texBaseColor, ts) * mat.baseColorFactor;
    // alpha test; alpha is scaled up with the mip level so foliage keeps its coverage in the distance
    if (mat.params2.y >= 0.0 && alphaTestValue(base.a, textureQueryLod(texBaseColor, uv).x) < mat.params2.y) discard;
    int shadingModel = int(mat.params3.x + 0.5);

    // customization / variation tint
    if (mat.params2.w > 0.5)
        base.rgb *= vTint.rgb;
    else
        base.rgb = mix(base.rgb, base.rgb * vTint.rgb, vTint.a);

    vec3 orm = sampleTile(texORM, ts).rgb;
    float ao = mix(1.0, orm.r, mat.params.w);
    // baked vertex occlusion (foliage crowns) lives in the magnitude of tangent.w
    float vertexAO = abs(vTangent.w);
    vertexAO = vertexAO < 0.01 ? 1.0 : min(vertexAO, 1.0);
    ao *= vertexAO;
    float rough = clamp(orm.g * mat.params.y, 0.03, 1.0);
    float metal = clamp(orm.b * mat.params.x, 0.0, 1.0);

    vec3 N = normalize(vNormal);
    // foliage cards keep their bent crown normals on both sides
    if (!gl_FrontFacing && shadingModel != 4) N = -N;
    vec3 T = vTangent.xyz - N * dot(N, vTangent.xyz);
    if (dot(T, T) > 1e-8) {
        T = normalize(T);
        vec3 B = cross(N, T) * (vTangent.w < 0.0 ? -1.0 : 1.0);
        vec3 tn = sampleTile(texNormal, ts).xyz * 2.0 - 1.0;
        tn.xy *= mat.params.z;
        // tiling micro detail (world scale, faded with distance)
        if (mat.params4.x > 0.0) {
            float fade = 1.0 - smoothstep(6.0, 22.0, distance(frame.cameraPos.xyz, vWorldPos));
            vec3 dn = texture(texDetail, vUV * mat.params4.x).xyz * 2.0 - 1.0;
            tn = normalize(vec3(tn.xy + dn.xy * mat.params4.y * fade, tn.z));
        }
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
    if (shadingModel == 1) f0 = vec3(0.028);

    if (shadingModel == 3) {
        // interior mapped window glass
        vec3 Ng = normalize(vNormal);
        if (!gl_FrontFacing) Ng = -Ng;
        float skyLum = dot(evalSH(frame.sh, vec3(0.0, 1.0, 0.0)) * frame.sunColor.w, vec3(0.2126, 0.7152, 0.0722));
        vec3 room = interiorRoom(vUV, vWorldPos, Ng, -V, mat.params3.y, mat.params3.z, mat.params3.w > 0.5, frame.extra.x, skyLum,
                                 frame.misc.x);
        float NoVg = clamp(dot(Ng, V), 1e-3, 1.0);
        float Fg = 0.04 + 0.96 * pow(1.0 - NoVg, 5.0);
        vec3 Rg = reflect(-V, Ng);
        vec3 refl = textureLod(texEnv, envDir(Rg, frame.envParams.x), 0.5).rgb * frame.sunColor.w;
        refl = urbanReflection(refl, Rg, frame.extra.w);
        vec3 Ls = normalize(frame.sunDir.xyz);
        float sunSpec = pow(clamp(dot(Rg, Ls), 0.0, 1.0), 900.0) * 40.0;
        float sh = sampleShadow(texShadow, vWorldPos, Ng, Ls, -(frame.view * vec4(vWorldPos, 1.0)).z, gl_FragCoord.xy);
        refl += frame.sunColor.rgb * frame.sunDir.w * sunSpec * sh;
        float grime = mat.params2.z > 0.0 ? smoothstep(0.55, 0.9, fbm(vWorldPos.xz * 2.0 + vWorldPos.y)) * 0.3 : 0.0;
        // shop windows: big panes, more reflective in daylight
        float reflAmt = mix(Fg, 0.25, 0.15) + (mat.params3.w > 0.5 ? 0.08 : 0.0);
        vec3 glassCol = room * (1.0 - Fg) * 0.82 + refl * reflAmt;
        glassCol = mix(glassCol, vec3(0.2, 0.19, 0.17) * skyLum * 0.3, grime);
        glassCol = applyFog(glassCol, vWorldPos, V);
        outColor = vec4(glassCol, 1.0);
        return;
    }
    float viewDepth = -(frame.view * vec4(vWorldPos, 1.0)).z;
    vec2 screenUV = gl_FragCoord.xy * frame.viewport.zw;

    vec3 diffuse = vec3(0.0), specular = vec3(0.0);

    // ambient occlusion (r) and contact shadows (g) from the SSAO pass
    vec2 ssaoCs = frame.misc.z > 0.5 ? texture(texSSAO, screenUV).rg : vec2(1.0);

    // sun
    vec3 L = normalize(frame.sunDir.xyz);
    float shadow = sampleShadow(texShadow, vWorldPos, normalize(vNormal), L, viewDepth, gl_FragCoord.xy);
    shadow = min(shadow, ssaoCs.g);
    {
        vec3 d;
        vec3 s = brdfDirect(N, V, L, albedo, metal, rough, f0, d);
        vec3 sun = frame.sunColor.rgb * frame.sunDir.w;
        if (shadingModel == 1) {
            diffuse += skinDiffuse(N, L, albedo * (1.0 - metal), shadow) * sun;
        } else if (shadingModel == 2) {
            float w = clamp((dot(N, L) + 0.3) / 1.3, 0.0, 1.0);
            diffuse += albedo / PI * w * sun * shadow;
            specular += clothSheen(N, V, L, albedo, rough) * sun * shadow;
        } else if (shadingModel == 4) {
            diffuse += foliageDiffuse(N, V, L, albedo, vertexAO) * sun * shadow;
        } else {
            diffuse += d * sun * shadow;
        }
        specular += s * sun * shadow;
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
    float ssao = ssaoCs.r;
    float occlusion = min(ao, ssao);
    float iblScale = frame.sunColor.w;
    vec3 irradiance = evalSH(frame.sh, N) * iblScale;
    vec3 R = reflect(-V, N);
    float mip = rough * frame.envParams.y;
    vec3 prefiltered = textureLod(texEnv, envDir(R, frame.envParams.x), mip).rgb * iblScale;
    prefiltered = urbanReflection(prefiltered, R, frame.extra.w * (1.0 - rough * 0.5));
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
