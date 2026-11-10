#version 450
// Растягивает SDF-кадр (он может быть меньше вьюпорта) и переносит его глубину,
// чтобы полигональные меши корректно перекрывались с SDF-объектами.
layout(binding = 0) uniform sampler2D sdfColorTex;
layout(binding = 1) uniform sampler2D sdfDepthTex;

layout(push_constant) uniform CompositePush {
    vec4 rect;      // вьюпорт в пикселях окна: x, y, w, h
    vec4 uvScale;   // xy = доля текстуры, занятая кадром; zw = максимум uv (без захода за край кадра)
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 f = (gl_FragCoord.xy - pc.rect.xy) / pc.rect.zw;
    vec2 uv = min(f * pc.uvScale.xy, pc.uvScale.zw);
    outColor = vec4(texture(sdfColorTex, uv).rgb, 1.0);
    gl_FragDepth = texture(sdfDepthTex, uv).r;
}
