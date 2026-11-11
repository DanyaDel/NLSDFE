#version 450

// Ровно 128 байт — гарантированный минимум push constants в Vulkan
layout(push_constant) uniform MeshPushConstants {
    mat4 mvp;
    vec4 normalRow0;   // строки матрицы нормалей; w = направление на солнце (x, y, z)
    vec4 normalRow1;
    vec4 normalRow2;
    vec4 color;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec4 fragColor;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragNormal = vec3(dot(pc.normalRow0.xyz, inNormal), dot(pc.normalRow1.xyz, inNormal), dot(pc.normalRow2.xyz, inNormal));
    fragTexCoord = inTexCoord;
    fragColor = vec4(inColor.rgb * pc.color.rgb, 1.0);
}
