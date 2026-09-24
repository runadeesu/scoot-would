// scoot would - surface helpers for pbr.frag: anti tiling texture sampling, skin / cloth diffuse,
// interior mapped windows

// ---------------------------------------------------------------------------------------------
// anti tiling: blend two differently offset samples, switched by a low frequency value noise
// (large surfaces stop showing the same texture tile over and over)
struct TileSample {
    vec2 uvA, uvB;
    vec2 ddx, ddy;
    float blend;
    bool on;
};

float tileHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float tileNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(tileHash(i), tileHash(i + vec2(1, 0)), f.x), mix(tileHash(i + vec2(0, 1)), tileHash(i + vec2(1, 1)), f.x), f.y);
}

TileSample tileSample(vec2 uv, bool on) {
    TileSample t;
    t.ddx = dFdx(uv);
    t.ddy = dFdy(uv);
    t.on = on;
    t.uvA = uv;
    t.uvB = uv;
    t.blend = 0.0;
    if (on) {
        float k = tileNoise(uv * 0.37) * 8.0;
        float ia = floor(k), ib = ia + 1.0;
        t.uvA = uv + sin(vec2(3.0, 7.0) * ia) * 0.5;
        t.uvB = uv + sin(vec2(3.0, 7.0) * ib) * 0.5;
        t.blend = smoothstep(0.25, 0.75, fract(k));
    }
    return t;
}

vec4 sampleTile(sampler2D s, TileSample t) {
    if (!t.on) return textureGrad(s, t.uvA, t.ddx, t.ddy);
    vec4 a = textureGrad(s, t.uvA, t.ddx, t.ddy);
    vec4 b = textureGrad(s, t.uvB, t.ddx, t.ddy);
    return mix(a, b, t.blend);
}

// ---------------------------------------------------------------------------------------------
// skin: wrapped diffuse with a warm subsurface tint in the terminator region (pre-integrated look)
vec3 skinDiffuse(vec3 N, vec3 L, vec3 albedo, float shadow) {
    float ndl = dot(N, L);
    float wrap = 0.42;
    float d = clamp((ndl + wrap) / (1.0 + wrap), 0.0, 1.0);
    float terminator = clamp(1.0 - abs(ndl) * 1.6, 0.0, 1.0);
    vec3 scatter = vec3(0.85, 0.22, 0.12) * terminator * 0.55 * max(shadow, 0.25);
    return albedo / PI * (vec3(d) * shadow + scatter);
}

// cloth: soft wrap + "Charlie" style sheen towards grazing angles
vec3 clothSheen(vec3 N, vec3 V, vec3 L, vec3 albedo, float rough) {
    vec3 H = normalize(V + L);
    float NoH = clamp(dot(N, H), 0.0, 1.0);
    float NoL = clamp(dot(N, L), 0.0, 1.0);
    float NoV = clamp(abs(dot(N, V)), 1e-3, 1.0);
    float a = max(rough, 0.3);
    float inv = 1.0 / a;
    float sin2 = 1.0 - NoH * NoH;
    float D = (2.0 + inv) * pow(max(sin2, 1e-4), inv * 0.5) / (2.0 * PI);
    float Vis = 1.0 / (4.0 * (NoL + NoV - NoL * NoV) + 1e-3);
    vec3 sheenColor = mix(vec3(0.04), albedo, 0.6);
    return sheenColor * D * Vis * NoL;
}

// foliage: wrapped diffuse + light transmitted through the leaves when looking towards the sun
// (thin translucent blades); crown occlusion damps the transmission deep inside the crown
vec3 foliageDiffuse(vec3 N, vec3 V, vec3 L, vec3 albedo, float crownAO) {
    float wrap = clamp((dot(N, L) + 0.5) / 1.5, 0.0, 1.0);
    float back = pow(clamp(dot(-V, L), 0.0, 1.0), 4.0);
    vec3 trans = albedo * vec3(1.1, 1.25, 0.55) * (back * 0.9 + 0.12) * crownAO;
    return albedo / PI * wrap + trans / PI;
}

// street canyon reflections: on city maps the low sky is hidden behind buildings, so reflection
// directions near the horizon see facades (lit by the sky and, half of them, by the sun) instead
// of the open sky (amount = frame.extra.w, 0 on open maps)
vec3 urbanReflection(vec3 refl, vec3 R, float amount) {
    if (amount <= 0.0) return refl;
    float blocked = (1.0 - smoothstep(-0.05, 0.42, R.y)) * amount;
    vec3 back = normalize(vec3(-R.x, 0.0, -R.z) + vec3(0.0, 1e-3, 0.0));
    vec3 skyE = evalSH(frame.sh, back) * frame.sunColor.w;
    vec3 sunE = frame.sunColor.rgb * frame.sunDir.w * max(dot(back, normalize(frame.sunDir.xyz)), 0.0) * 0.5 / PI;
    vec3 facade = vec3(0.3, 0.28, 0.26) * (skyE + sunE);
    return mix(refl, facade, blocked);
}

// alpha test value with mip compensation (keeps alpha tested coverage at a distance)
float alphaTestValue(float a, float lod) { return a * (1.0 + max(lod, 0.0) * 0.25); }

// ---------------------------------------------------------------------------------------------
// interior mapping: the glass UV is (window index + u, floor index + v). A ray from the eye is
// traced through a virtual room box behind the glass (u, v in window units, depth in metres) and
// the hit face is shaded procedurally: walls, floor, ceiling lamp, furniture, blinds, shop shelves.
float ihash(vec2 p) { return fract(sin(dot(p, vec2(41.3, 289.1))) * 17531.7); }

vec3 interiorRoom(vec2 uv, vec3 worldPos, vec3 N, vec3 viewDir, float depth, float light, bool shop, float night, float skyLum, float exposure) {
    vec2 cell = floor(uv);
    vec2 f = fract(uv);
    // world gradient of u and v on the glass plane (from screen derivatives)
    vec3 dp1 = dFdx(worldPos), dp2 = dFdy(worldPos);
    vec2 du = vec2(dFdx(uv.x), dFdy(uv.x)), dv = vec2(dFdx(uv.y), dFdy(uv.y));
    float a11 = dot(dp1, dp1), a12 = dot(dp1, dp2), a22 = dot(dp2, dp2);
    float det = a11 * a22 - a12 * a12;
    if (abs(det) < 1e-12) return vec3(0.02);
    vec2 cu = vec2(a22 * du.x - a12 * du.y, -a12 * du.x + a11 * du.y) / det;
    vec2 cv = vec2(a22 * dv.x - a12 * dv.y, -a12 * dv.x + a11 * dv.y) / det;
    vec3 gu = dp1 * cu.x + dp2 * cu.y;  // d(u)/d(world)
    vec3 gv = dp1 * cv.x + dp2 * cv.y;
    vec3 rd = vec3(dot(viewDir, gu), dot(viewDir, gv), -dot(viewDir, N));
    rd.z = max(rd.z, 1e-3);
    // exit distances for x in [0,1], y in [0,1], z in [0, depth]
    vec3 ro = vec3(f, 0.0);
    vec3 tMax = vec3(rd.x > 0.0 ? (1.0 - ro.x) / rd.x : (rd.x < 0.0 ? -ro.x / rd.x : 1e9),
                     rd.y > 0.0 ? (1.0 - ro.y) / rd.y : (rd.y < 0.0 ? -ro.y / rd.y : 1e9), depth / rd.z);
    float t = min(tMax.x, min(tMax.y, tMax.z));
    vec3 hit = ro + rd * t;
    float h = ihash(cell);
    float h2 = ihash(cell + 17.3);
    vec3 wallC = mix(vec3(0.72, 0.68, 0.6), vec3(0.55, 0.62, 0.66), h);
    wallC = mix(wallC, vec3(0.62, 0.5, 0.42), step(0.8, h2));
    vec3 col;
    float z01 = hit.z / depth;
    if (t == tMax.z) {
        // back wall: furniture silhouette + picture / shelves
        col = wallC;
        if (shop) {
            // shelving units along the back wall (boards, goods of varying height, uprights),
            // plain wall above, kick plate below
            float y = hit.y;
            if (y < 0.74) {
                float row = floor(y / 0.155);
                float fy = fract(y / 0.155);
                float gx = hit.x * 13.0;
                vec2 gid = vec2(floor(gx), row) + cell * 7.0;
                float g = ihash(gid);
                vec3 goods = mix(vec3(0.42, 0.38, 0.32), vec3(0.55, 0.22, 0.16), step(0.55, g));
                goods = mix(goods, vec3(0.18, 0.28, 0.42), step(0.8, g));
                goods = mix(goods, vec3(0.75, 0.72, 0.65), step(0.93, g));
                goods *= 0.5 + 0.5 * ihash(gid + 3.7);
                float fx = fract(gx);
                float item = step(0.08, fx) * step(fx, 0.92) * step(fy, 0.35 + 0.55 * ihash(gid + 1.3));
                col = mix(vec3(0.06, 0.06, 0.07), goods, item);
                if (fy > 0.88) col = vec3(0.62, 0.62, 0.6);
                float ux = fract(hit.x * 3.0);
                if (ux < 0.025 || ux > 0.975) col = vec3(0.35);
                if (y < 0.1) col = vec3(0.22);
            }
        } else {
            if (hit.y < 0.32 && abs(hit.x - (0.3 + 0.4 * h2)) < 0.28) col = mix(vec3(0.18, 0.14, 0.12), vec3(0.25, 0.28, 0.33), h);
            if (hit.y > 0.5 && hit.y < 0.75 && abs(hit.x - 0.5) < 0.12 + 0.1 * h) col = mix(vec3(0.3, 0.35, 0.45), vec3(0.6, 0.4, 0.3), h2);
        }
    } else if (t == tMax.y) {
        if (rd.y < 0.0) {
            // floor: wood / tiles, darker at the back
            col = shop ? vec3(0.55, 0.55, 0.52) * (0.85 + 0.15 * step(0.5, fract(hit.x * 4.0 + floor(hit.z * 2.0) * 0.5)))
                       : mix(vec3(0.32, 0.22, 0.14), vec3(0.4, 0.4, 0.42), h2) * (0.8 + 0.2 * fract(hit.x * 7.0 + h));
        } else {
            // ceiling with a lamp
            float lamp = exp(-dot(hit.xz - vec2(0.5, depth * 0.5), hit.xz - vec2(0.5, depth * 0.5)) * 6.0);
            if (shop) lamp = step(0.86, fract(hit.z * 0.45 + 0.3)) * step(abs(fract(hit.x * 1.5) - 0.5), 0.38) * 3.0;
            col = vec3(0.8) + vec3(1.0, 0.95, 0.85) * lamp * 1.5;
        }
    } else {
        col = wallC * 0.85;
    }
    // ambient falloff towards the back of the room
    float ambient = mix(1.0, 0.45, z01);
    bool lit = ihash(cell + 3.1) < mix(0.25, 0.65, night) || shop;
    // daylight through the window scales with the sky; lamps are absolute (exposure compensated)
    float lampLevel = lit ? mix(0.06, 0.55, night) / max(exposure, 1e-6) : 0.0;
    vec3 roomLight = vec3(light * skyLum * 0.3) * ambient + vec3(1.0, 0.88, 0.7) * lampLevel * (shop ? 1.3 : 1.0);
    col *= roomLight;
    // blinds / curtains on some residential windows
    if (!shop) {
        float drop = ihash(cell + 9.7);
        if (drop > 0.55 && f.y > 1.0 - (drop - 0.55) * 1.8) {
            float slat = 0.75 + 0.25 * smoothstep(0.3, 0.5, fract(f.y * 28.0));
            col = vec3(0.78, 0.76, 0.72) * slat * (light * skyLum * 0.5 + lampLevel * 0.6);
        }
        if (ihash(cell + 5.5) > 0.8 && (f.x < 0.18 || f.x > 0.82))
            col = mix(vec3(0.6, 0.55, 0.5), vec3(0.4, 0.2, 0.2), h) * (light * skyLum * 0.45 + lampLevel * 0.5);
    }
    return col;
}
