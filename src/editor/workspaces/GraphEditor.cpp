#include "GraphEditor.h"
#include "ScriptSystem.h"
#include "Common.h"
#include "imgui.h"
#include "imnodes.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <sstream>

using namespace nls;
using namespace nls::visual;

namespace grapheditor {

namespace {
    Graph graph;
    std::string path;
    bool dirty = false;           // Есть несохранённые правки
    bool recheck = true;          // Граф изменился — пересобрать код
    bool placeNodes = false;      // Позиции из файла ещё не переданы холсту
    bool ok = true;
    std::vector<CompileError> errors;
    std::vector<int> errorNodes;
    std::string source;
    std::vector<NodeDef> defs;
    char search[64] = "";
    ImVec2 pendingPosition(0, 0);

    // Атрибут imnodes = узел * 128 + (выход ? 64 : 0) + номер пина
    int attrId(int node, bool output, int pin) { return node * 128 + (output ? 64 : 0) + pin; }
    void decodeAttr(int attr, int& node, bool& output, int& pin) { node = attr / 128; output = (attr % 128) >= 64; pin = attr % 64; }

    void ensureContext() {
        static bool created = false;
        if (created) return;
        ImNodes::CreateContext();
        ImNodes::StyleColorsDark();
        ImNodes::GetIO().LinkDetachWithModifierClick.Modifier = &ImGui::GetIO().KeyCtrl;   // Ctrl+клик по связи — оторвать
        created = true;
    }

    ImU32 typeColor(Type t) {
        switch (t) {
            case Type::Void: return IM_COL32(230, 230, 230, 255);
            case Type::Float: return IM_COL32(120, 220, 120, 255);
            case Type::Int: return IM_COL32(80, 200, 200, 255);
            case Type::Bool: return IM_COL32(220, 80, 80, 255);
            case Type::Vec3: return IM_COL32(240, 200, 60, 255);
            case Type::String: return IM_COL32(230, 110, 220, 255);
            case Type::Node: return IM_COL32(90, 150, 255, 255);
        }
        return IM_COL32(200, 200, 200, 255);
    }

    ImU32 categoryColor(const std::string& category) {
        if (category == "Events") return IM_COL32(160, 40, 40, 255);
        if (category == "Flow") return IM_COL32(90, 90, 90, 255);
        if (category == "Properties" || category == "Node") return IM_COL32(40, 80, 150, 255);
        if (category == "Variables") return IM_COL32(120, 60, 140, 255);
        if (category == "Values") return IM_COL32(60, 110, 60, 255);
        return IM_COL32(50, 110, 110, 255);
    }

    void touch() { dirty = true; recheck = true; }

    void refresh() {
        defs = catalog(graph);
        if (!recheck) return;
        recheck = false;
        Program program;
        ok = compileGraph(graph, program, errors, &errorNodes);
        source = generate(graph).source;
    }

    // Поле ввода константы на несоединённом входе
    void constantEditor(GraphNode& n, int pin, const PinDef& def) {
        std::string& value = n.constants[pin];
        ImGui::PushID(pin);
        ImGui::SameLine();
        switch (def.type) {
            case Type::Float: {
                float f = (float)std::atof(value.c_str());
                ImGui::SetNextItemWidth(70.0f);
                if (ImGui::DragFloat("##c", &f, 0.05f)) { value = formatFloat(f); touch(); }
                break;
            }
            case Type::Int: {
                int i = std::atoi(value.c_str());
                ImGui::SetNextItemWidth(60.0f);
                if (ImGui::DragInt("##c", &i)) { value = std::to_string(i); touch(); }
                break;
            }
            case Type::Bool: {
                bool b = value == "true";
                if (ImGui::Checkbox("##c", &b)) { value = b ? "true" : "false"; touch(); }
                break;
            }
            case Type::Vec3: {
                float v[3] = {0, 0, 0};
                std::istringstream ss(value);
                ss >> v[0] >> v[1] >> v[2];
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::DragFloat3("##c", v, 0.05f)) { value = formatFloat(v[0]) + " " + formatFloat(v[1]) + " " + formatFloat(v[2]); touch(); }
                break;
            }
            case Type::String: {
                char buf[256];
                strncpy(buf, value.c_str(), sizeof(buf)); buf[sizeof(buf) - 1] = 0;
                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::InputText("##c", buf, sizeof(buf))) { value = buf; touch(); }
                break;
            }
            case Type::Node:
                ImGui::TextDisabled(def.name == "Target" ? "self" : "none");
                break;
            default: break;
        }
        ImGui::PopID();
    }

    void drawNode(GraphNode& n) {
        const NodeDef* d = findDef(defs, n.type);
        bool broken = std::find(errorNodes.begin(), errorNodes.end(), n.id) != errorNodes.end();
        ImU32 title = d ? categoryColor(d->category) : IM_COL32(120, 0, 0, 255);
        if (broken) title = IM_COL32(200, 40, 40, 255);
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, title);
        ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, title);
        ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, title);

        ImNodes::BeginNode(n.id);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(d ? d->title.c_str() : ("? " + n.type).c_str());
        ImNodes::EndNodeTitleBar();
        if (!d) { ImNodes::EndNode(); ImNodes::PopColorStyle(); ImNodes::PopColorStyle(); ImNodes::PopColorStyle(); return; }
        if (n.constants.size() < d->inputs.size()) n.constants.resize(d->inputs.size());

        // Входы слева, выходы справа — по строкам
        size_t rows = std::max(d->inputs.size(), d->outputs.size());
        for (size_t r = 0; r < rows; r++) {
            ImGui::BeginGroup();
            if (r < d->inputs.size()) {
                const PinDef& pin = d->inputs[r];
                ImNodes::PushColorStyle(ImNodesCol_Pin, typeColor(pin.type));
                ImNodes::BeginInputAttribute(attrId(n.id, false, (int)r), pin.isExec() ? ImNodesPinShape_TriangleFilled : ImNodesPinShape_CircleFilled);
                ImGui::TextUnformatted(pin.isExec() ? " " : pin.name.c_str());
                if (!pin.isExec() && !graph.linkInto(n.id, (int)r)) constantEditor(n, (int)r, pin);
                ImNodes::EndInputAttribute();
                ImNodes::PopColorStyle();
            } else ImGui::Dummy(ImVec2(1, ImGui::GetTextLineHeight()));
            ImGui::EndGroup();

            if (r < d->outputs.size()) {
                const PinDef& pin = d->outputs[r];
                ImGui::SameLine();
                ImNodes::PushColorStyle(ImNodesCol_Pin, typeColor(pin.type));
                ImNodes::BeginOutputAttribute(attrId(n.id, true, (int)r), pin.isExec() ? ImNodesPinShape_TriangleFilled : ImNodesPinShape_CircleFilled);
                ImGui::Indent(30.0f);
                ImGui::TextUnformatted(pin.name.empty() ? (pin.isExec() ? ">" : typeName(pin.type)) : pin.name.c_str());
                ImNodes::EndOutputAttribute();
                ImNodes::PopColorStyle();
            }
        }
        ImNodes::EndNode();
        ImNodes::PopColorStyle(); ImNodes::PopColorStyle(); ImNodes::PopColorStyle();
    }

    bool matches(const NodeDef& d) {
        if (!search[0]) return true;
        std::string hay = d.title + " " + d.category + " " + d.target;
        std::string needle = search;
        std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
        std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
        return hay.find(needle) != std::string::npos;
    }

    // Список узлов по категориям; возвращает выбранный тип
    std::string palette(bool compact) {
        std::string chosen;
        std::map<std::string, std::vector<const NodeDef*>> groups;
        std::vector<std::string> order = { "Events", "Flow", "Variables", "Properties", "Node", "Values", "Math", "Vector", "Logic", "Text" };
        for (const NodeDef& d : defs) {
            if (!matches(d)) continue;
            if (std::find(order.begin(), order.end(), d.category) == order.end()) order.push_back(d.category);
            groups[d.category].push_back(&d);
        }
        for (const std::string& category : order) {
            auto it = groups.find(category);
            if (it == groups.end()) continue;
            ImGuiTreeNodeFlags flags = (search[0] || category == "Events") ? ImGuiTreeNodeFlags_DefaultOpen : 0;
            if (compact && search[0]) flags |= ImGuiTreeNodeFlags_DefaultOpen;
            if (!ImGui::TreeNodeEx(category.c_str(), flags)) continue;
            for (const NodeDef* d : it->second) {
                if (ImGui::Selectable(d->title.c_str())) chosen = d->id;
                if (ImGui::IsItemHovered() && !d->target.empty()) ImGui::SetTooltip("%s", d->id.c_str());
            }
            ImGui::TreePop();
        }
        return chosen;
    }

    void addNodeAt(const std::string& type, ImVec2 screen) {
        int id = graph.addNode(type, 0, 0);
        if (!id) return;
        ImNodes::SetNodeScreenSpacePos(id, screen);
        ImVec2 grid = ImNodes::GetNodeGridSpacePos(id);
        graph.find(id)->x = grid.x;
        graph.find(id)->y = grid.y;
        touch();
    }

    void renameVariable(const std::string& from, const std::string& to) {
        for (GraphNode& n : graph.nodes) {
            if (n.type == variableNodeId(false, from)) n.type = variableNodeId(false, to);
            if (n.type == variableNodeId(true, from)) n.type = variableNodeId(true, to);
        }
    }
}

bool open(const std::string& fullPath) {
    ensureContext();
    std::string error;
    Graph loaded;
    if (!loadGraph(fullPath, loaded, &error)) { showToast("Cannot open graph: " + error); return false; }
    graph = loaded;
    path = fullPath;
    dirty = false;
    recheck = true;
    placeNodes = true;
    refresh();
    return true;
}

bool save() {
    if (path.empty()) return false;
    // Позиции берутся с холста: их меняют перетаскиванием
    for (GraphNode& n : graph.nodes) {
        ImVec2 p = ImNodes::GetNodeGridSpacePos(n.id);
        n.x = p.x; n.y = p.y;
    }
    if (!saveGraph(graph, path)) { showToast("Cannot write " + path); return false; }
    dirty = false;
    recheck = true;
    refresh();
    return true;
}

bool createDefault(const std::string& fullPath) {
    Graph g;
    g.vars.push_back({"speed", Type::Float, "90"});
    int start = g.addNode("event.start", 40, 40);
    int log = g.addNode("call.log", 300, 40);
    g.find(log)->constants[1] = "hello from a visual script";
    g.connect(start, 0, log, 0);

    int update = g.addNode("event.update", 40, 220);
    int set = g.addNode("set.rotation", 640, 220);
    int get = g.addNode("get.rotation", 220, 330);
    int speed = g.addNode("var.get.speed", 40, 460);
    int mul = g.addNode("op.mul_f", 220, 460);
    int make = g.addNode("op.make_v", 400, 440);
    int add = g.addNode("op.add_v", 460, 330);
    g.connect(update, 0, set, 0);
    g.connect(speed, 0, mul, 0);
    g.connect(update, 1, mul, 1);
    g.connect(mul, 0, make, 1);
    g.connect(get, 0, add, 0);
    g.connect(make, 0, add, 1);
    g.connect(add, 0, set, 2);
    return saveGraph(g, fullPath);
}

bool modified() { return dirty; }
bool compiledOk() { return ok; }
const std::vector<CompileError>& problems() { return errors; }
const std::string& generatedSource() { return source; }

void drawCanvas() {
    ensureContext();
    registerEngineBindings();
    refresh();

    if (placeNodes) {
        for (const GraphNode& n : graph.nodes) ImNodes::SetNodeGridSpacePos(n.id, ImVec2(n.x, n.y));
        placeNodes = false;
    }

    ImNodes::BeginNodeEditor();
    for (GraphNode& n : graph.nodes) drawNode(n);
    for (size_t i = 0; i < graph.links.size(); i++) {
        const GraphLink& l = graph.links[i];
        const GraphNode* from = graph.find(l.fromNode);
        const NodeDef* d = from ? findDef(defs, from->type) : nullptr;
        Type t = (d && l.fromPin < (int)d->outputs.size()) ? d->outputs[l.fromPin].type : Type::Void;
        ImNodes::PushColorStyle(ImNodesCol_Link, typeColor(t));
        ImNodes::Link((int)i + 1, attrId(l.fromNode, true, l.fromPin), attrId(l.toNode, false, l.toPin));
        ImNodes::PopColorStyle();
    }
    ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_BottomRight);
    bool canvasHovered = ImNodes::IsEditorHovered();
    ImNodes::EndNodeEditor();

    // Новая связь: тянуть можно и от входа, и от выхода
    int a = 0, b = 0;
    if (ImNodes::IsLinkCreated(&a, &b)) {
        int na, pa, nb, pb;
        bool oa, ob;
        decodeAttr(a, na, oa, pa);
        decodeAttr(b, nb, ob, pb);
        if (!oa) { std::swap(na, nb); std::swap(pa, pb); std::swap(oa, ob); }
        std::string why;
        if (oa && !ob && graph.connect(na, pa, nb, pb, &why)) touch();
        else if (!why.empty()) showToast("Cannot connect: " + why);
    }
    int destroyed = 0;
    if (ImNodes::IsLinkDestroyed(&destroyed) && destroyed >= 1 && destroyed <= (int)graph.links.size()) {
        graph.links.erase(graph.links.begin() + (destroyed - 1));
        touch();
    }

    // Удаление выделенного
    if (canvasHovered && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_X, false)) && !ImGui::GetIO().WantTextInput) {
        int linkCount = ImNodes::NumSelectedLinks();
        if (linkCount > 0) {
            std::vector<int> ids(linkCount);
            ImNodes::GetSelectedLinks(ids.data());
            std::sort(ids.rbegin(), ids.rend());
            for (int id : ids) if (id >= 1 && id <= (int)graph.links.size()) graph.links.erase(graph.links.begin() + (id - 1));
            ImNodes::ClearLinkSelection();
        }
        int nodeCount = ImNodes::NumSelectedNodes();
        if (nodeCount > 0) {
            std::vector<int> ids(nodeCount);
            ImNodes::GetSelectedNodes(ids.data());
            for (int id : ids) graph.removeNode(id);
            ImNodes::ClearNodeSelection();
        }
        if (linkCount > 0 || nodeCount > 0) touch();
    }

    // Правый клик по пустому месту — добавить узел туда
    if (canvasHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        int hovered = 0;
        if (!ImNodes::IsNodeHovered(&hovered)) {
            pendingPosition = ImGui::GetMousePos();
            search[0] = 0;
            ImGui::OpenPopup("AddGraphNode");
        }
    }
    if (ImGui::BeginPopup("AddGraphNode")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##search", "Search nodes...", search, sizeof(search));
        ImGui::BeginChild("AddList", ImVec2(260, 320));
        std::string chosen = palette(true);
        ImGui::EndChild();
        if (!chosen.empty()) { addNodeAt(chosen, pendingPosition); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    ImVec2 min = ImGui::GetItemRectMin();
    ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + 8, min.y + 6), IM_COL32(200, 200, 200, 160),
        "Right click: add node   Drag pins: connect   Delete / X: remove   Ctrl+click link: detach");
}

void drawSidebar() {
    ensureContext();
    refresh();

    ImGui::TextDisabled("VARIABLES");
    ImGui::TextDisabled("Shown in the Inspector of the object");
    int remove = -1;
    for (int i = 0; i < (int)graph.vars.size(); i++) {
        GraphVar& v = graph.vars[i];
        ImGui::PushID(i);
        char name[64];
        strncpy(name, v.name.c_str(), sizeof(name)); name[sizeof(name) - 1] = 0;
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::InputText("##name", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::string next = name;
            bool taken = false;
            for (const GraphVar& other : graph.vars) if (&other != &v && other.name == next) taken = true;
            if (isIdentifier(next) && !taken) { renameVariable(v.name, next); v.name = next; touch(); }
            else showToast("Name not allowed: " + next);
        }
        ImGui::SameLine();
        const Type types[] = { Type::Float, Type::Int, Type::Bool, Type::Vec3, Type::String, Type::Node };
        ImGui::SetNextItemWidth(70.0f);
        if (ImGui::BeginCombo("##type", typeName(v.type))) {
            for (Type t : types) {
                if (ImGui::Selectable(typeName(t), t == v.type) && t != v.type) {
                    v.type = t;
                    v.initial = defaultConstant(t);
                    // Связи с пинами старого типа больше не подходят
                    for (GraphNode& n : graph.nodes) {
                        if (n.type != variableNodeId(false, v.name) && n.type != variableNodeId(true, v.name)) continue;
                        int id = n.id;
                        graph.links.erase(std::remove_if(graph.links.begin(), graph.links.end(), [&](const GraphLink& l) { return l.fromNode == id || l.toNode == id; }), graph.links.end());
                        n.constants.assign(n.constants.size(), defaultConstant(t));
                    }
                    touch();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) remove = i;
        if (v.type != Type::Node) {
            GraphNode fake;
            fake.constants = { v.initial };
            ImGui::TextDisabled("  default");
            constantEditor(fake, 0, PinDef{"", v.type});
            if (fake.constants[0] != v.initial) { v.initial = fake.constants[0]; touch(); }
        }
        ImGui::PopID();
    }
    if (remove >= 0) {
        std::string name = graph.vars[remove].name;
        std::vector<int> users;
        for (const GraphNode& n : graph.nodes) if (n.type == variableNodeId(false, name) || n.type == variableNodeId(true, name)) users.push_back(n.id);
        for (int id : users) graph.removeNode(id);
        graph.vars.erase(graph.vars.begin() + remove);
        touch();
    }
    if (ImGui::Button("+ Variable")) {
        std::string name;
        for (int i = 1; ; i++) {
            name = "value" + std::to_string(i);
            bool taken = false;
            for (const GraphVar& v : graph.vars) if (v.name == name) taken = true;
            if (!taken) break;
        }
        graph.vars.push_back({name, Type::Float, "0"});
        touch();
    }

    ImGui::Separator();
    ImGui::TextDisabled("NODES");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##palettesearch", "Search...", search, sizeof(search));
    ImGui::BeginChild("Palette");
    std::string chosen = palette(false);
    ImGui::EndChild();
    if (!chosen.empty()) {
        // Узел из палитры появляется в левом верхнем углу видимой части холста
        ImVec2 panning = ImNodes::EditorContextGetPanning();
        int id = graph.addNode(chosen, -panning.x + 60.0f, -panning.y + 60.0f);
        if (id) { ImNodes::SetNodeGridSpacePos(id, ImVec2(graph.find(id)->x, graph.find(id)->y)); touch(); }
    }
}

}
