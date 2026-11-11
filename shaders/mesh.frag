#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform MeshPushConstants {
    mat4 mvp;
    vec4 normalRow0;
    vec4 normalRow1;
    vec4 normalRow2;
    vec4 color;
} pc;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = vec3(pc.normalRow0.w, pc.normalRow1.w, pc.normalRow2.w);

    // Simple Lambertian to match the SDF Solid/Material mode feel
    float diff = max(dot(N, L), 0.0);
    vec3 base = fragColor.rgb;
    if (pc.color.a > 0.5) {
        // Шахматка 8x8 на единичный квадрат UV: по ней видно растяжения развёртки
        vec2 cell = floor(fragTexCoord * 8.0);
        float checker = mod(cell.x + cell.y, 2.0);
        base = mix(vec3(0.25), vec3(0.9), checker) * mix(vec3(1.0), vec3(fract(fragTexCoord), 0.5), 0.35);
    }
    vec3 col = base * (0.2 + 0.1 * N.y + diff);
    outColor = vec4(col, 1.0);
}
