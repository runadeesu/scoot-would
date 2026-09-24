#version 450
// UI: atlas R = signed distance (0.5 = edge) for glyphs and shapes, or an rgba image
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D texAtlas;
// x: 0 = SDF atlas, 1 = rgba image; y: extra edge softness (glow / shadow)
layout(set = 3, binding = 0) uniform UIFrag { vec4 mode; } uf;
void main() {
    vec4 t = texture(texAtlas, vUV);
    vec4 c;
    if (uf.mode.x > 0.5) {
        c = t * vColor;
    } else {
        float w = max(fwidth(t.r) * 0.75, 1e-3);
        float a = smoothstep(0.5 - w - uf.mode.y, 0.5 + w, t.r);
        c = vec4(vColor.rgb, vColor.a * a);
    }
    outColor = vec4(c.rgb * c.a, c.a);  // premultiplied
}
