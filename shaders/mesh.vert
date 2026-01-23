#version 450

layout(push_constant) uniform MeshPushConstants {
    mat4 viewProj;
    mat4 model;
    vec4 color;
    vec4 params;
    vec4 camPos;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec4 fragColor;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.viewProj * worldPos;
    fragPos = worldPos.xyz;
    fragNormal = mat3(transpose(inverse(pc.model))) * inNormal;
    fragTexCoord = inTexCoord;
    fragColor = inColor * pc.color;
}
