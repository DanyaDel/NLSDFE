#include "Game.h"
#include "Scene.h"
#include "Physics.h"
#include "ScriptSystem.h"
#include "Settings.h"
#include "Renderer.h"
#include "imgui.h"
#include <algorithm>
#include <map>

PlayState play;

namespace {
    // Состояние редактора, которое возвращается после выхода из Play Mode
    std::vector<SceneNode> savedScene;
    std::vector<int> savedSelection;
    std::vector<vec3> basePositions;
    std::vector<float> phases;
    vec3 savedGravity;

    // --- Ввод для скриптов: клавиши по именам ---
    GLFWwindow* inputWindow = nullptr;
    std::map<std::string, int> keyNames;
    std::map<int, bool> keysNow, keysBefore;

    void buildKeyNames() {
        if (!keyNames.empty()) return;
        for (char c = 'a'; c <= 'z'; c++) keyNames[std::string(1, c)] = GLFW_KEY_A + (c - 'a');
        for (char c = '0'; c <= '9'; c++) keyNames[std::string(1, c)] = GLFW_KEY_0 + (c - '0');
        keyNames["space"] = GLFW_KEY_SPACE; keyNames["shift"] = GLFW_KEY_LEFT_SHIFT; keyNames["ctrl"] = GLFW_KEY_LEFT_CONTROL;
        keyNames["alt"] = GLFW_KEY_LEFT_ALT; keyNames["enter"] = GLFW_KEY_ENTER; keyNames["tab"] = GLFW_KEY_TAB;
        keyNames["up"] = GLFW_KEY_UP; keyNames["down"] = GLFW_KEY_DOWN; keyNames["left"] = GLFW_KEY_LEFT; keyNames["right"] = GLFW_KEY_RIGHT;
        // Кнопки мыши: отрицательные коды, чтобы не пересекаться с клавишами
        keyNames["mouse1"] = -1; keyNames["mouse2"] = -2; keyNames["mouse3"] = -3;
    }

    bool rawKeyDown(int code) {
        if (!inputWindow) return false;
        if (code < 0) return glfwGetMouseButton(inputWindow, -code - 1) == GLFW_PRESS;
        return glfwGetKey(inputWindow, code) == GLFW_PRESS;
    }

    int keyCode(const std::string& name) {
        std::string lower = name;
        for (char& c : lower) c = (char)std::tolower((unsigned char)c);
        auto it = keyNames.find(lower);
        return it == keyNames.end() ? 0 : it->second;
    }

    void pollKeys() {
        keysBefore = keysNow;
        for (const auto& [name, code] : keyNames) keysNow[code] = rawKeyDown(code);
    }

    void connectScriptHost(GLFWwindow* window) {
        inputWindow = window;
        buildKeyNames();
        keysNow.clear(); keysBefore.clear();
        nls::host.time = 0.0f;
        nls::host.delta = 0.0f;
        nls::host.keyDown = [](const std::string& key) { int code = keyCode(key); return code != 0 && keysNow[code]; };
        nls::host.keyPressed = [](const std::string& key) { int code = keyCode(key); return code != 0 && keysNow[code] && !keysBefore[code]; };
        nls::host.playerPosition = []() { return play.pos + vec3(0, PLAYER_EYE_HEIGHT, 0); };
        nls::host.setPlayerPosition = [](vec3 p) { play.pos = p - vec3(0, PLAYER_EYE_HEIGHT, 0); play.vel = vec3(0.0f); };
    }

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
            showToast("Trigger: " + trigger.name);
        }
    }

    void updateBehaviors(float dt) {
        for (int i = 0; i < (int)sceneGraph.size(); i++) {
            SceneNode& node = sceneGraph[i];
            if (node.behavior == BEHAVIOR_NONE || !node.behaviorOn || !node.worldActive) continue;
            if (node.behavior == BEHAVIOR_ROTATE) {
                node.localRotation += node.behaviorVec * node.behaviorSpeed * dt;
            } else if (node.behavior == BEHAVIOR_PINGPONG) {
                // Узлы, созданные скриптом во время игры, получают базу в момент появления
                while ((int)basePositions.size() <= i) { basePositions.push_back(sceneGraph[basePositions.size()].localPosition); phases.push_back(0.0f); }
                phases[i] += node.behaviorSpeed * dt;
                node.localPosition = basePositions[i] + node.behaviorVec * (0.5f - 0.5f * std::cos(phases[i]));
            }
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
    physics::reset();
    savedGravity = physics::gravity;
    for (auto& n : sceneGraph) { n.body.sleeping = false; n.body.sleepTimer = 0.0f; }
    connectScriptHost(window);
    nls::scripts.start();

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwGetCursorPos(window, &play.lastMouseX, &play.lastMouseY);
    play.active = true;
    cameraOverrideBasis = getPlayCameraBasis();
    cameraOverride = true;
    spdlog::info("Play Mode started");
}

void stopPlay(GLFWwindow* window) {
    if (!play.active) return;
    play.active = false;
    cameraOverride = false;
    nls::scripts.stop();
    physics::gravity = savedGravity;
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

    // Скрипты и встроенные поведения
    pollKeys();
    nls::host.time += dt;
    nls::host.delta = dt;
    nls::scripts.update(dt);
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

    // Физике игрок виден как две сферы, движущиеся с желаемой скоростью (до упора в стены)
    vec3 pushVelocity = play.vel;

    // Несколько подшагов, чтобы не пролетать сквозь тонкие стены
    const int substeps = 3;
    float h = dt / substeps;
    for (int s = 0; s < substeps; s++) {
        play.vel += physics::gravity * h;
        play.pos += play.vel * h;
        bool ground = resolveSphere(vec3(0.0f));
        resolveSphere(vec3(0, PLAYER_EYE_HEIGHT, 0));
        play.grounded = ground;
    }

    if (play.pos.y < KILL_HEIGHT) { play.pos = play.spawn; play.vel = vec3(0); }

    std::vector<physics::KinematicSphere> spheres = {
        { play.pos, PLAYER_RADIUS, pushVelocity, physics::PLAYER_ID },
        { play.pos + vec3(0, PLAYER_EYE_HEIGHT, 0), PLAYER_RADIUS, pushVelocity, physics::PLAYER_ID },
    };
    physics::Events events;
    events.onCollide = [](int self, int other, vec3, vec3, float) { nls::scripts.onCollide(self, other); };
    events.onTrigger = [](int trigger, int other, bool entered) {
        if (trigger < 0 || trigger >= (int)sceneGraph.size()) return;
        if (entered) applyTrigger(sceneGraph[trigger]);
        nls::scripts.onTrigger(trigger, other, entered);
    };
    physics::step(dt, spheres, events, [](float h) { nls::scripts.fixedUpdate(h); });

    cameraOverrideBasis = getPlayCameraBasis();
}

void drawToast() {
    if (!toast.active) return;
    ImGuiIO& io = ImGui::GetIO();
    float width = std::max(200.0f, ImGui::CalcTextSize(toast.message.c_str()).x + 40.0f);
    ImGui::SetNextWindowPos(ImVec2(viewportRect.x + (viewportRect.z - width) * 0.5f, viewportRect.y + viewportRect.w - 60));
    ImGui::SetNextWindowSize(ImVec2(width, 40));
    ImGui::Begin("Toast", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(toast.message.c_str()).x) * 0.5f);
    ImGui::TextUnformatted(toast.message.c_str());
    ImGui::End();
    toast.timer -= io.DeltaTime;
    if (toast.timer <= 0) toast.active = false;
}

void drawPlayHud() {
    ImGuiIO& io = ImGui::GetIO();
    viewportRect = vec4(0, 0, io.DisplaySize.x, io.DisplaySize.y);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    dl->AddCircleFilled(c, 2.5f, IM_COL32(255, 255, 255, 200));
    if (!play.standalone) dl->AddText(ImVec2(10, 8), IM_COL32(255, 255, 255, 180), "PLAY MODE  |  WASD move, Space jump, Shift run  |  Esc or F5 to stop");
    if (editorCfg.showStats && !play.standalone) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%.0f FPS | SDF %dx%d | GPU %.2f ms", io.Framerate, renderStats.renderW, renderStats.renderH, renderStats.gpuMs);
        dl->AddText(ImVec2(10, 26), IM_COL32(255, 255, 255, 180), buf);
    }
    drawToast();
}
