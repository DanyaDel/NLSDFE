#include "Common.h"
#include "Settings.h"
#include <algorithm>
#include <cmath>

// Камера редактора (орбитальная) либо подменённая игровым кодом
bool cameraOverride = false;
CameraBasis cameraOverrideBasis = {};

CameraBasis getCameraBasis() {
    if (cameraOverride) return cameraOverrideBasis;
    cam.phi = std::clamp(cam.phi, 0.01f, 3.14159f - 0.01f);
    vec3 offset = { 
        cam.radius * std::sin(cam.phi) * std::sin(cam.theta), 
        cam.radius * std::cos(cam.phi), 
        cam.radius * std::sin(cam.phi) * std::cos(cam.theta) 
    };
    vec3 pos = cam.target + offset;
    vec3 fwd = normalize(cam.target - pos); 
    vec3 worldUp = {0.0f, 1.0f, 0.0f};
    vec3 right = normalize(cross(fwd, worldUp));
    vec3 up = normalize(cross(right, fwd)); 
    return {pos, fwd, right, up};
}

// Одно значение на рендер, выбор объектов и проекцию оверлеев
float getCameraFocalLength() {
    return 1.0f / std::tan(glm::radians(editorCfg.fovDeg) * 0.5f);
}

bool projectWorldToScreen(vec3 worldPos, vec2& outScreen) {
    CameraBasis b = getCameraBasis();
    vec3 rel = worldPos - b.pos;
    vec3 viewPos;
    viewPos.x = dot(rel, b.right);
    viewPos.y = dot(rel, b.up);
    viewPos.z = dot(rel, -b.fwd); 
    if (viewPos.z >= -0.01f) return false; 
    float f = getCameraFocalLength();
    float x = viewPos.x * f / -viewPos.z;
    float y = viewPos.y * f / -viewPos.z;
    float width = viewportRect.z, height = viewportRect.w;
    outScreen.x = viewportRect.x + (x * height + width) * 0.5f;
    outScreen.y = viewportRect.y + (height - y * height) * 0.5f;
    return true;
}
