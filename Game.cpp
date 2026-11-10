#include "Game.h"
#include "Scene.h"
#include <algorithm>

PlayState play;

namespace {
    // Состояние редактора, которое возвращается после выхода из Play Mode
    std::vector<SceneNode> savedScene;
    std::vector<int> savedSelection;
    std::vector<vec3> basePositions;
    std::vector<float> phases;
    std::vector<bool> insideTrigger;

    const float GRAVITY = 18.0f;
    const float JUMP_SPEED = 6.5f;
    const float WALK_SPEED = 4.0f;
    const float RUN_SPEED = 7.5f;
    const float KILL_HEIGHT = -50.0f;

    // Выталкивает сферу из геометрии; возвращает true, если под ней опора
    bool resolveSphere(vec3 offset) {
        bool ground = false;
        for (int i = 0; i < 4; i++) {
            vec3 c = play.pos + offset;
            float d = sceneDistance(c);
            if (d >= PLAYER_RADIUS) break;
            vec3 n = sceneNormal(c);
            play.pos += n * (PLAYER_RADIUS - d);
            if (n.y > 0.5f) ground = true;
            // Гасим скорость, направленную в поверхность
            float vn = glm::dot(play.vel, n);
            if (vn < 0.0f) play.vel -= n * vn;
        }
        return ground;
    }

    void applyTrigger(const SceneNode& trigger) {
        int t = trigger.triggerTarget;
        if (t >= 0 && t < (int)sceneGraph.size()) {
            SceneNode& target = sceneGraph[t];
            switch (trigger.triggerAction) {
                case TRIGGER_TOGGLE_VISIBLE: target.active = !target.active; break;
                case TRIGGER_START_BEHAVIOR: target.behaviorOn = true; break;
                case TRIGGER_STOP_BEHAVIOR: target.behaviorOn = false; break;
                case TRIGGER_TOGGLE_BEHAVIOR: target.behaviorOn = !target.behaviorOn; break;
            }
        }
        showToast("Trigger: " + trigger.name);
    }

    void updateBehaviors(float dt) {
        for (int i = 0; i < (int)sceneGraph.size(); i++) {
            SceneNode& node = sceneGraph[i];
            if (node.behavior == BEHAVIOR_NONE || !node.behaviorOn || !node.worldActive) continue;
            if (node.behavior == BEHAVIOR_ROTATE) {
                node.localRotation += node.behaviorVec * node.behaviorSpeed * dt;
            } else if (node.behavior == BEHAVIOR_PINGPONG) {
                phases[i] += node.behaviorSpeed * dt;
                node.localPosition = basePositions[i] + node.behaviorVec * (0.5f - 0.5f * std::cos(phases[i]));
            }
        }
    }

    void updateTriggers() {
        vec3 body = play.pos + vec3(0, PLAYER_EYE_HEIGHT * 0.5f, 0);
        for (int i = 0; i < (int)sceneGraph.size(); i++) {
            const SceneNode& node = sceneGraph[i];
            if (!node.isTrigger || !node.worldActive) { insideTrigger[i] = false; continue; }
            vec3 local = mat3(node.gpuData.invRotation) * (body - vec3(node.gpuData.position));
            vec3 half = std::abs(node.gpuData.params.x) * vec3(node.gpuData.scale);
            bool inside = std::abs(local.x) <= half.x && std::abs(local.y) <= half.y && std::abs(local.z) <= half.z;
            if (inside && !insideTrigger[i]) applyTrigger(node);
            insideTrigger[i] = inside;
        }
    }
}

bool isPlayMode() { return play.active; }

CameraBasis getPlayCameraBasis() {
    vec3 fwd = { std::cos(play.pitch) * std::sin(play.yaw), std::sin(play.pitch), -std::cos(play.pitch) * std::cos(play.yaw) };
    vec3 right = glm::normalize(glm::cross(fwd, vec3(0, 1, 0)));
    vec3 up = glm::normalize(glm::cross(right, fwd));
    return { play.pos + vec3(0, PLAYER_EYE_HEIGHT, 0), fwd, right, up };
}

void startPlay(GLFWwindow* window) {
    if (play.active) return;

    // Игрок появляется там, где стоит камера редактора, и смотрит туда же
    CameraBasis b = getCameraBasis();
    play.pos = b.pos - vec3(0, PLAYER_EYE_HEIGHT, 0);
    play.spawn = play.pos;
    play.vel = vec3(0);
    play.pitch = std::asin(std::clamp(b.fwd.y, -1.0f, 1.0f));
    play.yaw = std::atan2(b.fwd.x, -b.fwd.z);
    play.grounded = false;

    savedScene = sceneGraph;
    savedSelection = selectedIndices;
    selectedIndices.clear();
    basePositions.clear();
    for (const auto& n : sceneGraph) basePositions.push_back(n.localPosition);
    phases.assign(sceneGraph.size(), 0.0f);
    insideTrigger.assign(sceneGraph.size(), false);

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwGetCursorPos(window, &play.lastMouseX, &play.lastMouseY);
    play.active = true;
    spdlog::info("Play Mode started");
}

void stopPlay(GLFWwindow* window) {
    if (!play.active) return;
    play.active = false;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    if (play.standalone) { glfwSetWindowShouldClose(window, GLFW_TRUE); return; }
    sceneGraph = savedScene;
    selectedIndices = savedSelection;
    savedScene.clear();
    spdlog::info("Play Mode stopped");
}

void updatePlay(GLFWwindow* window, float deltaTime) {
    if (!play.active) return;
    float dt = std::min(deltaTime, 1.0f / 20.0f);

    // Обзор мышью
    double mx, my; glfwGetCursorPos(window, &mx, &my);
    play.yaw += (float)(mx - play.lastMouseX) * 0.0025f;
    play.pitch = std::clamp(play.pitch - (float)(my - play.lastMouseY) * 0.0025f, -1.5f, 1.5f);
    play.lastMouseX = mx; play.lastMouseY = my;

    updateBehaviors(dt);

    // Движение в горизонтальной плоскости
    vec3 forward = { std::sin(play.yaw), 0.0f, -std::cos(play.yaw) };
    vec3 right = { std::cos(play.yaw), 0.0f, std::sin(play.yaw) };
    vec3 wish(0.0f);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) wish += forward;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) wish -= forward;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) wish += right;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) wish -= right;
    if (glm::dot(wish, wish) > 0.0f) wish = glm::normalize(wish);
    float speed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? RUN_SPEED : WALK_SPEED;
    play.vel.x = wish.x * speed;
    play.vel.z = wish.z * speed;

    if (play.grounded && glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) { play.vel.y = JUMP_SPEED; play.grounded = false; }

    // Несколько подшагов, чтобы не пролетать сквозь тонкие стены
    const int substeps = 3;
    float h = dt / substeps;
    for (int s = 0; s < substeps; s++) {
        play.vel.y -= GRAVITY * h;
        play.pos += play.vel * h;
        bool ground = resolveSphere(vec3(0.0f));
        resolveSphere(vec3(0, PLAYER_EYE_HEIGHT, 0));
        play.grounded = ground;
    }

    if (play.pos.y < KILL_HEIGHT) { play.pos = play.spawn; play.vel = vec3(0); }

    updateTriggers();
}
