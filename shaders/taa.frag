#version 450
// temporal anti-aliasing resolve
// The scene is rendered with a different sub pixel offset every frame (Halton 2,3). The previous result is
// reprojected with the per pixel motion from the prepass (camera, moving objects, skinned rider), clipped to
// the colour range of the current neighbourhood (YCoCg variance box, removes ghosting and disocclusion trails)
// and blended with the current frame reconstructed at the pixel centre.
#include "common.glsl"
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D texCurrent;
layout(set = 2, binding = 1) uniform sampler2D texHistory;
layout(set = 2, binding = 2) uniform sampler2D texDepth;
layout(set = 2, binding = 3) uniform sampler2D texNormalMotion;  // zw = motion (uv per frame)
layout(set = 3, binding = 0) uniform FrameUBO { FrameData frame; };
layout(set = 3, binding = 1) uniform TaaParams { vec4 params; } taa;  // x = history valid, y = min / z = max current weight

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// blend in a compressed range so a few very bright pixels (sun glints) do not dominate or flicker
vec3 compress(vec3 c) {
    c *= frame.misc.x;
    return c / (1.0 + luma(c));
}
vec3 expand(vec3 c) { return c / max(1.0 - luma(c), 1e-4) / frame.misc.x; }

vec3 toYCoCg(vec3 c) { return vec3(dot(c, vec3(0.25, 0.5, 0.25)), dot(c, vec3(0.5, 0.0, -0.5)), dot(c, vec3(-0.25, 0.5, -0.25))); }
vec3 fromYCoCg(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }

// Catmull-Rom history fetch from 5 bilinear taps (keeps the history sharp while it is resampled)
vec3 sampleHistory(vec2 uv) {
    vec2 size = frame.viewport.xy, texel = frame.viewport.zw;
    vec2 pos = uv * size;
    vec2 p1 = floor(pos - 0.5) + 0.5;
    vec2 f = pos - p1;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 t0 = (p1 - 1.0) * texel;
    vec2 t3 = (p1 + 2.0) * texel;
    vec2 t12 = (p1 + w2 / w12) * texel;
    vec3 r = textureLod(texHistory, vec2(t12.x, t0.y), 0.0).rgb * (w12.x * w0.y);
    r += textureLod(texHistory, vec2(t0.x, t12.y), 0.0).rgb * (w0.x * w12.y);
    r += textureLod(texHistory, t12, 0.0).rgb * (w12.x * w12.y);
    r += textureLod(texHistory, vec2(t3.x, t12.y), 0.0).rgb * (w3.x * w12.y);
    r += textureLod(texHistory, vec2(t12.x, t3.y), 0.0).rgb * (w12.x * w3.y);
    float wsum = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(r / wsum, vec3(0.0));
}

// pulls q towards the centre of the box until it is inside
vec3 clipBox(vec3 q, vec3 bmin, vec3 bmax) {
    vec3 c = 0.5 * (bmax + bmin);
    vec3 e = 0.5 * (bmax - bmin) + 1e-5;
    vec3 v = q - c;
    vec3 a = abs(v / e);
    float m = max(a.x, max(a.y, a.z));
    return m > 1.0 ? c + v / m : q;
}

void main() {
    vec2 texel = frame.viewport.zw;
    // the jittered image shows at pixel centre p what belongs at p - jitter
    vec2 jitterPx = vec2(frame.taa.x, -frame.taa.y) * 0.5 * frame.viewport.xy;

    vec3 m1 = vec3(0.0), m2 = vec3(0.0);
    vec3 bmin = vec3(1e9), bmax = vec3(-1e9);
    vec3 filtered = vec3(0.0);
    float wsum = 0.0;
    float closest = -1.0;
    vec2 closestUV = vUV;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 o = vec2(float(x), float(y));
            vec2 uv = vUV + o * texel;
            vec3 c = compress(textureLod(texCurrent, uv, 0.0).rgb);
            // Blackman-Harris like reconstruction around the unjittered pixel centre
            vec2 d = o - jitterPx;
            float w = exp(-2.29 * dot(d, d));
            filtered += c * w;
            wsum += w;
            vec3 yc = toYCoCg(c);
            m1 += yc;
            m2 += yc * yc;
            bmin = min(bmin, yc);
            bmax = max(bmax, yc);
            // motion of the nearest surface around the pixel keeps silhouettes of moving objects clean
            float z = textureLod(texDepth, uv, 0.0).r;
            if (z > closest) {
                closest = z;
                closestUV = uv;
            }
        }
    }
    vec3 current = filtered / max(wsum, 1e-5);

    vec2 motion;
    if (closest <= 0.0) {
        // sky: only the camera rotation moves it
        vec2 ndc = vec2(vUV.x * 2.0 - 1.0, 1.0 - vUV.y * 2.0) + frame.taa.xy;
        vec4 wp = frame.invViewProj * vec4(ndc, 1e-7, 1.0);
        vec4 prev = frame.prevViewProj * wp;
        motion = prev.w > 1e-6 ? vUV - vec2(prev.x / prev.w * 0.5 + 0.5, 0.5 - prev.y / prev.w * 0.5) : vec2(4.0);
    } else {
        motion = textureLod(texNormalMotion, closestUV, 0.0).zw;
    }
    vec2 prevUV = vUV - motion;
    if (taa.params.x < 0.5 || any(lessThan(prevUV, vec2(0.0))) || any(greaterThan(prevUV, vec2(1.0)))) {
        outColor = vec4(expand(current), 1.0);
        return;
    }

    vec3 history = compress(sampleHistory(prevUV));
    // variance box, tighter while things move fast on screen
    float speed = length(motion * frame.viewport.xy);
    vec3 mean = m1 / 9.0;
    vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
    float gamma = mix(1.25, 0.85, clamp(speed / 12.0, 0.0, 1.0));
    vec3 lo = max(bmin, mean - gamma * sigma);
    vec3 hi = min(bmax, mean + gamma * sigma);
    history = fromYCoCg(clipBox(toYCoCg(history), lo, hi));

    // current weight: low for a stable, well sampled image; higher when the history is stretched by motion
    float w = mix(taa.params.y, taa.params.z, clamp(speed / 16.0, 0.0, 1.0));
    vec3 result = mix(history, current, w);
    outColor = vec4(expand(result), 1.0);
}
