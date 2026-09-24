// PBR lighting: GGX / Smith / Schlick, cascaded shadows, IBL, fog

float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float f = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * f * f + 1e-7);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float a) {
    float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-6);
}

vec3 F_Schlick(vec3 f0, float VoH) {
    float f = pow(1.0 - VoH, 5.0);
    return f + f0 * (1.0 - f);
}

vec3 F_SchlickRoughness(vec3 f0, float NoV, float rough) {
    return f0 + (max(vec3(1.0 - rough), f0) - f0) * pow(1.0 - NoV, 5.0);
}

// radiance leaving the surface for one light (without light color)
vec3 brdfDirect(vec3 N, vec3 V, vec3 L, vec3 albedo, float metal, float rough, vec3 f0, out vec3 diffuseOut) {
    vec3 H = normalize(V + L);
    float NoL = clamp(dot(N, L), 0.0, 1.0);
    float NoV = clamp(abs(dot(N, V)), 1e-4, 1.0);
    float NoH = clamp(dot(N, H), 0.0, 1.0);
    float VoH = clamp(dot(V, H), 0.0, 1.0);
    float a = max(rough * rough, 0.002);
    vec3 F = F_Schlick(f0, VoH);
    vec3 spec = D_GGX(NoH, a) * V_SmithGGXCorrelated(NoV, NoL, a) * F;
    vec3 kd = (1.0 - F) * (1.0 - metal);
    diffuseOut = kd * albedo / PI * NoL;
    return spec * NoL;
}

const vec2 kPoisson[12] = vec2[](
    vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696, 0.457), vec2(-0.203, 0.621),
    vec2(0.962, -0.195), vec2(0.473, -0.480), vec2(0.519, 0.767), vec2(0.185, -0.893),
    vec2(0.507, 0.064), vec2(0.896, 0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598));

float shadowCascade(sampler2DArrayShadow smap, int c, vec3 wp, vec3 N, vec3 L, float rot) {
    float texel = frame.cascadeTexel[c];
    float NoL = clamp(dot(N, L), 0.0, 1.0);
    vec3 p = wp + N * texel * frame.shadowParams.z * (1.5 - NoL) + L * texel * 0.5;
    vec4 sp = frame.shadowMatrices[c] * vec4(p, 1.0);
    vec3 s = sp.xyz / sp.w;
    vec2 uv = s.xy * vec2(0.5, -0.5) + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || s.z > 1.0) return 1.0;
    float ref = s.z - frame.shadowParams.y;
    float radius = 1.6 * frame.shadowParams.x;
    float cr = cos(rot), sr = sin(rot);
    mat2 R = mat2(cr, sr, -sr, cr);
    float sum = 0.0;
    for (int i = 0; i < 12; ++i) {
        vec2 o = R * kPoisson[i] * radius;
        sum += texture(smap, vec4(uv + o, float(c), ref));
    }
    return sum / 12.0;
}

float sampleShadow(sampler2DArrayShadow smap, vec3 wp, vec3 N, vec3 L, float viewDepth, vec2 pix) {
    int count = int(frame.shadowParams.w);
    if (count == 0) return 1.0;
    float rot = ign(pix) * 6.2831;
    for (int c = 0; c < count; ++c) {
        float split = frame.cascadeSplits[c];
        if (viewDepth < split) {
            float s = shadowCascade(smap, c, wp, N, L, rot);
            // blend towards the next cascade near the split to hide the seam
            float blendStart = split * 0.88;
            if (c + 1 < count && viewDepth > blendStart) {
                float t = (viewDepth - blendStart) / (split - blendStart);
                s = mix(s, shadowCascade(smap, c + 1, wp, N, L, rot), t);
            } else if (c + 1 == count) {
                // fade out at the end of the last cascade
                s = mix(s, 1.0, smoothstep(split * 0.85, split, viewDepth));
            }
            return s;
        }
    }
    return 1.0;
}

float lightAttenuation(float dist, float radius) {
    float d = dist / radius;
    float f = clamp(1.0 - d * d * d * d, 0.0, 1.0);
    return f * f / (dist * dist + 1.0);
}

vec3 applyFog(vec3 color, vec3 wp, vec3 V) {
    float density = frame.fogParams.x;
    if (density <= 0.0) return color;
    vec3 cam = frame.cameraPos.xyz;
    vec3 d = wp - cam;
    float dist = length(d);
    float falloff = max(frame.fogParams.y, 1e-4);
    float dy = d.y;
    float heightTerm = exp(-falloff * max(cam.y, 0.0));
    float integral = abs(dy) > 0.01 ? (1.0 - exp(-falloff * dy)) / (falloff * dy) : 1.0;
    float fogDist = max(dist - frame.fogParams.z, 0.0);
    float amount = 1.0 - exp(-density * heightTerm * integral * fogDist);
    amount = min(amount, frame.fogParams.w);
    vec3 dir = -V;
    vec3 horizon = normalize(vec3(dir.x, 0.05, dir.z));
    vec3 fogCol = evalSH(frame.sh, horizon) * frame.sunColor.w * frame.fogColor.rgb;
    float sunAmt = pow(max(dot(dir, frame.sunDir.xyz), 0.0), 12.0) * frame.fogColor.w;
    fogCol += frame.sunColor.rgb * frame.sunDir.w * sunAmt * 0.05;
    return mix(color, fogCol, amount);
}
