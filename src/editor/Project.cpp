#include "Project.h"
#include "Panels.h"
#include "Scene.h"
#include "Settings.h"
#include "ScriptSystem.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

Project project;
Workspace currentWorkspace = WS_SCENE;

std::string Project::path(const std::string& rel) const {
    if (rel.empty()) return root;
    fs::path p(rel);
    return p.is_absolute() ? rel : (fs::path(root) / p).string();
}

std::string Project::relative(const std::string& absolute) const {
    std::error_code ec;
    fs::path rel = fs::relative(absolute, root, ec);
    if (ec || rel.empty() || *rel.begin() == "..") return absolute;
    return rel.generic_string();
}

std::vector<std::string> Project::list(const std::string& subdir, const std::string& extension) const {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(path(subdir), ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == extension) out.push_back(entry.path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool openProject(const std::string& directory) {
    std::error_code ec;
    fs::path root = fs::absolute(directory, ec);
    for (const char* sub : { "assets", "assets/sounds", "assets/prefabs", "scenes", "scripts", "ui" }) fs::create_directories(root / sub, ec);
    if (!fs::is_directory(root, ec)) { spdlog::error("Cannot open project folder {}", root.string()); return false; }

    project = Project();
    project.root = root.string();
    project.name = root.filename().string();
    std::ifstream f(root / "project.nlsproj");
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "name") project.name = v;
        else if (k == "lastExportDir") project.lastExportDir = v;
        else if (k == "exportPreset") { try { project.exportPreset = std::stoi(v); } catch (...) {} }
    }
    nls::scripts.rootDir = project.root;
    editorCfg.projectDir = project.root;
    spdlog::info("Project: {}", project.root);
    return true;
}

void saveProject() {
    if (project.root.empty()) return;
    std::ofstream f(fs::path(project.root) / "project.nlsproj");
    f << "name=" << project.name << "\n";
    f << "exportPreset=" << project.exportPreset << "\n";
    f << "lastExportDir=" << project.lastExportDir << "\n";
}

std::string importIntoProject(const std::string& file, const std::string& subdir) {
    std::error_code ec;
    fs::path source(file);
    fs::path target = fs::path(project.path(subdir)) / source.filename();
    if (fs::equivalent(source, target, ec)) return target.string();
    // Не затираем молча чужой файл с тем же именем
    for (int i = 1; fs::exists(target, ec) && i < 1000; i++) {
        target = fs::path(project.path(subdir)) / (source.stem().string() + "_" + std::to_string(i) + source.extension().string());
    }
    fs::copy_file(source, target, ec);
    if (ec) { spdlog::error("Import failed: {}", ec.message()); return ""; }
    return target.string();
}

const char* workspaceName(Workspace ws) {
    static const char* names[] = { "Model Editor", "Scene Editor", "UV Editor", "Script Editor", "UI Editor" };
    return names[std::clamp((int)ws, 0, (int)WS_COUNT - 1)];
}

// =================================================================
// --- Документы ---
// =================================================================
namespace {
    struct Document {
        std::vector<SceneNode> graph;
        std::vector<int> selection;
        std::vector<UndoState> undo, redo;
        WorldSettings world;
        CameraState camera;
        std::string file = "Untitled";
        bool created = false;
    };
    Document parked;            // Документ, который сейчас не на экране
    bool modelActive = false;   // Что лежит в sceneGraph: модель или сцена

    void makeBlankModel() {
        resetScene();
        sceneGraph.clear();
        SceneNode n;
        n.name = "Shape";
        n.gpuData.position.w = (float)SDF_SPHERE;
        n.gpuData.params = vec4(1.0f, 0, 0.1f, 0);
        n.gpuData.color = vec4(0.8f, 0.8f, 0.8f, 0.5f);
        sceneGraph.push_back(n);
        cam = CameraState();
        cam.target = vec3(0.0f);
        cam.radius = 5.0f;
    }

    void swapDocuments() {
        Document current;
        current.graph = std::move(sceneGraph);
        current.selection = std::move(selectedIndices);
        current.undo = std::move(undoStack);
        current.redo = std::move(redoStack);
        current.world = world;
        current.camera = cam;
        current.file = currentLoadedFile;
        current.created = true;

        modelActive = !modelActive;
        if (parked.created) {
            sceneGraph = std::move(parked.graph);
            selectedIndices = std::move(parked.selection);
            undoStack = std::move(parked.undo);
            redoStack = std::move(parked.redo);
            world = parked.world;
            cam = parked.camera;
            currentLoadedFile = parked.file;
        } else if (modelActive) {
            makeBlankModel();
        } else {
            resetScene();
        }
        parked = std::move(current);
        // Зажатые кнопки мыши не должны переехать в другой документ
        cam.leftMouse = cam.rightMouse = cam.middleMouse = false;
    }
}

bool editingModel() { return modelActive; }

void switchWorkspace(Workspace ws) {
    bool needModel = ws == WS_MODEL;
    if (needModel != modelActive) swapDocuments();
    if (ws != currentWorkspace) stopEditorPreview();
    currentWorkspace = ws;
}

void newDocument() {
    if (modelActive) makeBlankModel();
    else resetScene();
}

std::string documentDirectory() {
    return project.path(modelActive ? "assets" : "scenes");
}
