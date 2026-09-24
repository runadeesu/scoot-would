#version 450
// game UI (menus, HUD): screen space pixels -> NDC
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;
layout(set = 1, binding = 0) uniform UIParams { vec4 screen; } ui;  // xy = 2/size
void main() {
    vUV = inUV;
    vColor = inColor;
    gl_Position = vec4(inPos.x * ui.screen.x - 1.0, 1.0 - inPos.y * ui.screen.y, 0.0, 1.0);
}
