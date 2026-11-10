#include "Input.h"
#include "Common.h"
#include "Scene.h"
#include "Settings.h"
#include "Game.h"
#include "EditorUI.h"
#include "MathUtils.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>

// Накопители перетаскивания гизмо: привязка работает по суммарному смещению
static vec3 dragMoveAccum = {0, 0, 0};
static vec3 dragMoveApplied = {0, 0, 0};
static float dragAngleAccum = 0.0f;
static float dragAngleApplied = 0.0f;

bool isInViewport(double x, double y) {
    return x >= viewportRect.x && y >= viewportRect.y && x < viewportRect.x + viewportRect.z && y < viewportRect.y + viewportRect.w;
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

Ray getRayFromMouse(float mx, float my) {
    CameraBasis basis = getCameraBasis();
    float width = std::max(viewportRect.z, 1.0f), height = std::max(viewportRect.w, 1.0f);
    float ndcX = (2.0f * (mx - viewportRect.x)) / width - 1.0f;
    float ndcY = 1.0f - (2.0f * (my - viewportRect.y)) / height; 
    float aspect = width / height;
    vec3 viewDir = { ndcX * aspect, ndcY, -getCameraFocalLength() }; 
    vec3 worldDir = basis.right * viewDir.x + basis.up * viewDir.y + (-basis.fwd) * viewDir.z;
    return { basis.pos, normalize(worldDir) };
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (isPlayMode()) return;
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
            Ray ray = getRayFromMouse((float)x, (float)y);
            
            currentGizmoAxis = AXIS_NONE;
            if (!selectedIndices.empty()) {
                vec3 pivot = getSelectionCenter();
                CameraBasis b = getCameraBasis();
                currentGizmoAxis = checkGizmoIntersection(ray, pivot, b.pos);
                if (currentGizmoAxis != AXIS_NONE) { 
                    isDraggingGizmo = true; 
                    dragMoveAccum = dragMoveApplied = vec3(0.0f);
                    dragAngleAccum = dragAngleApplied = 0.0f;
                    saveUndo(); 
                    return; 
                }
            }
        }
        
        if (action == GLFW_RELEASE) {
            double x, y; glfwGetCursorPos(window, &x, &y);

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
                    if (!sceneGraph[i].worldActive) continue;
                    if (projectWorldToScreen(p, screenPos)) {
                        if (screenPos.x >= minX && screenPos.x <= maxX && screenPos.y >= minY && screenPos.y <= maxY) {
                            if (std::find(selectedIndices.begin(), selectedIndices.end(), i) == selectedIndices.end()) {
                                selectedIndices.push_back(i);
                            }
                        }
                    }
                }
            }
            else if (!isDraggingGizmo && !isMouseDragging) {
                int hitIndex = pickNode(getRayFromMouse((float)x, (float)y));
                
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
    if (isPlayMode()) return;
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

            // Привязка к шагу сетки (Ctrl временно инвертирует настройку)
            bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
            if (editorCfg.snap != ctrl) {
                dragMoveAccum += moveDeltaWorld;
                vec3 snapped = glm::round(dragMoveAccum / editorCfg.snapStep) * editorCfg.snapStep;
                moveDeltaWorld = snapped - dragMoveApplied;
                dragMoveApplied = snapped;
            }
            
            for(int idx : selectedIndices) {
                if(idx < sceneGraph.size()) {
                    // Дети выделенного родителя двигаются вместе с ним
                    if (isAncestorSelected(idx)) continue;

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

            bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
            if (editorCfg.snap != ctrl) {
                float step = glm::radians(editorCfg.snapAngleDeg);
                dragAngleAccum += delta;
                float snapped = std::round(dragAngleAccum / step) * step;
                delta = snapped - dragAngleApplied;
                dragAngleApplied = snapped;
            }

            for(int idx : selectedIndices) {
                if(idx < sceneGraph.size()) {
                    // Дети выделенного родителя двигаются вместе с ним
                    if (isAncestorSelected(idx)) continue;

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
                    // Дети выделенного родителя двигаются вместе с ним
                    if (isAncestorSelected(idx)) continue;

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
    if (isPlayMode()) return;
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) return;
    if (yoffset > 0) cam.radius *= 0.9f; else cam.radius *= 1.1f;
    if (cam.radius < 0.1f) cam.radius = 0.1f;
}

void processInput(GLFWwindow* window, float deltaTime) {
    ImGuiIO& io = ImGui::GetIO();
    auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };

    if (pressed(ImGuiKey_F5)) { if (isPlayMode()) stopPlay(window); else startPlay(window); return; }
    if (isPlayMode()) {
        if (pressed(ImGuiKey_Escape)) stopPlay(window);
        return;
    }
    if (io.WantCaptureKeyboard || io.WantTextInput) return;

    bool ctrl = io.KeyCtrl, shift = io.KeyShift, alt = io.KeyAlt;

    if (ctrl) {
        if (pressed(ImGuiKey_Z)) { if (shift) performRedo(); else performUndo(); }
        if (pressed(ImGuiKey_Y)) performRedo();
        if (pressed(ImGuiKey_S)) { if (shift) actionSaveSceneAs(window); else actionSaveScene(window); }
        if (pressed(ImGuiKey_O)) actionOpenScene(window);
        if (pressed(ImGuiKey_N)) resetScene();
        if (pressed(ImGuiKey_A)) actionSelectAll();
        if (pressed(ImGuiKey_C) && !selectedIndices.empty()) {
            clipboardNodes.clear();
            for (int idx : selectedIndices) if (idx < (int)sceneGraph.size()) clipboardNodes.push_back(sceneGraph[idx]);
            showToast("Copied");
        }
        if (pressed(ImGuiKey_V) && !clipboardNodes.empty()) {
            saveUndo(); selectedIndices.clear();
            for (const auto& node : clipboardNodes) {
                if (sceneGraph.size() >= MAX_OBJECTS) break;
                SceneNode newNode = node; newNode.name += "_Paste"; newNode.localPosition.x += 1.0f; newNode.parent = -1; newNode.children.clear();
                sceneGraph.push_back(newNode); selectedIndices.push_back((int)sceneGraph.size() - 1);
            }
            showToast("Pasted");
        }
        return;   // Ctrl-комбинации не должны двигать камеру
    }

    if (alt) {
        if (pressed(ImGuiKey_D)) duplicateSelectedObject();
        if (pressed(ImGuiKey_A)) selectedIndices.clear();
        return;
    }

    if (pressed(ImGuiKey_1)) currentMode = MODE_MOVE;
    if (pressed(ImGuiKey_2)) currentMode = MODE_ROTATE;
    if (pressed(ImGuiKey_3)) currentMode = MODE_SCALE;
    if (pressed(ImGuiKey_F1)) currentShadingMode = SHADING_WIRE;
    if (pressed(ImGuiKey_F2)) currentShadingMode = SHADING_SOLID;
    if (pressed(ImGuiKey_F3)) currentShadingMode = SHADING_MATERIAL;
    if (pressed(ImGuiKey_F4)) currentShadingMode = SHADING_RENDERED;
    if (pressed(ImGuiKey_T)) showToolbar = !showToolbar;
    if (pressed(ImGuiKey_N)) showSidebar = !showSidebar;
    if (pressed(ImGuiKey_G)) editorCfg.showGrid = !editorCfg.showGrid;
    if (pressed(ImGuiKey_Escape)) selectedIndices.clear();
    if (pressed(ImGuiKey_Delete) || pressed(ImGuiKey_X)) deleteSelectedObject();
    if ((pressed(ImGuiKey_KeypadDecimal) || pressed(ImGuiKey_F)) && !selectedIndices.empty()) cam.target = getSelectionCenter();
    if (pressed(ImGuiKey_H) && !selectedIndices.empty()) {
        saveUndo();
        for (int idx : selectedIndices) if (idx < (int)sceneGraph.size()) sceneGraph[idx].active = !sceneGraph[idx].active;
    }

    float speed = 5.0f * deltaTime;
    if (shift) speed *= 3.0f;
    CameraBasis b = getCameraBasis();
    vec3 forward = normalize(vec3{b.fwd.x, 0.0f, b.fwd.z});
    vec3 right   = normalize(vec3{b.right.x, 0.0f, b.right.z});
    if (ImGui::IsKeyDown(ImGuiKey_W)) cam.target += forward * speed;
    if (ImGui::IsKeyDown(ImGuiKey_S)) cam.target -= forward * speed;
    if (ImGui::IsKeyDown(ImGuiKey_A)) cam.target -= right * speed;
    if (ImGui::IsKeyDown(ImGuiKey_D)) cam.target += right * speed;
    if (ImGui::IsKeyDown(ImGuiKey_E)) cam.target.y += speed;
    if (ImGui::IsKeyDown(ImGuiKey_Q)) cam.target.y -= speed;
}
