#version 450
layout(binding = 0) uniform sampler2D sdfDepthTex;
void main() {
    float d = texelFetch(sdfDepthTex, ivec2(gl_FragCoord.xy), 0).r;
    if (d >= 1.0) discard; // No SDF object here
    gl_FragDepth = d;
}
