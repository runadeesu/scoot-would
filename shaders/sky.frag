#version 450
// HDRI sky with an analytic sun disk
#include "common.glsl"
layout(location = 0) in vec3 vDir;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D texSky;
layout(set = 3, binding = 0) uniform FrameUBO { FrameData frame; };

void main() {
    vec3 d = normalize(vDir);
    vec2 uv = dirToEquirect(d, frame.envParams.x);
    // use explicit lod 0: derivatives break at the equirect seam
    vec3 sky = textureLod(texSky, uv, 0.0).rgb * frame.envParams.z;
    // below the horizon the HDRI ground is replaced by a soft haze colour
    float below = smoothstep(0.02, -0.12, d.y);
    vec3 haze = evalSH(frame.sh, normalize(vec3(d.x, 0.05, d.z))) * frame.sunColor.w;
    sky = mix(sky, haze, below);
    float cosSun = dot(d, normalize(frame.sunDir.xyz));
    float disk = smoothstep(0.99985, 0.99995, cosSun);
    sky += frame.sunColor.rgb * frame.sunDir.w * disk * 25.0;
    outColor = vec4(sky, 1.0);
}
