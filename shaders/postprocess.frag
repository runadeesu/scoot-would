#version 450
// tone mapping (ACES), exposure, bloom composite, colour grading, vignette -> sRGB LDR (luma in alpha for FXAA)
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D texHDR;
layout(set = 2, binding = 1) uniform sampler2D texBloom;
layout(set = 3, binding = 0) uniform PostParams {
    vec4 exposure;  // x = exposure, y = bloom strength, z = vignette, w = saturation
    vec4 grading;   // x = contrast, y = temperature, z = tint, w = gamma lift
    vec4 flash;     // rgb = screen flash colour, w = amount
} pp;

vec3 acesFitted(vec3 v) {
    const mat3 ACESIn = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
    const mat3 ACESOut = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
    v = ACESIn * v;
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    v = a / b;
    return clamp(ACESOut * v, 0.0, 1.0);
}

vec3 linearToSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

void main() {
    vec3 hdr = textureLod(texHDR, vUV, 0.0).rgb;
    vec3 bloom = textureLod(texBloom, vUV, 0.0).rgb;
    vec3 c = hdr + bloom * pp.exposure.y;
    c *= pp.exposure.x;
    // white balance
    c *= vec3(1.0 + pp.grading.y * 0.1, 1.0 + pp.grading.z * 0.05, 1.0 - pp.grading.y * 0.1);
    c = acesFitted(c);
    // contrast around mid grey (in perceptual space) and saturation
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(luma), c, pp.exposure.w);
    c = clamp((c - 0.18) * pp.grading.x + 0.18, 0.0, 1.0);
    c = pow(c, vec3(1.0 - pp.grading.w));
    // vignette
    vec2 q = vUV - 0.5;
    float vig = 1.0 - dot(q, q) * pp.exposure.z * 1.6;
    c *= clamp(vig, 0.0, 1.0);
    c = mix(c, pp.flash.rgb, pp.flash.w);
    vec3 s = linearToSrgb(clamp(c, 0.0, 1.0));
    // ordered dither to avoid banding in the sky
    float n = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
    s += n / 255.0;
    outColor = vec4(s, dot(s, vec3(0.299, 0.587, 0.114)));
}
