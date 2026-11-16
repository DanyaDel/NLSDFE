// Компоненты Animation и Particles в инспекторе: дорожка ключевых кадров с прокруткой
// и предпросмотр прямо во вьюпорте редактора, без запуска игры.
#include "Panels.h"
#include "Scene.h"
#include "Animation.h"
#include "Particles.h"
#include "Physics.h"
#include "Game.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace {
    float cursor = 0.0f;          // Время на дорожке, секунды; общее для инспектора и панели Timeline
    int previewFrame = -1;        // Кадр, в котором предпросмотр уже продвинут
    bool previewing = false;
    bool particlePreview = false;

    void undoOnEdit() { if (ImGui::IsItemActivated()) saveUndo(); }

    // Поза узла на момент курсора с учётом его режима повтора
    void poseAt(SceneNode& node, float time) {
        const NodeAnimation& a = node.animation;
        float length = anim::length(a);
        if (a.keys.empty()) return;
        float t = time * a.speed;
        if (length > 0.0f && a.loop) {
            if (a.pingPong) { float cycle = std::fmod(std::fabs(t), length * 2.0f); t = cycle <= length ? cycle : length * 2.0f - cycle; }
            else if (t > length) t = std::fmod(t, length);
        }
        anim::apply(node, t);
    }

    void poseAll() { for (SceneNode& n : sceneGraph) if (n.animation.enabled) poseAt(n, cursor); }

    // Предпросмотр идёт раз в кадр, кто бы его ни рисовал: инспектор или панель Timeline
    void advancePreview() {
        if (!previewing || previewFrame == ImGui::GetFrameCount()) return;
        previewFrame = ImGui::GetFrameCount();
        float longest = 0.0f;
        bool endless = false;
        for (const SceneNode& n : sceneGraph) {
            if (!n.animation.enabled || n.animation.keys.size() < 2) continue;
            longest = std::max(longest, anim::length(n.animation) / std::max(std::fabs(n.animation.speed), 0.01f));
            endless |= n.animation.loop;
        }
        if (longest <= 0.0f) { previewing = false; return; }
        cursor += ImGui::GetIO().DeltaTime;
        // Без зацикленных анимаций предпросмотр останавливается на конце самой длинной
        if (!endless && cursor >= longest) { cursor = longest; previewing = false; }
        poseAll();
    }

    void togglePreview() {
        if (previewing) { previewing = false; return; }
        saveUndo();
        previewing = true;
    }
}

bool editorPreviewActive() { return previewing || particlePreview; }

void stopEditorPreview() {
    previewing = false;
    if (particlePreview) particles::clear();
    particlePreview = false;
}

void drawAnimationComponent(SceneNode& node, int nodeIdx) {
    NodeAnimation& a = node.animation;
    (void)nodeIdx;
    advancePreview();
    float length = anim::length(a);

    // --- Дорожка ---
    float span = std::max(length, 1.0f) * 1.1f;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x, height = 34.0f;
    ImGui::InvisibleButton("##track", ImVec2(width, height));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(30, 32, 40, 255), 4.0f);
    auto timeToX = [&](float t) { return origin.x + 6.0f + (width - 12.0f) * std::clamp(t / span, 0.0f, 1.0f); };
    for (float t = 0.0f; t <= span; t += span > 12.0f ? 5.0f : (span > 4.0f ? 1.0f : 0.5f)) {
        dl->AddLine(ImVec2(timeToX(t), origin.y + height - 8.0f), ImVec2(timeToX(t), origin.y + height), IM_COL32(110, 115, 130, 255));
    }
    if (ImGui::IsItemActivated() && !a.keys.empty()) saveUndo();
    if (ImGui::IsItemActive()) {
        // Перетаскивание по дорожке ставит узел в позу этого момента; рядом с ключом курсор прилипает к нему
        cursor = std::clamp((ImGui::GetIO().MousePos.x - origin.x - 6.0f) / std::max(width - 12.0f, 1.0f), 0.0f, 1.0f) * span;
        for (const Keyframe& k : a.keys) if (std::fabs(timeToX(k.time) - ImGui::GetIO().MousePos.x) < 5.0f) cursor = k.time;
        previewing = false;
        if (!a.keys.empty()) anim::apply(node, cursor);
    }
    int keyAtCursor = -1;
    for (int i = 0; i < (int)a.keys.size(); i++) {
        bool here = std::fabs(a.keys[i].time - cursor) < 1e-3f;
        if (here) keyAtCursor = i;
        ImVec2 c(timeToX(a.keys[i].time), origin.y + height * 0.42f);
        dl->AddQuadFilled(ImVec2(c.x, c.y - 6), ImVec2(c.x + 6, c.y), ImVec2(c.x, c.y + 6), ImVec2(c.x - 6, c.y), here ? IM_COL32(255, 200, 60, 255) : IM_COL32(200, 205, 220, 255));
    }
    dl->AddLine(ImVec2(timeToX(cursor), origin.y), ImVec2(timeToX(cursor), origin.y + height), IM_COL32(90, 160, 255, 255), 2.0f);

    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::DragFloat("Time", &cursor, 0.01f, 0.0f, 600.0f, "%.2f s")) {
        cursor = std::max(cursor, 0.0f);
        previewing = false;
        if (!a.keys.empty()) anim::apply(node, cursor);
    }
    undoOnEdit();
    ImGui::SameLine();
    if (ImGui::Button(previewing ? "Stop" : "Preview")) togglePreview();

    // Ключ запоминает текущую трансформацию узла: подвинуть объект гизмо — нажать Key
    if (ImGui::Button(keyAtCursor >= 0 ? "Update Key" : "Add Key")) {
        saveUndo();
        anim::insertKey(a, anim::keyFromNode(node, cursor));
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Store the object's current position, rotation and scale at this time");
    ImGui::SameLine();
    ImGui::BeginDisabled(keyAtCursor < 0);
    if (ImGui::Button("Delete Key")) { saveUndo(); a.keys.erase(a.keys.begin() + keyAtCursor); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%d keys, %.2f s", (int)a.keys.size(), length);
    if (a.keys.size() < 2) ImGui::TextDisabled("Move the time, pose the object, press Add Key");

    bool loop = a.loop, ping = a.pingPong, onStart = a.playOnStart;
    if (ImGui::Checkbox("Loop", &loop)) { saveUndo(); a.loop = loop; }
    ImGui::SameLine();
    ImGui::BeginDisabled(!a.loop);
    if (ImGui::Checkbox("Back and forth", &ping)) { saveUndo(); a.pingPong = ping; }
    ImGui::EndDisabled();
    if (ImGui::Checkbox("Play on start", &onStart)) { saveUndo(); a.playOnStart = onStart; }
    ImGui::DragFloat("Speed##anim", &a.speed, 0.01f, -10.0f, 10.0f); undoOnEdit();
}

void drawParticlesComponent(SceneNode& node, int nodeIdx) {
    (void)nodeIdx;
    ParticleEmitter& e = node.emitter;
    bool emitting = e.emitting, collide = e.collide, lit = e.lit;
    if (ImGui::Checkbox("Emit continuously", &emitting)) { saveUndo(); e.emitting = emitting; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off: particles appear only when a script calls emit(count)");
    ImGui::DragFloat("Rate", &e.rate, 0.5f, 0.0f, 2000.0f, "%.0f / s"); undoOnEdit();
    ImGui::DragFloat("Lifetime", &e.lifetime, 0.01f, 0.05f, 30.0f, "%.2f s"); undoOnEdit();
    ImGui::DragFloat("Speed##emit", &e.speed, 0.02f, 0.0f, 100.0f); undoOnEdit();
    float spreadDeg = glm::degrees(e.spread);
    if (ImGui::SliderFloat("Spread", &spreadDeg, 0.0f, 180.0f, "%.0f deg")) e.spread = glm::radians(spreadDeg);
    undoOnEdit();
    ImGui::DragFloat("Gravity", &e.gravityScale, 0.01f, -5.0f, 5.0f); undoOnEdit();
    ImGui::DragFloat("Start size", &e.sizeStart, 0.002f, 0.0f, 5.0f); undoOnEdit();
    ImGui::DragFloat("End size", &e.sizeEnd, 0.002f, 0.0f, 5.0f); undoOnEdit();
    ImGui::ColorEdit3("Start color", &e.colorStart.x); undoOnEdit();
    ImGui::ColorEdit3("End color", &e.colorEnd.x); undoOnEdit();
    if (ImGui::Checkbox("Bounce off geometry", &collide)) { saveUndo(); e.collide = collide; }
    if (ImGui::Checkbox("Lit by the sun", &lit)) { saveUndo(); e.lit = lit; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: smoke and dust, darker in shadow. Off: fire and sparks that glow on their own");
    if (ImGui::Checkbox("Preview in viewport", &particlePreview) && !particlePreview) particles::clear();
    ImGui::TextDisabled("Particles fly along the object's local +Y");
}

// Вызывается из оверлеев вьюпорта: частицы всех источников сцены, пока включён предпросмотр
void drawEditorParticlePreview(ImDrawList* dl) {
    if (!particlePreview || isPlayMode()) return;
    particles::update(std::min(ImGui::GetIO().DeltaTime, 0.05f), physics::gravity);
    drawParticlesOverlay(dl);
}

// Вкладка Timeline: дорожка на каждый анимированный объект сцены, общий курсор времени
void drawTimelinePanel() {
    advancePreview();
    int selected = selectedIndices.size() == 1 && selectedIndices[0] < (int)sceneGraph.size() ? selectedIndices[0] : -1;
    float longest = 1.0f;
    int animated = 0;
    for (const SceneNode& n : sceneGraph) if (n.animation.enabled) { animated++; longest = std::max(longest, anim::length(n.animation)); }

    if (ImGui::Button(previewing ? "Stop" : "Play")) togglePreview();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::DragFloat("##time", &cursor, 0.01f, 0.0f, 600.0f, "%.2f s")) { cursor = std::max(cursor, 0.0f); previewing = false; poseAll(); }
    undoOnEdit();
    ImGui::SameLine();
    ImGui::BeginDisabled(selected < 0);
    if (ImGui::Button("Key selected")) {
        // Первый ключ заодно добавляет объекту компонент Animation
        saveUndo();
        SceneNode& n = sceneGraph[selected];
        n.animation.enabled = true;
        anim::insertKey(n.animation, anim::keyFromNode(n, cursor));
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Store the selected object's transform at the current time");
    ImGui::SameLine();
    ImGui::TextDisabled(animated ? "drag on a track to scrub, click a row to select" : "select an object, pose it and press Key selected");

    ImGui::BeginChild("TimelineRows");
    float nameWidth = 150.0f;
    float span = longest * 1.1f;
    for (int i = 0; i < (int)sceneGraph.size(); i++) {
        SceneNode& node = sceneGraph[i];
        if (!node.animation.enabled) continue;
        ImGui::PushID(i);
        if (ImGui::Selectable(node.name.c_str(), i == selected, 0, ImVec2(nameWidth, 22.0f))) selectedIndices = { i };
        ImGui::SameLine();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        float width = std::max(ImGui::GetContentRegionAvail().x - 4.0f, 50.0f), height = 22.0f;
        ImGui::InvisibleButton("##row", ImVec2(width, height));
        auto timeToX = [&](float t) { return origin.x + 6.0f + (width - 12.0f) * std::clamp(t / span, 0.0f, 1.0f); };
        if (ImGui::IsItemActivated()) saveUndo();
        if (ImGui::IsItemActive()) {
            cursor = std::clamp((ImGui::GetIO().MousePos.x - origin.x - 6.0f) / std::max(width - 12.0f, 1.0f), 0.0f, 1.0f) * span;
            for (const Keyframe& k : node.animation.keys) if (std::fabs(timeToX(k.time) - ImGui::GetIO().MousePos.x) < 5.0f) cursor = k.time;
            previewing = false;
            poseAll();
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), i == selected ? IM_COL32(40, 46, 60, 255) : IM_COL32(30, 32, 40, 255), 3.0f);
        float length = anim::length(node.animation);
        if (node.animation.keys.size() >= 2) dl->AddRectFilled(ImVec2(timeToX(node.animation.keys.front().time), origin.y + height * 0.5f - 1.0f), ImVec2(timeToX(length), origin.y + height * 0.5f + 1.0f), IM_COL32(90, 96, 115, 255));
        for (const Keyframe& k : node.animation.keys) {
            ImVec2 c(timeToX(k.time), origin.y + height * 0.5f);
            bool here = std::fabs(k.time - cursor) < 1e-3f;
            dl->AddQuadFilled(ImVec2(c.x, c.y - 5), ImVec2(c.x + 5, c.y), ImVec2(c.x, c.y + 5), ImVec2(c.x - 5, c.y), here ? IM_COL32(255, 200, 60, 255) : IM_COL32(200, 205, 220, 255));
        }
        dl->AddLine(ImVec2(timeToX(cursor), origin.y), ImVec2(timeToX(cursor), origin.y + height), IM_COL32(90, 160, 255, 255), 2.0f);
        ImGui::PopID();
    }
    ImGui::EndChild();
}
