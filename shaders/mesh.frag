#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform MeshPushConstants {
    mat4 viewProj;
    mat4 model;
    vec4 color;
    vec4 params; // x=metallic, y=roughness, z=ao, w=unused
    vec4 camPos;
} pc;

vec3 lightDir = normalize(vec3(0.5, 0.8, -0.5));

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = lightDir;
    vec3 V = normalize(pc.camPos.xyz - fragPos); 
    
    // Simple Lambertian + Specular (Blinn-Phong) to match SDF logic roughly
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * fragColor.rgb;
    
    vec3 ambient = 0.1 * fragColor.rgb;
    
    // Simple output for now, matching the Solid/Material mode feel
    outColor = vec4(ambient + diffuse, 1.0);
}
