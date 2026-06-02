#include "Input.h"
#include "Common.h"
#include "Scene.h"
#include "MathUtils.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <iostream>

CameraBasis getCameraBasis() {
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

bool projectWorldToScreen(vec3 worldPos, float width, float height, vec2& outScreen) {
    CameraBasis b = getCameraBasis();
    vec3 rel = worldPos - b.pos;
    vec3 viewPos;
    viewPos.x = dot(rel, b.right);
    viewPos.y = dot(rel, b.up);
    viewPos.z = dot(rel, -b.fwd); 
    if (viewPos.z >= 0.0f) return false; 
    float f = 1.8f; 
    float x = viewPos.x * f / -viewPos.z;
    float y = viewPos.y * f / -viewPos.z;
    outScreen.x = (x * height + width) * 0.5f;
    outScreen.y = (height - y * height) * 0.5f;
    return true;
}

GizmoAxis checkGizmoIntersection(Ray ray, vec3 pivotPos, vec3 camPos) {
    if (!showGizmos) return AXIS_NONE;
    
    float distToCam = length(camPos - pivotPos);
    float scale = distToCam * 0.12f; 
    float minD = 10000.0f;
    GizmoAxis hit = AXIS_NONE;

    if (currentMode == MODE_MOVE || currentMode == MODE_SCALE) {
        if (intersectSphere(ray, pivotPos, 0.25f * scale) > 0.0f) return AXIS_CENTER;
        
        float totalLen = 1.3f * scale; 
        float hitThick = 0.35f * scale;
        
        float bx = intersectBox(ray, pivotPos + vec3{totalLen/2.0f, 0, 0}, {totalLen/2.0f, hitThick, hitThick});
        float by = intersectBox(ray, pivotPos + vec3{0, totalLen/2.0f, 0}, {hitThick, totalLen/2.0f, hitThick});
        float bz = intersectBox(ray, pivotPos + vec3{0, 0, totalLen/2.0f}, {hitThick, hitThick, totalLen/2.0f});
        
        if (bx > 0 && bx < minD) { minD = bx; hit = AXIS_X; }
        if (by > 0 && by < minD) { minD = by; hit = AXIS_Y; }
        if (bz > 0 && bz < minD) { minD = bz; hit = AXIS_Z; }
    }
    else if (currentMode == MODE_ROTATE) {
        float r = 1.0f * scale; 
        float thick = 0.2f * scale;
        float dX = intersectRing(ray, pivotPos, 0, r, thick); 
        float dY = intersectRing(ray, pivotPos, 1, r, thick); 
        float dZ = intersectRing(ray, pivotPos, 2, r, thick); 
        if (dX < minD) { minD = dX; hit = AXIS_X; }
        if (dY < minD) { minD = dY; hit = AXIS_Y; }
        if (dZ < minD) { minD = dZ; hit = AXIS_Z; }
    }
    return hit;
}

float getMouseDeltaOnAxis(vec3 axisWorldDir, float dx, float dy) {
    CameraBasis b = getCameraBasis();
    vec2 axisScreenDir;
    axisScreenDir.x = dot(axisWorldDir, b.right);
    axisScreenDir.y = dot(axisWorldDir, b.up); 
    float len = std::sqrt(axisScreenDir.x*axisScreenDir.x + axisScreenDir.y*axisScreenDir.y);
    if (len < 0.001f) return 0.0f;
    axisScreenDir.x /= len; axisScreenDir.y /= len;
    return (dx * axisScreenDir.x + (-dy) * axisScreenDir.y);
}

Ray getRayFromMouse(float mx, float my, float width, float height) {
    CameraBasis basis = getCameraBasis();
    float ndcX = (2.0f * mx) / width - 1.0f;
    float ndcY = 1.0f - (2.0f * my) / height; 
    float aspect = width / height;
    vec3 viewDir = { ndcX * aspect, ndcY, -1.8f }; 
    vec3 worldDir = basis.right * viewDir.x + basis.up * viewDir.y + (-basis.fwd) * viewDir.z;
    return { basis.pos, normalize(worldDir) };
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) {
        cam.leftMouse = false; cam.rightMouse = false; cam.middleMouse = false;
        isDraggingGizmo = false; isBoxSelecting = false;
        return;
    }

    if (action == GLFW_PRESS) {
        double x, y; glfwGetCursorPos(window, &x, &y);
        cam.lastX = x; cam.lastY = y;
        mousePressX = x; mousePressY = y;
        isMouseDragging = false;
    }
    
    if (button == GLFW_MOUSE_BUTTON_RIGHT) cam.rightMouse = (action == GLFW_PRESS);
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) cam.middleMouse = (action == GLFW_PRESS);
    
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        cam.leftMouse = (action == GLFW_PRESS);
        
        if (action == GLFW_PRESS) {
            double x, y; glfwGetCursorPos(window, &x, &y);
            int w, h; glfwGetFramebufferSize(window, &w, &h);
            Ray ray = getRayFromMouse((float)x, (float)y, (float)w, (float)h);
            
            currentGizmoAxis = AXIS_NONE;
            if (!selectedIndices.empty()) {
                vec3 pivot = getSelectionCenter();
                CameraBasis b = getCameraBasis();
                currentGizmoAxis = checkGizmoIntersection(ray, pivot, b.pos);
                if (currentGizmoAxis != AXIS_NONE) { 
                    isDraggingGizmo = true; 
                    saveUndo(); 
                    return; 
                }
            }
        }
        
        if (action == GLFW_RELEASE) {
            double x, y; glfwGetCursorPos(window, &x, &y);
            int w, h; glfwGetFramebufferSize(window, &w, &h);

            if (isBoxSelecting) {
                if (!(mods & GLFW_MOD_SHIFT)) selectedIndices.clear(); 
                float minX = std::min((float)mousePressX, (float)x);
                float maxX = std::max((float)mousePressX, (float)x);
                float minY = std::min((float)mousePressY, (float)y);
                float maxY = std::max((float)mousePressY, (float)y);
                
                for(int i=0; i<sceneGraph.size(); i++) {
                    const auto& od = sceneGraph[i].gpuData;
                    vec3 p = {od.position.x, od.position.y, od.position.z};
                    vec2 screenPos;
                    if (projectWorldToScreen(p, (float)w, (float)h, screenPos)) {
                        if (screenPos.x >= minX && screenPos.x <= maxX && screenPos.y >= minY && screenPos.y <= maxY) {
                            if (std::find(selectedIndices.begin(), selectedIndices.end(), i) == selectedIndices.end()) {
                                selectedIndices.push_back(i);
                            }
                        }
                    }
                }
            }
            else if (!isDraggingGizmo && !isMouseDragging) {
                Ray ray = getRayFromMouse((float)x, (float)y, (float)w, (float)h);
                float minD = 10000.0f;
                int hitIndex = -1;
                
                for(int i=0; i<sceneGraph.size(); i++) {
                    const auto& od = sceneGraph[i].gpuData;
                    vec3 p = {od.position.x, od.position.y, od.position.z};
                    vec3 rot = {od.rotationEuler.x, od.rotationEuler.y, od.rotationEuler.z};
                    vec3 scl = {od.scale.x, od.scale.y, od.scale.z}; 
                    float size = od.params.x;
                    int type = (int)od.position.w;
                    float d = -1.0f;
                    
                    if (type == 0) d = intersectEllipsoid(ray, p, rot, scl, size);
                    else if (type == 1) d = intersectBoxOBB(ray, p, rot, scl, size);
                    
                    if (d > 0.0f && d < minD) { minD = d; hitIndex = i; }
                }
                
                if (!(mods & GLFW_MOD_SHIFT)) selectedIndices.clear();

                if (hitIndex != -1) {
                    auto it = std::find(selectedIndices.begin(), selectedIndices.end(), hitIndex);
                    if (it != selectedIndices.end() && (mods & GLFW_MOD_SHIFT)) selectedIndices.erase(it);
                    else if (it == selectedIndices.end()) selectedIndices.push_back(hitIndex);
                }
            }

            isDraggingGizmo = false;
            isBoxSelecting = false;
            isMouseDragging = false;
            currentGizmoAxis = AXIS_NONE;
        }
    }
    
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE && !isMouseDragging) {
        if (!selectedIndices.empty()) triggerSceneContextMenu = true;
    }
}

void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos) {
    currentMouseX = xpos; currentMouseY = ypos;
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) {
        if (!cam.leftMouse && !cam.rightMouse && !cam.middleMouse) return;
    }

    float dx = (float)(xpos - cam.lastX);
    float dy = (float)(ypos - cam.lastY);
    cam.lastX = xpos; cam.lastY = ypos;

    if (cam.leftMouse || cam.rightMouse || cam.middleMouse) {
        double dist = std::sqrt(std::pow(xpos - mousePressX, 2) + std::pow(ypos - mousePressY, 2));
        if (dist > DRAG_THRESHOLD) isMouseDragging = true;
    }
    if (cam.leftMouse && isMouseDragging && !isDraggingGizmo) isBoxSelecting = true;

    if (cam.middleMouse || (cam.rightMouse && glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)) {
         CameraBasis b = getCameraBasis();
         float s = cam.radius * 0.002f;
         cam.target = cam.target - (b.right * dx * s) + (b.up * dy * s);
         return; 
    }
    if (cam.rightMouse && isMouseDragging) { cam.theta -= dx * 0.005f; cam.phi -= dy * 0.005f; }

    if (cam.leftMouse && isDraggingGizmo && !selectedIndices.empty()) {
        CameraBasis b = getCameraBasis();
        
        if (currentMode == MODE_MOVE) {
            float pivotDist = glm::length(b.pos - getSelectionCenter());
            float speed = pivotDist * 0.002f; 
            vec3 moveDeltaWorld = {0,0,0};

            if (currentGizmoAxis == AXIS_CENTER) {
                moveDeltaWorld = (b.right * dx * speed) + (b.up * (-dy) * speed);
            } else {
                vec3 axis = {0,0,0};
                if (currentGizmoAxis == AXIS_X) axis = {1,0,0}; 
                if (currentGizmoAxis == AXIS_Y) axis = {0,1,0}; 
                if (currentGizmoAxis == AXIS_Z) axis = {0,0,1};
                float d = getMouseDeltaOnAxis(axis, dx, dy);
                moveDeltaWorld = axis * d * speed;
            }
            
            for(int idx : selectedIndices) {
                if(idx < sceneGraph.size()) {
                    bool parentIsSelected = false;
                    int p = sceneGraph[idx].parent;
                    while (p != -1) {
                        if (std::find(selectedIndices.begin(), selectedIndices.end(), p) != selectedIndices.end()) {
                            parentIsSelected = true;
                            break;
                        }
                        p = sceneGraph[p].parent;
                    }
                    if (parentIsSelected) continue;

                    vec3 localDelta = moveDeltaWorld;
                    
                    int parentIdx = sceneGraph[idx].parent;
                    if (parentIdx != -1) {
                        mat4 pMat = calculateWorldMatrix(parentIdx);
                        vec3 c0 = vec3(pMat[0]);
                        vec3 c1 = vec3(pMat[1]);
                        vec3 c2 = vec3(pMat[2]);
                        
                        float sx = glm::length(c0);
                        float sy = glm::length(c1);
                        float sz = glm::length(c2);
                        
                        if(sx > 0.0001f) c0 = c0 / sx;
                        if(sy > 0.0001f) c1 = c1 / sy;
                        if(sz > 0.0001f) c2 = c2 / sz;
                        
                        vec3 rotDelta;
                        rotDelta.x = glm::dot(c0, moveDeltaWorld);
                        rotDelta.y = glm::dot(c1, moveDeltaWorld);
                        rotDelta.z = glm::dot(c2, moveDeltaWorld);
                        
                        if(sx > 0.0001f) localDelta.x = rotDelta.x / sx;
                        if(sy > 0.0001f) localDelta.y = rotDelta.y / sy;
                        if(sz > 0.0001f) localDelta.z = rotDelta.z / sz;
                    }

                    sceneGraph[idx].localPosition += localDelta;
                }
            }
        }
        else if (currentMode == MODE_ROTATE) {
            vec3 axis = {0,0,0};
            if (currentGizmoAxis == AXIS_X) axis = {1,0,0};
            if (currentGizmoAxis == AXIS_Y) axis = {0,1,0};
            if (currentGizmoAxis == AXIS_Z) axis = {0,0,1};

            vec3 tangent = normalize(cross(b.fwd, axis));
            if (length(tangent) < 0.1f) tangent = b.right;
            float delta = getMouseDeltaOnAxis(tangent, dx, dy) * 0.02f;

            for(int idx : selectedIndices) {
                if(idx < sceneGraph.size()) {
                    // Пропускаем детей, если родитель выделен (вращение наследуется)
                    bool parentIsSelected = false;
                    int p = sceneGraph[idx].parent;
                    while (p != -1) {
                         if (std::find(selectedIndices.begin(), selectedIndices.end(), p) != selectedIndices.end()) {
                             parentIsSelected = true; break;
                         }
                         p = sceneGraph[p].parent;
                    }
                    if (parentIsSelected) continue;

                    if (currentGizmoAxis == AXIS_X) sceneGraph[idx].localRotation.x += delta;
                    if (currentGizmoAxis == AXIS_Y) sceneGraph[idx].localRotation.y += delta;
                    if (currentGizmoAxis == AXIS_Z) sceneGraph[idx].localRotation.z += delta;
                }
            }
        }
        else if (currentMode == MODE_SCALE) {
            float scaleSpeed = 0.002f; 
            float deltaS = 0.0f;
            vec3 axis = {0,0,0};
            
            if (currentGizmoAxis == AXIS_CENTER) {
                deltaS = (dx - dy) * scaleSpeed;
            } else {
                if (currentGizmoAxis == AXIS_X) axis = {1,0,0}; 
                if (currentGizmoAxis == AXIS_Y) axis = {0,1,0}; 
                if (currentGizmoAxis == AXIS_Z) axis = {0,0,1};
                deltaS = getMouseDeltaOnAxis(axis, dx, dy) * 0.05f;
            }

            for(int idx : selectedIndices) {
                if(idx < sceneGraph.size()) {
                    // Пропускаем детей
                    bool parentIsSelected = false;
                    int p = sceneGraph[idx].parent;
                    while (p != -1) {
                         if (std::find(selectedIndices.begin(), selectedIndices.end(), p) != selectedIndices.end()) {
                             parentIsSelected = true; break;
                         }
                         p = sceneGraph[p].parent;
                    }
                    if (parentIsSelected) continue;

                    if (currentGizmoAxis == AXIS_CENTER) {
                        float s = 1.0f + deltaS;
                        sceneGraph[idx].localScale.x *= s; 
                        sceneGraph[idx].localScale.y *= s; 
                        sceneGraph[idx].localScale.z *= s;
                    } else {
                        sceneGraph[idx].localScale.x += axis.x * deltaS;
                        sceneGraph[idx].localScale.y += axis.y * deltaS;
                        sceneGraph[idx].localScale.z += axis.z * deltaS;
                    }
                    auto fixZero = [](float& val) { if (std::abs(val) < 0.001f) val = (val >= 0) ? 0.001f : -0.001f; };
                    fixZero(sceneGraph[idx].localScale.x);
                    fixZero(sceneGraph[idx].localScale.y);
                    fixZero(sceneGraph[idx].localScale.z);
                }
            }
        }
    }
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) return;
    if (yoffset > 0) cam.radius *= 0.9f; else cam.radius *= 1.1f;
    if (cam.radius < 0.1f) cam.radius = 0.1f;
}

void processInput(GLFWwindow* window, float deltaTime) {
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard) return;
    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) currentMode = MODE_MOVE;
    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) currentMode = MODE_ROTATE;
    if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) currentMode = MODE_SCALE;
    
    if (glfwGetKey(window, GLFW_KEY_KP_DECIMAL) == GLFW_PRESS && !selectedIndices.empty()) cam.target = getSelectionCenter();
    bool isCtrl = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS);
    
    static float undoTimer = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS && isCtrl) {
        if (undoTimer <= 0.0f) { performUndo(); undoTimer = 0.15f; } else undoTimer -= deltaTime;
    } else undoTimer = 0.0f;

    static float delTimer = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS) {
        if (delTimer <= 0.0f) { deleteSelectedObject(); delTimer = 0.2f; } else delTimer -= deltaTime;
    } else delTimer = 0.0f;
    
    static float copyTimer = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS && isCtrl) {
        if (copyTimer <= 0.0f && !selectedIndices.empty()) {
            clipboardNodes.clear();
            for (int idx : selectedIndices) if (idx < sceneGraph.size()) clipboardNodes.push_back(sceneGraph[idx]);
            showToast("Copied"); copyTimer = 0.3f; 
        } else copyTimer -= deltaTime;
    } else copyTimer = 0.0f;
    
    static float pasteTimer = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS && isCtrl) {
        if (pasteTimer <= 0.0f && !clipboardNodes.empty()) {
            saveUndo(); selectedIndices.clear();
            for (const auto& node : clipboardNodes) {
                if (sceneGraph.size() >= MAX_OBJECTS) break;
                SceneNode newNode = node; newNode.name += "_Paste"; newNode.localPosition.x += 1.0f; newNode.parent = -1; newNode.children.clear();
                sceneGraph.push_back(newNode); selectedIndices.push_back((int)sceneGraph.size() - 1);
            }
            showToast("Pasted"); pasteTimer = 0.2f;
        } else pasteTimer -= deltaTime;
    } else pasteTimer = 0.0f;
    
    static float dupTimer = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS && glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS) {
        if (dupTimer <= 0.0f && !selectedIndices.empty()) { duplicateSelectedObject(); dupTimer = 0.25f; } else dupTimer -= deltaTime;
    } else dupTimer = 0.0f;

    float speed = 5.0f * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) speed *= 3.0f;
    CameraBasis b = getCameraBasis();
    vec3 forward = normalize(vec3{b.fwd.x, 0.0f, b.fwd.z});
    vec3 right   = normalize(vec3{b.right.x, 0.0f, b.right.z});
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cam.target += forward * speed;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cam.target -= forward * speed;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cam.target -= right * speed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cam.target += right * speed;
}