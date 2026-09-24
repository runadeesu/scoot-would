#version 450
// UI: texture R = coverage for glyphs (atlas), rgba for images
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D texAtlas;
layout(set = 3, binding = 0) uniform UIFrag { vec4 mode; } uf;  // x = 0 alpha atlas, 1 = rgba image
void main() {
    vec4 t = texture(texAtlas, vUV);
    vec4 c = uf.mode.x > 0.5 ? t * vColor : vec4(vColor.rgb, vColor.a * t.r);
    outColor = vec4(c.rgb * c.a, c.a);  // premultiplied
}
