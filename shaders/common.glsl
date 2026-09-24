// scoot would - shared shader definitions
// Resource convention (SDL GPU): vertex set0 = storage, set1 = uniforms;
// fragment set2 = samplers + storage, set3 = uniforms.

#define PI 3.14159265359
#define MAX_CASCADES 4

struct FrameData {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 invViewProj;
    mat4 prevViewProj;
    mat4 invView;
    mat4 invProj;
    mat4 shadowMatrices[MAX_CASCADES];
    vec4 cameraPos;      // xyz, w = time
    vec4 sunDir;         // xyz towards the sun, w = intensity
    vec4 sunColor;       // rgb, w = IBL intensity
    vec4 viewport;       // width, height, 1/width, 1/height
    vec4 cascadeSplits;  // view distance of each cascade end
    vec4 shadowParams;   // x = 1/shadow size, y = depth bias, z = normal bias, w = cascade count
    vec4 fogParams;      // x = density, y = height falloff, z = start distance, w = max opacity
    vec4 fogColor;       // rgb, w = sun inscatter strength
    vec4 misc;           // x = exposure, y = light count, z = ssao enabled, w = debug view
    vec4 envParams;      // x = env rotation (radians), y = spec mip count, z = sky intensity, w = wetness
    vec4 cascadeTexel;   // world size of one shadow texel per cascade
    vec4 sh[9];          // irradiance SH coefficients (rgb)
};

struct InstanceData {
    mat4 model;
    vec4 nrm0;
    vec4 nrm1;
    vec4 nrm2;
    vec4 tint;   // rgb tint, a = tint amount
    vec4 misc;   // x = bone offset, y = dither fade, z = unused, w = object id
};

struct LightData {
    vec4 posRadius;       // xyz position, w = radius
    vec4 colorIntensity;  // rgb, w = intensity
    vec4 dirType;         // xyz spot direction, w = type (0 point, 1 spot)
    vec4 spot;            // x = cos inner, y = cos outer
};

vec3 evalSH(vec4 sh[9], vec3 n) {
    // L2 spherical harmonics irradiance (coefficients pre-convolved with the cosine lobe)
    vec3 r = sh[0].rgb * 0.282095;
    r += sh[1].rgb * 0.488603 * n.y;
    r += sh[2].rgb * 0.488603 * n.z;
    r += sh[3].rgb * 0.488603 * n.x;
    r += sh[4].rgb * 1.092548 * n.x * n.y;
    r += sh[5].rgb * 1.092548 * n.y * n.z;
    r += sh[6].rgb * 0.315392 * (3.0 * n.z * n.z - 1.0);
    r += sh[7].rgb * 1.092548 * n.x * n.z;
    r += sh[8].rgb * 0.546274 * (n.x * n.x - n.y * n.y);
    return max(r, vec3(0.0));
}

// equirectangular direction <-> uv (u = longitude, v = latitude, v=0 at the top)
vec2 dirToEquirect(vec3 d, float rot) {
    float phi = atan(d.z, d.x) + rot;
    float theta = acos(clamp(d.y, -1.0, 1.0));
    return vec2(fract(phi / (2.0 * PI) + 0.5), theta / PI);
}

vec2 octEncode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 r = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return r;
}

vec3 octDecode(vec2 f) {
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash12(i), b = hash12(i + vec2(1, 0)), c = hash12(i + vec2(0, 1)), d = hash12(i + vec2(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) {
        v += a * valueNoise(p);
        p = p * 2.03 + vec2(17.1, 3.7);
        a *= 0.5;
    }
    return v;
}

// interleaved gradient noise for dithering
float ign(vec2 pix) { return fract(52.9829189 * fract(dot(pix, vec2(0.06711056, 0.00583715)))); }

// rotate a world direction into environment map space (matches the sky's equirect rotation)
vec3 envDir(vec3 d, float rot) {
    float c = cos(rot), s = sin(rot);
    return vec3(d.x * c - d.z * s, d.y, d.x * s + d.z * c);
}

float linearizeReverseZ(float d, float nearZ) { return nearZ / max(d, 1e-7); }
