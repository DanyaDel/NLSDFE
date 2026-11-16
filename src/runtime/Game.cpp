#include "Game.h"
#include "Scene.h"
#include "Physics.h"
#include "Audio.h"
#include "ScriptSystem.h"
#include "GameApi.h"
#include "UiLayout.h"
#include "Particles.h"
#include "Animation.h"
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
    std::vector<SceneNode> startScene;     // Сцена на момент запуска — для scene.restart()
    std::string savedLoadedFile;
    std::vector<int> savedSelection;
    std::vector<vec3> basePositions;
    std::vector<float> phases;
    std::vector<uint32_t> generations;     // Поколения узлов: по ним видно, что место занял новый узел
    bool autoCapture = false;              // Захват курсора, выбранный движком для сцены
    int particleFrame = 0;
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
        keyNames["escape"] = GLFW_KEY_ESCAPE; keyNames["backspace"] = GLFW_KEY_BACKSPACE;
        // Кнопки геймпада "pad0".."pad14" (раскладка Xbox: 0 — A, 1 — B, ...): их можно назначать действиям ввода
        for (int b = 0; b < 15; b++) keyNames["pad" + std::to_string(b)] = -100 - b;
    }

    bool rawKeyDown(int code) {
        if (code <= -100) return nls::game::state.padButtons[std::min(-100 - code, 14)];
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
        nls::host.lastKey = []() {
            for (const auto& [name, code] : keyNames) if (keysNow[code] && !keysBefore[code]) return name;
            return std::string();
        };
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
            // Клавиши — через действия ввода: игра может их переназначить (input.bind)
            if (nls::game::actionDown("forward")) wish += forward;
            if (nls::game::actionDown("back")) wish -= forward;
            if (nls::game::actionDown("right")) wish += right;
            if (nls::game::actionDown("left")) wish -= right;
            if (glm::dot(wish, wish) > 0.0f) wish = glm::normalize(wish);
            float speed = nls::game::actionDown("run") ? cc.runSpeed : cc.walkSpeed;
            state.velocity.x = wish.x * speed;
            state.velocity.z = wish.z * speed;
            if (state.grounded && nls::game::actionDown("jump")) { state.velocity.y = cc.jumpSpeed; state.grounded = false; }

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

    // Мышь и геймпад для скриптов
    void pollPointerAndPad(GLFWwindow* window, float mouseDX, float mouseDY) {
        nls::game::State& g = nls::game::state;
        int w = 1, h = 1; glfwGetWindowSize(window, &w, &h);
        double mx, my; glfwGetCursorPos(window, &mx, &my);
        vec4 area = viewportRect.z > 1.0f ? viewportRect : vec4(0, 0, (float)w, (float)h);
        g.mouseX = ((float)mx - area.x) / std::max(area.z, 1.0f);
        g.mouseY = ((float)my - area.y) / std::max(area.w, 1.0f);
        g.mouseDX = mouseDX; g.mouseDY = mouseDY;
        bool down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        g.mousePressed = down && !g.mouseDown;
        g.mouseDown = down;

        std::copy(std::begin(g.padButtons), std::end(g.padButtons), std::begin(g.padButtonsBefore));
        GLFWgamepadstate pad;
        bool found = false;
        for (int id = GLFW_JOYSTICK_1; id <= GLFW_JOYSTICK_LAST && !found; id++) {
            if (!glfwJoystickIsGamepad(id) || !glfwGetGamepadState(id, &pad)) continue;
            found = true;
            // Мёртвая зона: стики в покое не дают ровно ноль
            for (int a = 0; a < 6; a++) g.padAxes[a] = (a < 4 && std::fabs(pad.axes[a]) < 0.15f) ? 0.0f : pad.axes[a];
            for (int b = 0; b < 15; b++) g.padButtons[b] = pad.buttons[b] == GLFW_PRESS;
        }
        if (!found) { std::fill(std::begin(g.padAxes), std::end(g.padAxes), 0.0f); std::fill(std::begin(g.padButtons), std::end(g.padButtons), false); }
    }

    // Узлы, появившиеся во время игры (scene.spawn, scene.instantiate), в том числе на месте удалённых:
    // состояние прежнего хозяина места сбрасывается, компоненты нового запускаются
    void adoptNewNodes() {
        for (int i = 0; i < (int)sceneGraph.size(); i++) {
            if (i < (int)generations.size() && generations[i] == sceneGraph[i].generation) continue;
            if (i >= (int)generations.size()) { generations.push_back(0); basePositions.push_back(vec3(0.0f)); phases.push_back(0.0f); }
            SceneNode& n = sceneGraph[i];
            generations[i] = n.generation;
            basePositions[i] = n.localPosition;
            phases[i] = 0.0f;
            controllers.erase(i);
            stopNodeSound(i);
            if (n.destroyed) continue;
            if (n.audio.enabled && n.audio.playOnStart) playNodeSound(i);
            if (n.animation.enabled && n.animation.playOnStart) anim::play(n, true);
        }
    }

    // input.showCursor() перекрывает выбор движка
    void applyCursorMode(GLFWwindow* window) {
        int wanted = nls::game::state.cursorVisible;
        bool capture = wanted < 0 ? autoCapture : (wanted == 0 && play.mouseLook);
        if (capture == play.cursorCaptured) return;
        play.cursorCaptured = capture;
        glfwSetInputMode(window, GLFW_CURSOR, capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (capture && glfwRawMouseMotionSupported()) glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        glfwGetCursorPos(window, &play.lastMouseX, &play.lastMouseY);
        play.mouseSettle = 3;
    }

    // Частица не видна, если между ней и камерой есть геометрия
    bool particleHidden(vec3 eye, vec3 point) {
        vec3 dir = point - eye;
        float dist = glm::length(dir);
        if (dist < 1e-4f) return false;
        dir /= dist;
        float t = 0.05f;
        for (int step = 0; step < 32 && t < dist; step++) {
            float d = sceneDistance(eye + dir * t);
            if (d < 0.01f) return true;
            t += d;
        }
        return false;
    }

    void drawParticles(ImDrawList* dl) {
        if (particles::pool.empty()) return;
        CameraBasis cam = getCameraBasis();
        float focal = getCameraFocalLength();
        particleFrame++;
        for (size_t i = 0; i < particles::pool.size(); i++) {
            particles::Particle& p = particles::pool[i];
            // Проверка видимости дорогая: каждая частица проходит её раз в четыре кадра
            if ((int)((i + particleFrame) & 3) == 0 || p.age == 0.0f) p.hidden = particleHidden(cam.pos, p.position);
            // Освещаемые частицы: небо плюс солнце, если оно не закрыто геометрией
            if (p.lit && ((int)((i + particleFrame) & 7) == 0 || p.age == 0.0f)) {
                bool shadowed = particleHidden(p.position, p.position + sunDirection(world) * 40.0f);
                p.light = std::min(world.ambient + (shadowed ? 0.0f : world.sunIntensity * 0.3f), 1.3f);
            }
            if (p.hidden) continue;
            vec2 screen;
            if (!projectWorldToScreen(p.position, screen)) continue;
            float depth = glm::dot(p.position - cam.pos, cam.fwd);
            float k = std::clamp(p.age / p.life, 0.0f, 1.0f);
            float radius = glm::mix(p.sizeStart, p.sizeEnd, k) * focal * viewportRect.w * 0.5f / std::max(depth, 0.05f);
            if (radius < 0.3f) continue;
            vec3 color = glm::mix(p.colorStart, p.colorEnd, k);
            if (p.lit) color = glm::min(color * p.light * glm::mix(vec3(1.0f), world.sunColor, 0.5f), vec3(1.0f));
            float alpha = 1.0f - k * k;
            dl->AddCircleFilled(ImVec2(screen.x, screen.y), std::min(radius, 400.0f), ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, alpha)), radius > 12.0f ? 24 : 12);
        }
    }

    // Общая часть запуска игры и смены сцены: всё состояние по узлам строится заново
    void beginScene(GLFWwindow* window) {
        selectedIndices.clear();
        basePositions.clear();
        for (const auto& n : sceneGraph) basePositions.push_back(n.localPosition);
        phases.assign(sceneGraph.size(), 0.0f);
        generations.clear();
        for (const auto& n : sceneGraph) generations.push_back(n.generation);
        controllers.clear();
        physics::reset();
        for (auto& n : sceneGraph) { n.body.sleeping = false; n.body.sleepTimer = 0.0f; }

        updateSceneCaches();
        play.cameraNode = findMainCamera();
        refreshCamera();

        // Курсор захватывается, только если кому-то нужен обзор мышью
        bool wantsMouse = false;
        for (const auto& n : sceneGraph) if (n.controller.enabled && n.controller.mouseLook && n.worldActive) wantsMouse = true;
        play.cursorCaptured = autoCapture = wantsMouse && play.mouseLook;
        glfwSetInputMode(window, GLFW_CURSOR, play.cursorCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (play.cursorCaptured && glfwRawMouseMotionSupported()) glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        glfwGetCursorPos(window, &play.lastMouseX, &play.lastMouseY);
        play.mouseSettle = 3;
    }

    void startSceneLogic() {
        anim::startAll();
        nls::scripts.start();
        for (int i = 0; i < (int)sceneGraph.size(); i++) {
            const SceneNode& n = sceneGraph[i];
            if (n.audio.enabled && n.audio.playOnStart && n.worldActive) playNodeSound(i);
        }
    }

    // scene.load("level2"): ищем в scenes/ проекта и рядом с игрой
    std::string findSceneFile(const std::string& name) {
        namespace fs = std::filesystem;
        fs::path p(name);
        if (p.extension().empty()) p += ".nos";
        std::error_code ec;
        if (p.is_absolute()) return fs::exists(p, ec) ? p.string() : "";
        fs::path root(nls::scripts.rootDir);
        for (const fs::path& candidate : { root / "scenes" / p, root / p }) if (fs::exists(candidate, ec)) return candidate.string();
        return "";
    }

    // Смена сцены между кадрами: скрипты и звук останавливаются, новая сцена стартует с нуля.
    // file пустой — перезапуск сцены, с которой началась игра.
    void switchScene(GLFWwindow* window, const std::string& file) {
        nls::scripts.stop();
        audio::stopAll();
        nodeSounds.clear();
        if (file.empty()) sceneGraph = startScene;
        else {
            std::string keepFile = currentLoadedFile;
            bool ok = loadScene(file, false);
            currentLoadedFile = keepFile;
            if (!ok) { sceneGraph = startScene; showToast("Cannot load scene"); spdlog::error("scene.load: cannot read {}", file); }
            else startScene = sceneGraph;
        }
        nls::game::reset();
        beginScene(window);
        nls::host.time = 0.0f;
        startSceneLogic();
    }
}

bool isPlayMode() { return play.active; }

void startPlay(GLFWwindow* window) {
    if (play.active) return;

    // Вид редактора запоминаем: им пользуемся, если в сцене нет камеры
    CameraBasis editorView = getCameraBasis();

    savedScene = sceneGraph;
    savedSelection = selectedIndices;
    startScene = sceneGraph;
    savedLoadedFile = currentLoadedFile;
    savedGravity = physics::gravity;
    cameraOverrideBasis = editorView;
    cameraOverrideFov = 0.0f;
    cameraOverride = true;
    nls::game::reset();
    if (!nls::scripts.rootDir.empty()) nls::game::setSaveFile((std::filesystem::path(nls::scripts.rootDir) / "save.dat").string());
    beginScene(window);
    if (play.cameraNode < 0 && !play.standalone) showToast("No camera in the scene: Add > Camera");

    audio::init();
    connectScriptHost(window);
    play.active = true;
    startSceneLogic();
    spdlog::info("Play Mode started");
}

void stopPlay(GLFWwindow* window) {
    if (!play.active) return;
    play.active = false;
    cameraOverride = false;
    cameraOverrideFov = 0.0f;
    nls::scripts.stop();
    if (!nls::game::flushSave()) spdlog::error("Cannot write the save file");
    nls::game::reset();
    audio::stopAll();
    audio::stopMusic();
    nodeSounds.clear();
    startScene.clear();
    physics::gravity = savedGravity;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    play.cursorCaptured = false;
    if (play.standalone) { glfwSetWindowShouldClose(window, GLFW_TRUE); return; }
    sceneGraph = savedScene;
    currentLoadedFile = savedLoadedFile;
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

    // Скрипты и встроенные поведения. Ввод читается и на паузе (time.setScale(0)) — меню должно работать
    pollKeys();
    pollPointerAndPad(window, mouseDX, mouseDY);
    nls::ui::updateInput(nls::game::state.mouseX, nls::game::state.mouseY, nls::game::state.mouseDown, nls::game::state.mousePressed);
    dt *= nls::game::state.timeScale;
    nls::host.time += dt;
    nls::host.delta = dt;
    nls::game::beginFrame();
    nls::scripts.update(dt);
    nls::game::updateTweens(dt);
    anim::update(dt);
    nls::game::updateNavigation(dt);
    adoptNewNodes();
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
    particles::update(dt, physics::gravity);
    // Удалённые узлы освобождают места здесь: ни скрипты, ни физика сейчас не работают
    flushDestroyed();
    applyCursorMode(window);

    // Запросы скриптов исполняются между кадрами, когда ни один скрипт не работает
    nls::game::State& g = nls::game::state;
    if (g.quitRequested) { stopPlay(window); return; }
    if (g.restartRequested) switchScene(window, "");
    else if (!g.pendingScene.empty()) {
        std::string file = findSceneFile(g.pendingScene);
        if (file.empty()) { showToast("Scene not found: " + g.pendingScene); spdlog::error("scene.load: {} not found", g.pendingScene); g.pendingScene.clear(); }
        else switchScene(window, file);
    }
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

void drawParticlesOverlay(ImDrawList* dl) { drawParticles(dl); }

void drawHudItems(ImDrawList* dl, vec4 rect, const std::vector<nls::game::HudItem>& items) {
    float textScale = rect.w / 720.0f;
    for (const nls::game::HudItem& item : items) {
        ImVec2 a(rect.x + item.x * rect.z, rect.y + item.y * rect.w);
        ImVec2 b(a.x + item.w * rect.z, a.y + item.h * rect.w);
        ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(item.color.x, item.color.y, item.color.z, 1.0f));
        if (item.kind == nls::game::HUD_TEXT) {
            float size = std::max(item.value * textScale, 4.0f);
            dl->AddText(ImGui::GetFont(), size, ImVec2(a.x + 1, a.y + 1), IM_COL32(0, 0, 0, 160), item.text.c_str());
            dl->AddText(ImGui::GetFont(), size, a, color, item.text.c_str());
        } else if (item.kind == nls::game::HUD_BAR) {
            dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 150), 3.0f);
            dl->AddRectFilled(ImVec2(a.x + 2, a.y + 2), ImVec2(a.x + 2 + std::max(b.x - a.x - 4, 0.0f) * item.value, b.y - 2), color, 2.0f);
        } else {
            dl->AddRectFilled(a, b, color, 4.0f);
            if (!item.text.empty()) {
                float size = std::max(item.textSize * textScale, 4.0f);
                ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, item.text.c_str());
                dl->AddText(ImGui::GetFont(), size, ImVec2((a.x + b.x - extent.x) * 0.5f, (a.y + b.y - extent.y) * 0.5f), IM_COL32(255, 255, 255, 255), item.text.c_str());
            }
        }
    }
}

void drawPlayHud() {
    ImGuiIO& io = ImGui::GetIO();
    viewportRect = vec4(0, 0, io.DisplaySize.x, io.DisplaySize.y);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    drawParticles(dl);
    // Экраны из UI Editor, поверх них — то, что скрипты нарисовали за кадр
    std::vector<nls::game::HudItem> items;
    for (const nls::ui::Layout& layout : nls::ui::shown) nls::ui::appendItems(layout, items);
    drawHudItems(dl, viewportRect, items);
    drawHudItems(dl, viewportRect, nls::game::state.hud);
    if (play.cursorCaptured) dl->AddCircleFilled(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), 2.5f, IM_COL32(255, 255, 255, 200));
    if (!play.standalone) {
        dl->AddText(ImVec2(10, 8), IM_COL32(255, 255, 255, 180), "PLAY MODE  |  Esc or F5 to stop");
        // Ошибки скриптов видны прямо в игре: последние строки консоли, пока они свежие
        static size_t seenLines = 0;
        static float errorTimer = 0.0f;
        const auto& console = nls::scripts.console;
        if (console.size() < seenLines) seenLines = 0;
        for (size_t i = seenLines; i < console.size(); i++) if (console[i].compare(0, 5, "ERROR") == 0) errorTimer = 8.0f;
        seenLines = console.size();
        if (errorTimer > 0.0f) {
            errorTimer -= io.DeltaTime;
            float y = io.DisplaySize.y - 30.0f;
            int shown = 0;
            for (int i = (int)console.size() - 1; i >= 0 && shown < 4; i--) {
                if (console[i].compare(0, 5, "ERROR") != 0) continue;
                ImVec2 size = ImGui::CalcTextSize(console[i].c_str());
                dl->AddRectFilled(ImVec2(8, y - 2), ImVec2(16 + size.x, y + size.y + 2), IM_COL32(60, 10, 10, 220), 3.0f);
                dl->AddText(ImVec2(12, y), IM_COL32(255, 140, 140, 255), console[i].c_str());
                y -= size.y + 6.0f;
                shown++;
            }
        }
        if (editorCfg.showStats) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%.0f FPS | SDF %dx%d | GPU %.2f ms", io.Framerate, renderStats.renderW, renderStats.renderH, renderStats.gpuMs);
            dl->AddText(ImVec2(10, 26), IM_COL32(255, 255, 255, 180), buf);
        }
    }
    drawToast();
}
