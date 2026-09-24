#version 450
// FXAA 3.11 style edge anti aliasing + upscale (render scale) + optional contrast adaptive sharpening
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D texLDR;
layout(set = 3, binding = 0) uniform FxaaParams { vec4 params; } fp;  // xy = source texel, z = fxaa on, w = sharpen

float luma(vec4 c) { return c.a; }

vec3 fxaa(vec2 uv, vec2 t) {
    vec4 cM = textureLod(texLDR, uv, 0.0);
    float lM = luma(cM);
    float lN = luma(textureLod(texLDR, uv + vec2(0, -t.y), 0.0));
    float lS = luma(textureLod(texLDR, uv + vec2(0, t.y), 0.0));
    float lE = luma(textureLod(texLDR, uv + vec2(t.x, 0), 0.0));
    float lW = luma(textureLod(texLDR, uv + vec2(-t.x, 0), 0.0));
    float lMin = min(lM, min(min(lN, lS), min(lE, lW)));
    float lMax = max(lM, max(max(lN, lS), max(lE, lW)));
    float range = lMax - lMin;
    if (range < max(0.0312, lMax * 0.125)) return cM.rgb;
    float lNW = luma(textureLod(texLDR, uv + vec2(-t.x, -t.y), 0.0));
    float lNE = luma(textureLod(texLDR, uv + vec2(t.x, -t.y), 0.0));
    float lSW = luma(textureLod(texLDR, uv + vec2(-t.x, t.y), 0.0));
    float lSE = luma(textureLod(texLDR, uv + vec2(t.x, t.y), 0.0));
    float edgeH = abs(lNW + lNE - 2.0 * lN) + 2.0 * abs(lW + lE - 2.0 * lM) + abs(lSW + lSE - 2.0 * lS);
    float edgeV = abs(lNW + lSW - 2.0 * lW) + 2.0 * abs(lN + lS - 2.0 * lM) + abs(lNE + lSE - 2.0 * lE);
    bool horizontal = edgeH >= edgeV;
    float l1 = horizontal ? lN : lW;
    float l2 = horizontal ? lS : lE;
    float g1 = abs(l1 - lM), g2 = abs(l2 - lM);
    float stepLen = horizontal ? t.y : t.x;
    float lLocal;
    float grad;
    if (g1 >= g2) { stepLen = -stepLen; lLocal = (l1 + lM) * 0.5; grad = g1; }
    else { lLocal = (l2 + lM) * 0.5; grad = g2; }
    vec2 cur = uv;
    if (horizontal) cur.y += stepLen * 0.5; else cur.x += stepLen * 0.5;
    vec2 off = horizontal ? vec2(t.x, 0) : vec2(0, t.y);
    vec2 p1 = cur - off, p2 = cur + off;
    float e1 = luma(textureLod(texLDR, p1, 0.0)) - lLocal;
    float e2 = luma(textureLod(texLDR, p2, 0.0)) - lLocal;
    bool r1 = abs(e1) >= grad * 0.25, r2 = abs(e2) >= grad * 0.25;
    const float steps[10] = float[](1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 4.0, 8.0);
    for (int i = 0; i < 10 && !(r1 && r2); ++i) {
        if (!r1) { p1 -= off * steps[i]; e1 = luma(textureLod(texLDR, p1, 0.0)) - lLocal; r1 = abs(e1) >= grad * 0.25; }
        if (!r2) { p2 += off * steps[i]; e2 = luma(textureLod(texLDR, p2, 0.0)) - lLocal; r2 = abs(e2) >= grad * 0.25; }
    }
    float d1 = horizontal ? uv.x - p1.x : uv.y - p1.y;
    float d2 = horizontal ? p2.x - uv.x : p2.y - uv.y;
    bool closer1 = d1 < d2;
    float dmin = min(d1, d2);
    float span = d1 + d2;
    float pixOff = -dmin / span + 0.5;
    bool lCenterSmaller = lM < lLocal;
    bool correct = ((closer1 ? e1 : e2) < 0.0) != lCenterSmaller;
    float finalOff = correct ? pixOff : 0.0;
    float lAvg = (1.0 / 12.0) * (2.0 * (lN + lS + lE + lW) + lNW + lNE + lSW + lSE);
    float sub = clamp(abs(lAvg - lM) / range, 0.0, 1.0);
    sub = (-2.0 * sub + 3.0) * sub * sub;
    finalOff = max(finalOff, sub * sub * 0.75);
    vec2 fuv = uv;
    if (horizontal) fuv.y += finalOff * stepLen; else fuv.x += finalOff * stepLen;
    return textureLod(texLDR, fuv, 0.0).rgb;
}

void main() {
    vec2 t = fp.params.xy;
    vec3 c = fp.params.z > 0.5 ? fxaa(vUV, t) : textureLod(texLDR, vUV, 0.0).rgb;
    if (fp.params.w > 0.0) {
        // contrast adaptive sharpening (helps when render scale < 100%)
        vec3 n = textureLod(texLDR, vUV + vec2(0, -t.y), 0.0).rgb;
        vec3 s = textureLod(texLDR, vUV + vec2(0, t.y), 0.0).rgb;
        vec3 e = textureLod(texLDR, vUV + vec2(t.x, 0), 0.0).rgb;
        vec3 w = textureLod(texLDR, vUV + vec2(-t.x, 0), 0.0).rgb;
        vec3 mn = min(c, min(min(n, s), min(e, w)));
        vec3 mx = max(c, max(max(n, s), max(e, w)));
        vec3 amp = clamp(min(mn, 1.0 - mx) / max(mx, 1e-4), 0.0, 1.0);
        amp = sqrt(amp);
        vec3 wgt = -amp * mix(0.125, 0.2, fp.params.w);
        c = clamp((c + (n + s + e + w) * wgt) / (1.0 + 4.0 * wgt), 0.0, 1.0);
    }
    outColor = vec4(c, 1.0);
}
