#include "BlenderLink.h"
#include "BlenderExchange.h"
#include "Project.h"
#include "Scene.h"
#include "Settings.h"
#include "Renderer.h"
#include "EngineBridge.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace {
    std::vector<BlenderSession> sessions;
    double lastPoll = 0.0;

    std::string quoteArg(const std::string& s) {
#ifdef _WIN32
        return "\"" + s + "\"";
#else
        std::string out = "'";
        for (char c : s) { if (c == '\'') out += "'\\''"; else out += c; }
        return out + "'";
#endif
    }

    std::string safeName(std::string s) {
        for (char& c : s) if (!std::isalnum((unsigned char)c) && c != '_' && c != '-') c = '_';
        return s.empty() ? std::string("model") : s;
    }

    std::string addonPath() { return getExecutableDir() + "/nlsdfe_link.py"; }

    bool blenderAvailable() {
#ifdef _WIN32
        return true;   // Проверит сам запуск
#else
        const std::string& b = editorCfg.blenderPath;
        if (b.find('/') != std::string::npos) return fs::exists(b);
        return std::system(("command -v " + quoteArg(b) + " >/dev/null 2>&1").c_str()) == 0;
#endif
    }

    bool launch(const BlenderSession& s, bool background) {
        std::string args = " --python " + quoteArg(addonPath()) + " -- " + quoteArg(s.exchange);
        std::string cmd;
        if (background) {
            cmd = quoteArg(editorCfg.blenderPath) + " -b --factory-startup" + args + " --selftest";
#ifndef _WIN32
            cmd += " >/dev/null 2>&1";
#endif
        } else {
#ifdef _WIN32
            cmd = "start \"\" " + quoteArg(editorCfg.blenderPath) + args;
#else
            // Отдельный процесс: Blender живёт своей жизнью и не держит редактор
            cmd = "setsid " + quoteArg(editorCfg.blenderPath) + args + " >/dev/null 2>&1 &";
#endif
        }
        return std::system(cmd.c_str()) == 0;
    }

    // Выделенные SDF-объекты вместе с SDF-потомками
    std::vector<int> sdfNodes(const std::vector<int>& roots) {
        std::set<int> nodes;
        std::vector<int> stack = roots;
        while (!stack.empty()) {
            int idx = stack.back(); stack.pop_back();
            if (idx < 0 || idx >= (int)sceneGraph.size() || nodes.count(idx)) continue;
            if (isSdfNode(sceneGraph[idx]) && !sceneGraph[idx].isTrigger) nodes.insert(idx);
            for (int child : sceneGraph[idx].children) stack.push_back(child);
        }
        return std::vector<int>(nodes.begin(), nodes.end());
    }

    int selectedMesh() {
        if (selectedIndices.size() != 1) return -1;
        int idx = selectedIndices[0];
        if (idx < 0 || idx >= (int)sceneGraph.size()) return -1;
        int mi = sceneGraph[idx].meshIdx;
        return (mi >= 0 && mi < (int)meshCache.size() && !meshCache[mi].vertices.empty()) ? idx : -1;
    }

    // Новый меш из Blender: копия в проект под новым именем (кэш мешей привязан к пути)
    void applyMesh(BlenderSession& s) {
        int node = s.meshNode;
        if (node < 0 || node >= (int)sceneGraph.size() || sceneGraph[node].name != s.nodeNames[node]) {
            showToast("Blender: the mesh object is gone from the scene");
            return;
        }
        std::error_code ec;
        fs::create_directories(project.path("assets/meshes"), ec);
        std::string target;
        for (int i = 1; ; i++) {
            target = project.path("assets/meshes/" + safeName(sceneGraph[node].name) + "_v" + std::to_string(i) + ".obj");
            if (!fs::exists(target, ec)) break;
        }
        fs::copy_file(s.result, target, ec);
        if (ec) { showToast("Blender: cannot copy the mesh"); return; }
        int meshIdx = loadMeshFile(target);
        if (meshIdx < 0) { showToast("Blender: cannot load the mesh"); return; }
        saveUndo();
        sceneGraph[node].meshIdx = meshIdx;
        sceneGraph[node].meshPath = target;
        showToast("Mesh updated from Blender");
        spdlog::info("Blender: mesh {} replaced by {}", sceneGraph[node].name, target);
    }
}

bool canEditInBlender() {
    if (editingModel()) return !sdfNodes([] { std::vector<int> all(sceneGraph.size()); for (int i = 0; i < (int)all.size(); i++) all[i] = i; return all; }()).empty();
    return selectedMesh() >= 0 || !sdfNodes(selectedIndices).empty();
}

bool editInBlender(bool background) {
    if (!blenderAvailable()) { showToast("Blender not found: set its path in Edit > Preferences > Editor"); return false; }
    if (!fs::exists(addonPath())) { showToast("nlsdfe_link.py is missing next to the editor"); return false; }

    BlenderSession s;
    s.modelDocument = editingModel();
    std::string error;
    bool ok;
    int mesh = s.modelDocument ? -1 : selectedMesh();
    if (mesh >= 0) {
        s.name = sceneGraph[mesh].name;
        ok = writeMeshExchange(mesh, meshCache[sceneGraph[mesh].meshIdx].vertices, project.path("assets/blender/" + safeName(s.name)), s, &error);
    } else {
        std::vector<int> roots = selectedIndices;
        if (s.modelDocument) { roots.clear(); for (int i = 0; i < (int)sceneGraph.size(); i++) roots.push_back(i); }
        std::vector<int> nodes = sdfNodes(roots);
        s.name = s.modelDocument ? fs::path(currentLoadedFile).stem().string() : (nodes.empty() ? "model" : sceneGraph[nodes[0]].name);
        ok = writeSdfExchange(nodes, project.path("assets/blender/" + safeName(s.name)), s, &error);
    }
    if (!ok) { showToast("Blender: " + error); return false; }

    // Повторное открытие того же объекта заменяет старую сессию
    sessions.erase(std::remove_if(sessions.begin(), sessions.end(), [&](const BlenderSession& o) { return o.dir == s.dir; }), sessions.end());
    sessions.push_back(s);
    if (!launch(s, background)) { showToast("Blender: failed to start " + editorCfg.blenderPath); return false; }
    if (!background) showToast("Opened in Blender: save there (Ctrl+S) to send changes back");
    return true;
}

void pollBlender() {
    double now = ImGui::GetTime();
    if (sessions.empty() || now - lastPoll < 0.5) return;
    lastPoll = now;
    for (BlenderSession& s : sessions) {
        long long time = 0;
        if (!blenderResultChanged(s, &time)) continue;
        if (s.modelDocument != editingModel()) continue;   // Ждём, пока откроют нужный документ
        if (s.mesh) {
            s.resultTime = time;
            applyMesh(s);
        } else {
            std::string error;
            if (applySdfResult(s, &error)) { showToast("Updated from Blender: " + s.name); spdlog::info("Blender: applied {} ({} objects)", s.name, s.exported.size()); }
            else { s.resultTime = time; showToast("Blender: " + error); }
        }
    }
}

int blenderSessionCount() { return (int)sessions.size(); }
