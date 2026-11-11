#include "Game.h"
#include "Scene.h"
#include "Physics.h"
#include "Audio.h"
#include "ScriptSystem.h"
#include "Settings.h"
#include "Renderer.h"
#include "MathUtils.h"
#include "imgui.h"
#include <algorithm>
#include <filesystem>
#include <map>

PlayState play;

namespace {
    // Состояние редактора, которое возвращается после выхода из Play Mode
    std::vector<SceneNode> savedScene;
    std::vector<int> savedSelection;
    std::vector<vec3> basePositions;
    std::vector<float> phases;
    vec3 savedGravity;

    // Состояние персонажей и звуков по индексу узла
    struct ControllerState { vec3 velocity = {0, 0, 0}; bool grounded = false; float pitch = 0.0f; };
    std::map<int, ControllerState> controllers;
    std::map<int, int> nodeSounds;

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

    // --- Звук ---
    std::string resolveAsset(const std::string& path) {
        std::filesystem::path p(path);
        if (p.is_absolute() || nls::scripts.rootDir.empty()) return path;
        return (std::filesystem::path(nls::scripts.rootDir) / p).string();
    }

    void playNodeSound(int node) {
        if (node < 0 || node >= (int)sceneGraph.size()) return;
        const SceneNode& n = sceneGraph[node];
        if (n.audio.clip.empty()) return;
        auto it = nodeSounds.find(node);
        if (it != nodeSounds.end()) audio::stop(it->second);
        nodeSounds[node] = audio::play(resolveAsset(n.audio.clip), n.audio.volume, n.audio.loop, n.audio.spatial, vec3(n.gpuData.position), n.audio.range);
    }

    void stopNodeSound(int node) {
        auto it = nodeSounds.find(node);
        if (it == nodeSounds.end()) return;
        audio::stop(it->second);
        nodeSounds.erase(it);
    }

    void connectScriptHost(GLFWwindow* window) {
        inputWindow = window;
        buildKeyNames();
        keysNow.clear(); keysBefore.clear();
        nls::host.time = 0.0f;
        nls::host.delta = 0.0f;
        nls::host.keyDown = [](const std::string& key) { int code = keyCode(key); return code != 0 && keysNow[code]; };
        nls::host.keyPressed = [](const std::string& key) { int code = keyCode(key); return code != 0 && keysNow[code] && !keysBefore[code]; };
        nls::host.playSound = playNodeSound;
        nls::host.stopSound = stopNodeSound;
        nls::host.resolvePath = resolveAsset;
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

    int findMainCamera() {
        for (int i = 0; i < (int)sceneGraph.size(); i++) {
            if (nodeType(sceneGraph[i]) == NODE_CAMERA && sceneGraph[i].worldActive) return i;
        }
        return -1;
    }

    // Камера смотрит вдоль своей локальной оси -Z
    void refreshCamera() {
        if (play.cameraNode < 0 || play.cameraNode >= (int)sceneGraph.size()) return;
        mat4 m = calculateWorldMatrix(play.cameraNode);
        vec3 scale;
        mat3 r = extractRotation(m, scale);
        cameraOverrideBasis = { vec3(m[3]), -r[2], r[0], r[1] };
        cameraOverrideFov = sceneGraph[play.cameraNode].cameraFov;
    }

    // Персонажи: ввод -> желаемая скорость -> капсула в мире коллайдеров
    void updateControllers(GLFWwindow* window, float dt, float mouseDX, float mouseDY, std::vector<physics::KinematicSphere>& spheres) {
        for (int i = 0; i < (int)sceneGraph.size(); i++) {
            SceneNode& node = sceneGraph[i];
            const CharacterController& cc = node.controller;
            if (!cc.enabled || !node.worldActive) continue;
            ControllerState& state = controllers[i];

            if (cc.mouseLook) {
                node.localRotation.y -= mouseDX * 0.0025f;
                state.pitch = std::clamp(state.pitch - mouseDY * 0.0025f, -1.5f, 1.5f);
                // Наклон головы достаётся дочерним камерам, само тело остаётся вертикальным
                for (int child : node.children) if (nodeType(sceneGraph[child]) == NODE_CAMERA) sceneGraph[child].localRotation.x = state.pitch;
            }

            float yaw = node.localRotation.y;
            vec3 forward = { -std::sin(yaw), 0.0f, -std::cos(yaw) };
            vec3 right = { std::cos(yaw), 0.0f, -std::sin(yaw) };
            vec3 wish(0.0f);
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) wish += forward;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) wish -= forward;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) wish += right;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) wish -= right;
            if (glm::dot(wish, wish) > 0.0f) wish = glm::normalize(wish);
            float speed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? cc.runSpeed : cc.walkSpeed;
            state.velocity.x = wish.x * speed;
            state.velocity.z = wish.z * speed;
            if (state.grounded && glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) { state.velocity.y = cc.jumpSpeed; state.grounded = false; }

            // Тела персонаж толкает желаемой скоростью — той, что была до упора в стены
            vec3 pushVelocity = state.velocity;

            physics::Character body;
            body.node = i;
            body.position = vec3(node.gpuData.position);
            body.velocity = state.velocity;
            body.radius = cc.radius;
            body.height = cc.height;
            body.grounded = state.grounded;
            physics::moveCharacter(body, dt);
            state.velocity = body.velocity;
            state.grounded = body.grounded;

            // Мировое положение обратно в локальное
            if (node.parent == -1) node.localPosition = body.position;
            else node.localPosition = vec3(glm::inverse(calculateWorldMatrix(node.parent)) * vec4(body.position, 1.0f));

            float half = std::max(cc.height * 0.5f - cc.radius, 0.0f);
            spheres.push_back({ body.position - vec3(0, half, 0), cc.radius, pushVelocity, i });
            if (half > 0.0f) spheres.push_back({ body.position + vec3(0, half, 0), cc.radius, pushVelocity, i });
        }
    }

    void updateAudio() {
        if (!audio::available()) return;
        CameraBasis b = getCameraBasis();
        audio::setListener(b.pos, b.fwd, b.up);
        for (auto it = nodeSounds.begin(); it != nodeSounds.end();) {
            int node = it->first;
            bool alive = node < (int)sceneGraph.size() && audio::isPlaying(it->second);
            // Скрытый узел замолкает
            if (alive && !sceneGraph[node].worldActive) { audio::stop(it->second); alive = false; }
            if (!alive) { it = nodeSounds.erase(it); continue; }
            if (sceneGraph[node].audio.spatial) audio::setPosition(it->second, vec3(sceneGraph[node].gpuData.position));
            ++it;
        }
        audio::collectFinished();
    }
}

bool isPlayMode() { return play.active; }

void startPlay(GLFWwindow* window) {
    if (play.active) return;

    // Вид редактора запоминаем: им пользуемся, если в сцене нет камеры
    CameraBasis editorView = getCameraBasis();

    savedScene = sceneGraph;
    savedSelection = selectedIndices;
    selectedIndices.clear();
    basePositions.clear();
    for (const auto& n : sceneGraph) basePositions.push_back(n.localPosition);
    phases.assign(sceneGraph.size(), 0.0f);
    controllers.clear();
    physics::reset();
    savedGravity = physics::gravity;
    for (auto& n : sceneGraph) { n.body.sleeping = false; n.body.sleepTimer = 0.0f; }

    updateSceneCaches();
    play.cameraNode = findMainCamera();
    cameraOverrideBasis = editorView;
    cameraOverrideFov = 0.0f;
    cameraOverride = true;
    refreshCamera();
    if (play.cameraNode < 0 && !play.standalone) showToast("No camera in the scene: Add > Camera");

    // Курсор захватывается, только если кому-то нужен обзор мышью
    bool wantsMouse = false;
    for (const auto& n : sceneGraph) if (n.controller.enabled && n.controller.mouseLook && n.worldActive) wantsMouse = true;
    play.cursorCaptured = wantsMouse && play.mouseLook;
    if (play.cursorCaptured) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    glfwGetCursorPos(window, &play.lastMouseX, &play.lastMouseY);
    play.mouseSettle = 3;

    audio::init();
    connectScriptHost(window);
    play.active = true;
    nls::scripts.start();
    for (int i = 0; i < (int)sceneGraph.size(); i++) {
        const SceneNode& n = sceneGraph[i];
        if (n.audio.enabled && n.audio.playOnStart && n.worldActive) playNodeSound(i);
    }
    spdlog::info("Play Mode started");
}

void stopPlay(GLFWwindow* window) {
    if (!play.active) return;
    play.active = false;
    cameraOverride = false;
    cameraOverrideFov = 0.0f;
    nls::scripts.stop();
    audio::stopAll();
    nodeSounds.clear();
    physics::gravity = savedGravity;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    play.cursorCaptured = false;
    if (play.standalone) { glfwSetWindowShouldClose(window, GLFW_TRUE); return; }
    sceneGraph = savedScene;
    selectedIndices = savedSelection;
    savedScene.clear();
    spdlog::info("Play Mode stopped");
}

void updatePlay(GLFWwindow* window, float deltaTime) {
    if (!play.active) return;
    float dt = std::min(deltaTime, 1.0f / 20.0f);

    // При захвате курсора система может прислать скачок позиции — первые кадры его пропускаем
    double mx, my; glfwGetCursorPos(window, &mx, &my);
    float mouseDX = 0.0f, mouseDY = 0.0f;
    if (play.mouseSettle > 0) play.mouseSettle--;
    else if (play.cursorCaptured) { mouseDX = (float)(mx - play.lastMouseX); mouseDY = (float)(my - play.lastMouseY); }
    play.lastMouseX = mx; play.lastMouseY = my;

    // Скрипты и встроенные поведения
    pollKeys();
    nls::host.time += dt;
    nls::host.delta = dt;
    nls::scripts.update(dt);
    updateBehaviors(dt);

    std::vector<physics::KinematicSphere> spheres;
    updateControllers(window, dt, mouseDX, mouseDY, spheres);

    physics::Events events;
    events.onCollide = [](int self, int other, vec3, vec3, float) { nls::scripts.onCollide(self, other); };
    events.onTrigger = [](int trigger, int other, bool entered) {
        if (trigger < 0 || trigger >= (int)sceneGraph.size()) return;
        if (entered) applyTrigger(sceneGraph[trigger]);
        nls::scripts.onTrigger(trigger, other, entered);
    };
    physics::step(dt, spheres, events, [](float h) { nls::scripts.fixedUpdate(h); });

    // Активная камера могла смениться (скрипт спрятал одну и показал другую)
    if (play.cameraNode < 0 || play.cameraNode >= (int)sceneGraph.size() || !sceneGraph[play.cameraNode].active) play.cameraNode = findMainCamera();
    refreshCamera();
    updateAudio();
}

void drawToast() {
    if (!toast.active) return;
    ImGuiIO& io = ImGui::GetIO();
    float width = std::max(200.0f, ImGui::CalcTextSize(toast.message.c_str()).x + 40.0f);
    vec4 area = viewportRect.z > 1.0f ? viewportRect : vec4(0, 0, io.DisplaySize.x, io.DisplaySize.y);
    ImGui::SetNextWindowPos(ImVec2(area.x + (area.z - width) * 0.5f, area.y + area.w - 60));
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
    if (play.cursorCaptured) dl->AddCircleFilled(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), 2.5f, IM_COL32(255, 255, 255, 200));
    if (!play.standalone) {
        dl->AddText(ImVec2(10, 8), IM_COL32(255, 255, 255, 180), "PLAY MODE  |  Esc or F5 to stop");
        if (editorCfg.showStats) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%.0f FPS | SDF %dx%d | GPU %.2f ms", io.Framerate, renderStats.renderW, renderStats.renderH, renderStats.gpuMs);
            dl->AddText(ImVec2(10, 26), IM_COL32(255, 255, 255, 180), buf);
        }
    }
    drawToast();
}
