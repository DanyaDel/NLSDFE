// Script Editor: встроенная среда для скриптов NLS с проверкой при сохранении и справочником API,
// который строится из реестра движка — то есть всегда совпадает с тем, что реально доступно.
#include "Panels.h"
#include "Project.h"
#include "ScriptSystem.h"
#include "GraphEditor.h"
#include "imgui.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>

namespace fs = std::filesystem;

namespace {
    std::string openPath;                    // Относительно проекта: "scripts/door.nls"
    std::string buffer;
    bool modified = false;
    bool compiled = false;
    std::vector<nls::CompileError> errors;

    const char* TEMPLATE =
        "// Script variables show up in the Inspector\n"
        "var speed: float = 90.0\n"
        "\n"
        "on start() {\n"
        "    log(\"hello from \" + self.name)\n"
        "}\n"
        "\n"
        "on update(dt: float) {\n"
        "    self.rotation.y += speed * dt\n"
        "}\n";

    void check() {
        nls::registerEngineBindings();
        nls::Program program;
        compiled = nls::compile(buffer, program, errors);
    }

    bool isVisual(const std::string& rel) { return fs::path(rel).extension() == ".nlsg"; }

    void load(const std::string& rel) {
        openPath = rel;
        modified = false;
        if (isVisual(rel)) { grapheditor::open(project.path(rel)); return; }
        std::ifstream f(project.path(rel));
        std::stringstream ss;
        ss << f.rdbuf();
        buffer = ss.str();
        check();
    }

    void save() {
        if (openPath.empty()) return;
        bool good;
        if (isVisual(openPath)) {
            if (!grapheditor::save()) return;
            good = grapheditor::compiledOk();
        } else {
            std::ofstream f(project.path(openPath));
            f << buffer;
            modified = false;
            check();
            good = compiled;
        }
        showToast(good ? "Saved: " + fs::path(openPath).filename().string() : "Saved with errors");
    }

    std::string newScriptPath(const char* base, const char* ext) {
        std::error_code ec;
        for (int i = 0; ; i++) {
            std::string rel = std::string("scripts/") + base + (i ? "_" + std::to_string(i) : std::string()) + ext;
            if (!fs::exists(project.path(rel), ec)) return rel;
        }
    }

    int resizeCallback(ImGuiInputTextCallbackData* data) {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
            std::string* text = (std::string*)data->UserData;
            text->resize(data->BufTextLen);
            data->Buf = text->data();
        }
        return 0;
    }

    std::string signature(const nls::NativeDef& def, size_t firstParam, const std::string& shownName) {
        std::string s = shownName + "(";
        for (size_t i = firstParam; i < def.params.size(); i++) s += std::string(i > firstParam ? ", " : "") + nls::typeName(def.params[i]);
        s += ")";
        if (def.ret != nls::Type::Void) s += std::string(" -> ") + nls::typeName(def.ret);
        return s;
    }

    void apiReference() {
        const nls::Registry& reg = nls::registry();
        ImGui::TextDisabled("LANGUAGE");
        ImGui::TextWrapped("var x: float = 1.0   script variable\nlet y = 2            local\nfunc f(a: int) -> int { }\non start() / update(dt: float)\non fixedUpdate(dt: float)\non collide(other: node)\non triggerEnter(other: node) / triggerExit\nif / else, while, for i in 0..10\nTypes: float int bool vec3 string node\nself is the node the script is on.");
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Node properties", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto& p : reg.properties) ImGui::Text("%s: %s%s", p.name.c_str(), nls::typeName(p.type), p.set ? "" : " (read-only)");
        }
        // Функции группируются по модулю: "input.down" -> input
        std::map<std::string, std::vector<std::string>> groups;
        for (const auto& f : reg.functions) {
            size_t dot = f.name.find('.');
            if (dot == std::string::npos) groups[""].push_back(signature(f, 0, f.name));
            else if (f.name.compare(0, 5, "node.") == 0) groups["node"].push_back(signature(f, 1, f.name.substr(5)));
            else groups[f.name.substr(0, dot)].push_back(signature(f, 0, f.name));
        }
        if (ImGui::CollapsingHeader("Node methods", ImGuiTreeNodeFlags_DefaultOpen)) for (const auto& s : groups["node"]) ImGui::TextUnformatted(s.c_str());
        for (const auto& [module, list] : groups) {
            if (module.empty() || module == "node") continue;
            if (ImGui::CollapsingHeader(module.c_str())) for (const auto& s : list) ImGui::TextUnformatted(s.c_str());
        }
        if (ImGui::CollapsingHeader("Functions")) {
            ImGui::TextUnformatted("vec3(x, y, z), float(x), int(x), str(x)");
            for (const auto& s : groups[""]) ImGui::TextUnformatted(s.c_str());
        }
    }
}

void openScriptInEditor(const std::string& rel) {
    switchWorkspace(WS_SCRIPT);
    if (rel != openPath) load(rel);
}

void drawScriptWorkspace(GLFWwindow* window, const WorkArea& area) {
    (void)window;
    nls::registerEngineBindings();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoTitleBar;
    float listWidth = std::min(220.0f, area.w * 0.2f);
    float apiWidth = std::min(320.0f, area.w * 0.28f);
    float outputHeight = std::min(170.0f, area.h * 0.3f);
    float editorWidth = area.w - listWidth - apiWidth;

    // --- Список скриптов ---
    ImGui::SetNextWindowPos(ImVec2(area.x, area.y)); ImGui::SetNextWindowSize(ImVec2(listWidth, area.h));
    ImGui::Begin("ScriptList", nullptr, flags);
    ImGui::TextDisabled("SCRIPTS");
    if (ImGui::Button("New script", ImVec2(-1, 0))) {
        std::string rel = newScriptPath("script", ".nls");
        std::ofstream(project.path(rel)) << TEMPLATE;
        load(rel);
    }
    if (ImGui::Button("New visual script", ImVec2(-1, 0))) {
        std::string rel = newScriptPath("graph", ".nlsg");
        if (grapheditor::createDefault(project.path(rel))) load(rel);
    }
    ImGui::Separator();
    std::vector<std::string> files = project.list("scripts", ".nls");
    for (const std::string& f : project.list("scripts", ".nlsg")) files.push_back(f);
    std::sort(files.begin(), files.end());
    for (const std::string& file : files) {
        std::string rel = "scripts/" + file;
        std::string label = (isVisual(rel) ? "[graph] " : "") + file;
        if (ImGui::Selectable(label.c_str(), rel == openPath) && rel != openPath) load(rel);
    }
    ImGui::End();

    // --- Редактор ---
    ImGui::SetNextWindowPos(ImVec2(area.x + listWidth, area.y)); ImGui::SetNextWindowSize(ImVec2(editorWidth, area.h - outputHeight));
    ImGui::Begin("ScriptEditor", nullptr, flags);
    bool visual = !openPath.empty() && isVisual(openPath);
    bool dirty = visual ? grapheditor::modified() : modified;
    bool good = visual ? grapheditor::compiledOk() : compiled;
    const std::vector<nls::CompileError>& problems = visual ? grapheditor::problems() : errors;
    if (openPath.empty()) {
        ImGui::TextDisabled("Pick a script on the left or create a new one.");
    } else {
        ImGui::Text("%s%s", openPath.c_str(), dirty ? " *" : "");
        ImGui::SameLine();
        if (ImGui::SmallButton("Save (Ctrl+S)")) save();
        ImGui::SameLine();
        if (good) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "OK");
        else ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%d error(s)", (int)problems.size());

        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save();
        if (visual) {
            grapheditor::drawCanvas();
        } else if (ImGui::InputTextMultiline("##source", buffer.data(), buffer.capacity() + 1, ImVec2(-1, -1),
                                             ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize, resizeCallback, &buffer)) {
            modified = true;
        }
    }
    ImGui::End();

    // --- Ошибки и вывод ---
    ImGui::SetNextWindowPos(ImVec2(area.x + listWidth, area.y + area.h - outputHeight)); ImGui::SetNextWindowSize(ImVec2(editorWidth, outputHeight));
    ImGui::Begin("ScriptOutput", nullptr, flags);
    if (ImGui::BeginTabBar("OutputTabs")) {
        if (ImGui::BeginTabItem("Problems")) {
            if (openPath.empty()) ImGui::TextDisabled("No script open");
            else if (problems.empty()) ImGui::TextDisabled(!visual && modified ? "Save to check the script" : "No problems");
            for (const auto& e : problems) {
                if (visual) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", e.message.c_str());
                else ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%d:%d  %s", e.line, e.column, e.message.c_str());
            }
            ImGui::EndTabItem();
        }
        if (visual && ImGui::BeginTabItem("Generated NLS")) {
            // Граф — это обычный код на NLS; видно, во что превращается каждый узел
            const std::string& code = grapheditor::generatedSource();
            ImGui::InputTextMultiline("##generated", const_cast<char*>(code.c_str()), code.size() + 1, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Console")) {
            if (ImGui::SmallButton("Clear")) nls::scripts.console.clear();
            ImGui::BeginChild("ConsoleLines");
            for (const auto& line : nls::scripts.console) {
                if (line.compare(0, 5, "ERROR") == 0) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", line.c_str());
                else ImGui::TextUnformatted(line.c_str());
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();

    // --- Справочник ---
    ImGui::SetNextWindowPos(ImVec2(area.x + area.w - apiWidth, area.y)); ImGui::SetNextWindowSize(ImVec2(apiWidth, area.h));
    ImGui::Begin("ScriptApi", nullptr, flags);
    if (visual) grapheditor::drawSidebar();
    else apiReference();
    ImGui::End();
}
