#version 450
layout(location = 0) out vec3 vColor;
layout(set = 1, binding = 0) uniform U { vec4 offset; } u;
void main() {
    vec2 p[3] = vec2[](vec2(0.0, 0.6), vec2(-0.6, -0.5), vec2(0.6, -0.5));
    vec3 c[3] = vec3[](vec3(1,0.3,0.2), vec3(0.2,1,0.3), vec3(0.2,0.3,1));
    gl_Position = vec4(p[gl_VertexIndex] + u.offset.xy, 0.5, 1.0);
    vColor = c[gl_VertexIndex];
}
