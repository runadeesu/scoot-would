#version 450
// final copy of the backbuffer (scene + UI) into the swapchain image, rescaled if their sizes differ
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D texSource;
void main() {
    outColor = vec4(textureLod(texSource, vUV, 0.0).rgb, 1.0);
}
