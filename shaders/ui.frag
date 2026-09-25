#version 450
// UI: atlas R = signed distance (0.5 = edge) for glyphs and shapes, or an rgba image
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D texAtlas;
// x: 0 = SDF atlas, 1 = rgba image, 2 = frosted backdrop (vUV = screen uv); y: extra edge softness
// (glow / shadow) or the backdrop tint amount; zw: backdrop texel size
layout(set = 3, binding = 0) uniform UIFrag { vec4 mode; } uf;
void main() {
    vec4 c;
    if (uf.mode.x > 1.5) {
        // 9 tap tent over the 1/8 resolution downsample: a wide, smooth blur
        vec2 t = uf.mode.zw;
        vec3 b = texture(texAtlas, vUV).rgb * 0.25;
        b += (texture(texAtlas, vUV + vec2(t.x, 0)).rgb + texture(texAtlas, vUV - vec2(t.x, 0)).rgb + texture(texAtlas, vUV + vec2(0, t.y)).rgb +
              texture(texAtlas, vUV - vec2(0, t.y)).rgb) * 0.125;
        b += (texture(texAtlas, vUV + t).rgb + texture(texAtlas, vUV - t).rgb + texture(texAtlas, vUV + vec2(t.x, -t.y)).rgb +
              texture(texAtlas, vUV + vec2(-t.x, t.y)).rgb) * 0.0625;
        c = vec4(mix(b, vColor.rgb, uf.mode.y), vColor.a);
        outColor = vec4(c.rgb * c.a, c.a);
        return;
    }
    vec4 t = texture(texAtlas, vUV);
    if (uf.mode.x > 0.5) {
        c = t * vColor;
    } else {
        float w = max(fwidth(t.r) * 0.75, 1e-3);
        float a = smoothstep(0.5 - w - uf.mode.y, 0.5 + w, t.r);
        c = vec4(vColor.rgb, vColor.a * a);
    }
    outColor = vec4(c.rgb * c.a, c.a);  // premultiplied
}
