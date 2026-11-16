#include "VisualScript.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace nls::visual {

// =================================================================
// --- Каталог узлов ---
// =================================================================
namespace {
    const PinDef EXEC{"", Type::Void};

    // Имена параметров для функций реестра (в самом реестре только типы)
    const std::map<std::string, std::vector<std::string>> paramNames = {
        {"log", {"Text"}}, {"toast", {"Text"}},
        {"input.down", {"Key"}}, {"input.pressed", {"Key"}},
        {"scene.find", {"Name"}}, {"scene.get", {"Index"}}, {"scene.spawn", {"Template"}},
        {"node.applyImpulse", {"Target", "Impulse"}}, {"node.wake", {"Target"}}, {"node.destroy", {"Target"}},
        {"node.distanceTo", {"Target", "Other"}}, {"node.playSound", {"Target"}}, {"node.stopSound", {"Target"}},
        {"audio.play", {"File", "Volume"}},
        {"physics.raycast", {"Origin", "Direction", "Max Distance"}}, {"physics.raycastDistance", {"Origin", "Direction", "Max Distance"}},
        {"physics.setGravity", {"Gravity"}},
        {"sin", {"X"}}, {"cos", {"X"}}, {"tan", {"X"}}, {"sqrt", {"X"}}, {"abs", {"X"}}, {"floor", {"X"}}, {"ceil", {"X"}},
        {"round", {"X"}}, {"sign", {"X"}}, {"radians", {"Degrees"}}, {"degrees", {"Radians"}},
        {"atan2", {"Y", "X"}}, {"pow", {"Base", "Exponent"}}, {"min", {"A", "B"}}, {"max", {"A", "B"}},
        {"clamp", {"Value", "Min", "Max"}}, {"lerp", {"A", "B", "T"}}, {"randomRange", {"Min", "Max"}},
        {"length", {"Vector"}}, {"normalize", {"Vector"}}, {"distance", {"A", "B"}}, {"dot", {"A", "B"}},
        {"cross", {"A", "B"}}, {"lerpv", {"A", "B", "T"}},
    };
    // Функции с результатом, которые что-то меняют: им нужен порядок выполнения
    const std::set<std::string> impureWithResult = { "scene.spawn" };

    std::string paramName(const std::string& fn, size_t i, Type t) {
        auto it = paramNames.find(fn);
        if (it != paramNames.end() && i < it->second.size()) return it->second[i];
        std::string n = typeName(t);
        n[0] = (char)std::toupper((unsigned char)n[0]);
        return n;
    }

    NodeDef pure(const char* id, const char* title, const char* category, std::vector<PinDef> in, std::vector<PinDef> out, const char* pattern) {
        NodeDef d;
        d.id = id; d.title = title; d.category = category; d.kind = NodeKind::Pure;
        d.inputs = std::move(in); d.outputs = std::move(out); d.pattern = pattern;
        return d;
    }

    struct EventInfo { const char* name; const char* title; std::vector<PinDef> params; };
    const std::vector<EventInfo> events = {
        {"start", "On Start", {}},
        {"update", "On Update", {{"dt", Type::Float}}},
        {"fixedUpdate", "On Fixed Update", {{"dt", Type::Float}}},
        {"collide", "On Collide", {{"other", Type::Node}}},
        {"triggerEnter", "On Trigger Enter", {{"other", Type::Node}}},
        {"triggerExit", "On Trigger Exit", {{"other", Type::Node}}},
        {"message", "On Message", {{"name", Type::String}, {"value", Type::Float}}},
    };

    std::vector<NodeDef> builtins() {
        std::vector<NodeDef> out;
        for (const auto& e : events) {
            NodeDef d;
            d.id = std::string("event.") + e.name; d.title = e.title; d.category = "Events"; d.kind = NodeKind::Event; d.target = e.name;
            d.outputs.push_back(EXEC);
            for (const auto& p : e.params) d.outputs.push_back(p);
            out.push_back(d);
        }

        NodeDef branch; branch.id = "flow.branch"; branch.title = "Branch"; branch.category = "Flow"; branch.kind = NodeKind::Branch;
        branch.inputs = { EXEC, {"Condition", Type::Bool} }; branch.outputs = { {"True", Type::Void}, {"False", Type::Void} };
        out.push_back(branch);
        NodeDef seq; seq.id = "flow.sequence"; seq.title = "Sequence"; seq.category = "Flow"; seq.kind = NodeKind::Sequence;
        seq.inputs = { EXEC }; seq.outputs = { {"Then 1", Type::Void}, {"Then 2", Type::Void}, {"Then 3", Type::Void} };
        out.push_back(seq);
        NodeDef loop; loop.id = "flow.for"; loop.title = "For Loop"; loop.category = "Flow"; loop.kind = NodeKind::ForLoop;
        loop.inputs = { EXEC, {"First", Type::Int}, {"Last (excl.)", Type::Int} };
        loop.outputs = { {"Body", Type::Void}, {"Index", Type::Int}, {"Completed", Type::Void} };
        out.push_back(loop);

        NodeDef self; self.id = "node.self"; self.title = "Self"; self.category = "Node"; self.kind = NodeKind::Self;
        self.outputs = { {"Self", Type::Node} };
        out.push_back(self);

        const Type F = Type::Float, I = Type::Int, B = Type::Bool, V = Type::Vec3, S = Type::String, N = Type::Node;
        // Литералы: значение вводится прямо в узле
        out.push_back(pure("lit.float", "Float", "Values", {{"", F}}, {{"Value", F}}, "$0"));
        out.push_back(pure("lit.int", "Int", "Values", {{"", I}}, {{"Value", I}}, "$0"));
        out.push_back(pure("lit.bool", "Bool", "Values", {{"", B}}, {{"Value", B}}, "$0"));
        out.push_back(pure("lit.string", "String", "Values", {{"", S}}, {{"Value", S}}, "$0"));
        out.push_back(pure("lit.vec3", "Vec3", "Values", {{"", V}}, {{"Value", V}}, "$0"));
        // Операторы
        out.push_back(pure("op.add_f", "Add", "Math", {{"A", F}, {"B", F}}, {{"", F}}, "($0 + $1)"));
        out.push_back(pure("op.sub_f", "Subtract", "Math", {{"A", F}, {"B", F}}, {{"", F}}, "($0 - $1)"));
        out.push_back(pure("op.mul_f", "Multiply", "Math", {{"A", F}, {"B", F}}, {{"", F}}, "($0 * $1)"));
        out.push_back(pure("op.div_f", "Divide", "Math", {{"A", F}, {"B", F}}, {{"", F}}, "($0 / $1)"));
        out.push_back(pure("op.add_i", "Add (int)", "Math", {{"A", I}, {"B", I}}, {{"", I}}, "($0 + $1)"));
        out.push_back(pure("op.add_v", "Add Vectors", "Vector", {{"A", V}, {"B", V}}, {{"", V}}, "($0 + $1)"));
        out.push_back(pure("op.sub_v", "Subtract Vectors", "Vector", {{"A", V}, {"B", V}}, {{"", V}}, "($0 - $1)"));
        out.push_back(pure("op.scale_v", "Scale Vector", "Vector", {{"Vector", V}, {"Factor", F}}, {{"", V}}, "($0 * $1)"));
        out.push_back(pure("op.make_v", "Make Vec3", "Vector", {{"X", F}, {"Y", F}, {"Z", F}}, {{"", V}}, "vec3($0, $1, $2)"));
        out.push_back(pure("op.break_v", "Break Vec3", "Vector", {{"Vector", V}}, {{"X", F}, {"Y", F}, {"Z", F}}, "($0).x|($0).y|($0).z"));
        out.push_back(pure("op.lt", "Less", "Logic", {{"A", F}, {"B", F}}, {{"", B}}, "($0 < $1)"));
        out.push_back(pure("op.gt", "Greater", "Logic", {{"A", F}, {"B", F}}, {{"", B}}, "($0 > $1)"));
        out.push_back(pure("op.le", "Less or Equal", "Logic", {{"A", F}, {"B", F}}, {{"", B}}, "($0 <= $1)"));
        out.push_back(pure("op.ge", "Greater or Equal", "Logic", {{"A", F}, {"B", F}}, {{"", B}}, "($0 >= $1)"));
        out.push_back(pure("op.eq_f", "Equal", "Logic", {{"A", F}, {"B", F}}, {{"", B}}, "($0 == $1)"));
        out.push_back(pure("op.eq_i", "Equal (int)", "Logic", {{"A", I}, {"B", I}}, {{"", B}}, "($0 == $1)"));
        out.push_back(pure("op.eq_s", "Equal (string)", "Logic", {{"A", S}, {"B", S}}, {{"", B}}, "($0 == $1)"));
        out.push_back(pure("op.and", "And", "Logic", {{"A", B}, {"B", B}}, {{"", B}}, "($0 and $1)"));
        out.push_back(pure("op.or", "Or", "Logic", {{"A", B}, {"B", B}}, {{"", B}}, "($0 or $1)"));
        out.push_back(pure("op.not", "Not", "Logic", {{"", B}}, {{"", B}}, "(not $0)"));
        out.push_back(pure("op.valid", "Is Valid", "Node", {{"Node", N}}, {{"", B}}, "($0 != null)"));
        out.push_back(pure("op.same", "Same Node", "Node", {{"A", N}, {"B", N}}, {{"", B}}, "($0 == $1)"));
        out.push_back(pure("op.concat", "Join Text", "Text", {{"A", S}, {"B", S}}, {{"", S}}, "($0 + $1)"));
        out.push_back(pure("op.str_f", "Float to Text", "Text", {{"", F}}, {{"", S}}, "str($0)"));
        out.push_back(pure("op.str_i", "Int to Text", "Text", {{"", I}}, {{"", S}}, "str($0)"));
        out.push_back(pure("op.str_b", "Bool to Text", "Text", {{"", B}}, {{"", S}}, "str($0)"));
        out.push_back(pure("op.str_v", "Vec3 to Text", "Text", {{"", V}}, {{"", S}}, "str($0)"));
        out.push_back(pure("op.str_n", "Node to Text", "Text", {{"", N}}, {{"", S}}, "str($0)"));
        out.push_back(pure("op.to_int", "Float to Int", "Math", {{"", F}}, {{"", I}}, "int($0)"));
        out.push_back(pure("op.to_float", "Int to Float", "Math", {{"", I}}, {{"", F}}, "float($0)"));
        return out;
    }

    std::string capitalize(std::string s) { if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]); return s; }
}

std::string variableNodeId(bool set, const std::string& name) { return std::string(set ? "var.set." : "var.get.") + name; }

std::string defaultConstant(Type type) {
    switch (type) {
        case Type::Float: case Type::Int: return "0";
        case Type::Bool: return "false";
        case Type::Vec3: return "0 0 0";
        default: return "";
    }
}

std::vector<NodeDef> catalog(const Graph& graph) {
    std::vector<NodeDef> out = builtins();
    const Registry& reg = registry();

    for (const PropertyDef& p : reg.properties) {
        NodeDef get;
        get.id = "get." + p.name; get.title = "Get " + capitalize(p.name); get.category = "Properties"; get.kind = NodeKind::GetProperty; get.target = p.name;
        get.inputs = { {"Target", Type::Node} }; get.outputs = { {capitalize(p.name), p.type} };
        out.push_back(get);
        if (!p.set) continue;
        NodeDef set;
        set.id = "set." + p.name; set.title = "Set " + capitalize(p.name); set.category = "Properties"; set.kind = NodeKind::SetProperty; set.target = p.name;
        set.inputs = { EXEC, {"Target", Type::Node}, {capitalize(p.name), p.type} }; set.outputs = { EXEC };
        out.push_back(set);
    }

    for (const NativeDef& f : reg.functions) {
        NodeDef d;
        d.id = "call." + f.name; d.target = f.name;
        bool method = f.name.compare(0, 5, "node.") == 0;
        size_t dot = f.name.find('.');
        d.title = method ? f.name.substr(5) : f.name;
        d.category = method ? "Node" : (dot == std::string::npos ? "Math" : capitalize(f.name.substr(0, dot)));
        bool exec = f.ret == Type::Void || impureWithResult.count(f.name);
        d.kind = exec ? NodeKind::Call : NodeKind::Pure;
        if (exec) { d.inputs.push_back(EXEC); d.outputs.push_back(EXEC); }
        for (size_t i = 0; i < f.params.size(); i++) d.inputs.push_back({paramName(f.name, i, f.params[i]), f.params[i]});
        if (f.ret != Type::Void) d.outputs.push_back({"Result", f.ret});
        out.push_back(d);
    }

    for (const GraphVar& v : graph.vars) {
        NodeDef get;
        get.id = variableNodeId(false, v.name); get.title = v.name; get.category = "Variables"; get.kind = NodeKind::GetVariable; get.target = v.name;
        get.outputs = { {v.name, v.type} };
        out.push_back(get);
        NodeDef set;
        set.id = variableNodeId(true, v.name); set.title = "Set " + v.name; set.category = "Variables"; set.kind = NodeKind::SetVariable; set.target = v.name;
        set.inputs = { EXEC, {v.name, v.type} }; set.outputs = { EXEC };
        out.push_back(set);
    }
    return out;
}

const NodeDef* findDef(const std::vector<NodeDef>& defs, const std::string& id) {
    for (const NodeDef& d : defs) if (d.id == id) return &d;
    return nullptr;
}

bool isIdentifier(const std::string& name) {
    static const std::set<std::string> reserved = {
        "var", "let", "func", "on", "if", "else", "while", "for", "in", "return", "break", "continue", "true", "false", "null",
        "and", "or", "not", "self", "dt", "other", "float", "int", "bool", "vec3", "string", "node", "str",
    };
    if (name.empty() || (!std::isalpha((unsigned char)name[0]) && name[0] != '_')) return false;
    for (char c : name) if (!std::isalnum((unsigned char)c) && c != '_') return false;
    // Префиксы служебных переменных генератора
    if (name.compare(0, 2, "i_") == 0 || name.compare(0, 2, "r_") == 0) return false;
    if (registry().isNamespace(name) || registry().findFunction(name) >= 0) return false;
    return !reserved.count(name);
}

// =================================================================
// --- Граф ---
// =================================================================
GraphNode* Graph::find(int id) {
    for (GraphNode& n : nodes) if (n.id == id) return &n;
    return nullptr;
}

const GraphNode* Graph::find(int id) const {
    for (const GraphNode& n : nodes) if (n.id == id) return &n;
    return nullptr;
}

int Graph::addNode(const std::string& type, float x, float y) {
    std::vector<NodeDef> defs = catalog(*this);
    const NodeDef* def = findDef(defs, type);
    if (!def) return 0;
    GraphNode n;
    n.id = nextId++;
    n.type = type;
    n.x = x; n.y = y;
    for (const PinDef& pin : def->inputs) n.constants.push_back(pin.isExec() ? "" : defaultConstant(pin.type));
    nodes.push_back(n);
    return n.id;
}

void Graph::removeNode(int id) {
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const GraphNode& n) { return n.id == id; }), nodes.end());
    links.erase(std::remove_if(links.begin(), links.end(), [&](const GraphLink& l) { return l.fromNode == id || l.toNode == id; }), links.end());
}

const GraphLink* Graph::linkInto(int node, int pin) const {
    for (const GraphLink& l : links) if (l.toNode == node && l.toPin == pin) return &l;
    return nullptr;
}

bool Graph::connect(int fromNode, int fromPin, int toNode, int toPin, std::string* why) {
    auto refuse = [&](const char* reason) { if (why) *why = reason; return false; };
    if (fromNode == toNode) return refuse("a node cannot connect to itself");
    std::vector<NodeDef> defs = catalog(*this);
    const GraphNode* a = find(fromNode);
    const GraphNode* b = find(toNode);
    if (!a || !b) return refuse("node not found");
    const NodeDef* da = findDef(defs, a->type);
    const NodeDef* db = findDef(defs, b->type);
    if (!da || !db || fromPin < 0 || fromPin >= (int)da->outputs.size() || toPin < 0 || toPin >= (int)db->inputs.size()) return refuse("no such pin");
    const PinDef& out = da->outputs[fromPin];
    const PinDef& in = db->inputs[toPin];
    if (out.isExec() != in.isExec()) return refuse("execution pins connect only to execution pins");
    if (!out.isExec() && out.type != in.type && !(out.type == Type::Int && in.type == Type::Float))
        return refuse("pin types do not match");

    // У exec-выхода одно продолжение, у входа данных — один источник
    if (out.isExec()) links.erase(std::remove_if(links.begin(), links.end(), [&](const GraphLink& l) { return l.fromNode == fromNode && l.fromPin == fromPin; }), links.end());
    else links.erase(std::remove_if(links.begin(), links.end(), [&](const GraphLink& l) { return l.toNode == toNode && l.toPin == toPin; }), links.end());
    links.push_back({fromNode, fromPin, toNode, toPin});
    return true;
}

// =================================================================
// --- Генерация NLS ---
// =================================================================
namespace {

// Лексер NLS понимает только запись вида 12.5, без экспоненты
std::string floatLiteral(const std::string& text) {
    char* end = nullptr;
    double f = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(f)) f = 0.0;
    char buf[64];
    snprintf(buf, sizeof(buf), "%.6f", f);
    std::string s = buf;
    while (s.size() > 2 && s.back() == '0' && s[s.size() - 2] != '.') s.pop_back();
    return s;
}

std::string quote(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out + "\"";
}

std::string constantLiteral(Type type, const std::string& text, bool isTarget) {
    switch (type) {
        case Type::Float: return floatLiteral(text);
        case Type::Int: return std::to_string(std::atoi(text.c_str()));
        case Type::Bool: return text == "true" ? "true" : "false";
        case Type::String: return quote(text);
        case Type::Vec3: {
            std::istringstream ss(text);
            std::string x = "0", y = "0", z = "0";
            ss >> x >> y >> z;
            return "vec3(" + floatLiteral(x) + ", " + floatLiteral(y) + ", " + floatLiteral(z) + ")";
        }
        case Type::Node: return isTarget ? "self" : "null";
        default: return "";
    }
}

struct Generator {
    const Graph& g;
    std::vector<NodeDef> defs;
    Generated out;
    int indent = 0;
    std::vector<int> execStack;
    std::vector<int> dataStack;

    explicit Generator(const Graph& graph) : g(graph), defs(catalog(graph)) {}

    void line(int node, const std::string& text) {
        out.source += std::string(indent * 4, ' ') + text + "\n";
        out.lineNodes.push_back(node);
    }
    void error(int node, const std::string& message) {
        out.errors.push_back({0, 0, "node #" + std::to_string(node) + ": " + message});
    }

    const NodeDef* defOf(const GraphNode& n) {
        const NodeDef* d = findDef(defs, n.type);
        if (!d) error(n.id, "unknown node type '" + n.type + "'");
        return d;
    }

    std::string input(const GraphNode& n, const NodeDef& d, int pin) {
        if (const GraphLink* l = g.linkInto(n.id, pin)) {
            const GraphNode* src = g.find(l->fromNode);
            if (src) return expr(*src, l->fromPin);
        }
        const std::string& text = pin < (int)n.constants.size() ? n.constants[pin] : std::string();
        return constantLiteral(d.inputs[pin].type, text, d.inputs[pin].name == "Target");
    }

    // Аргументы функции реестра (без exec-пина)
    std::string callArgs(const GraphNode& n, const NodeDef& d, int first) {
        std::string s;
        for (int i = first; i < (int)d.inputs.size(); i++) {
            if (d.inputs[i].isExec()) continue;
            if (!s.empty()) s += ", ";
            s += input(n, d, i);
        }
        return s;
    }

    std::string callExpr(const GraphNode& n, const NodeDef& d) {
        int first = d.kind == NodeKind::Call ? 1 : 0;
        if (d.target.compare(0, 5, "node.") == 0) {
            // node.applyImpulse(target, x) -> (target).applyImpulse(x)
            std::string target = input(n, d, first);
            return "(" + target + ")." + d.target.substr(5) + "(" + callArgs(n, d, first + 1) + ")";
        }
        return d.target + "(" + callArgs(n, d, first) + ")";
    }

    std::string expr(const GraphNode& n, int outPin) {
        const NodeDef* d = defOf(n);
        if (!d) return "0";
        if (std::find(dataStack.begin(), dataStack.end(), n.id) != dataStack.end()) { error(n.id, "data connections form a loop"); return "0"; }
        dataStack.push_back(n.id);
        std::string result = "0";
        switch (d->kind) {
            case NodeKind::Event: result = d->outputs[outPin].name; break;   // dt / other
            case NodeKind::Self: result = "self"; break;
            case NodeKind::GetVariable: result = d->target; break;
            case NodeKind::GetProperty: result = "(" + input(n, *d, 0) + ")." + d->target; break;
            case NodeKind::ForLoop: result = "i_" + std::to_string(n.id); break;
            case NodeKind::Call: result = "r_" + std::to_string(n.id); break;   // Результат, сохранённый при выполнении
            case NodeKind::Pure:
                if (!d->pattern.empty()) {
                    // Шаблон оператора; у узлов с несколькими выходами — по шаблону на выход через '|'
                    std::string pattern = d->pattern;
                    for (int k = 0; k < outPin; k++) pattern = pattern.substr(pattern.find('|') + 1);
                    pattern = pattern.substr(0, pattern.find('|'));
                    result.clear();
                    for (size_t i = 0; i < pattern.size(); i++) {
                        if (pattern[i] == '$' && i + 1 < pattern.size() && std::isdigit((unsigned char)pattern[i + 1])) {
                            result += input(n, *d, pattern[i + 1] - '0');
                            i++;
                        } else result += pattern[i];
                    }
                } else result = callExpr(n, *d);
                break;
            default: error(n.id, "this node has no value output"); break;
        }
        dataStack.pop_back();
        return result;
    }

    void follow(int node, int outPin) {
        for (const GraphLink& l : g.links) {
            if (l.fromNode != node || l.fromPin != outPin) continue;
            if (const GraphNode* next = g.find(l.toNode)) exec(*next);
            return;
        }
    }

    void exec(const GraphNode& n) {
        if (std::find(execStack.begin(), execStack.end(), n.id) != execStack.end()) { error(n.id, "execution flow loops back to this node"); return; }
        const NodeDef* d = defOf(n);
        if (!d) return;
        execStack.push_back(n.id);
        std::string id = std::to_string(n.id);
        switch (d->kind) {
            case NodeKind::Call: {
                const NativeDef& f = registry().functions[registry().findFunction(d->target)];
                if (f.ret == Type::Void) line(n.id, callExpr(n, *d));
                else line(n.id, "let r_" + id + " = " + callExpr(n, *d));
                follow(n.id, 0);
                break;
            }
            case NodeKind::SetProperty:
                line(n.id, "(" + input(n, *d, 1) + ")." + d->target + " = " + input(n, *d, 2));
                follow(n.id, 0);
                break;
            case NodeKind::SetVariable:
                line(n.id, d->target + " = " + input(n, *d, 1));
                follow(n.id, 0);
                break;
            case NodeKind::Branch:
                line(n.id, "if " + input(n, *d, 1) + " {");
                indent++; follow(n.id, 0); indent--;
                line(n.id, "} else {");
                indent++; follow(n.id, 1); indent--;
                line(n.id, "}");
                break;
            case NodeKind::Sequence:
                for (int k = 0; k < (int)d->outputs.size(); k++) follow(n.id, k);
                break;
            case NodeKind::ForLoop:
                line(n.id, "for i_" + id + " in " + input(n, *d, 1) + ".." + input(n, *d, 2) + " {");
                indent++; follow(n.id, 0); indent--;
                line(n.id, "}");
                follow(n.id, 2);
                break;
            default:
                error(n.id, "this node cannot be part of the execution flow");
                break;
        }
        execStack.pop_back();
    }

    void run() {
        out.source = "// Generated from a visual script. Edit the graph, not this text.\n";
        out.lineNodes.push_back(0);

        for (const GraphVar& v : g.vars) {
            if (!isIdentifier(v.name)) { out.errors.push_back({0, 0, "variable name '" + v.name + "' is not allowed"}); continue; }
            std::string decl = "var " + v.name + ": " + typeName(v.type);
            if (v.type != Type::Node) decl += " = " + constantLiteral(v.type, v.initial, false);
            line(0, decl);
        }

        // Одинаковые события из разных узлов сливаются в один обработчик
        std::vector<std::string> order;
        std::map<std::string, std::vector<const GraphNode*>> byEvent;
        for (const GraphNode& n : g.nodes) {
            const NodeDef* d = findDef(defs, n.type);
            if (!d) { error(n.id, "unknown node type '" + n.type + "'"); continue; }
            if (d->kind != NodeKind::Event) continue;
            if (!byEvent.count(d->target)) order.push_back(d->target);
            byEvent[d->target].push_back(&n);
        }
        for (const std::string& name : order) {
            const NodeDef* d = findDef(defs, "event." + name);
            std::string params;
            for (size_t i = 1; i < d->outputs.size(); i++) params += (params.empty() ? "" : ", ") + d->outputs[i].name + ": " + typeName(d->outputs[i].type);
            line(byEvent[name][0]->id, "");
            line(byEvent[name][0]->id, "on " + name + "(" + params + ") {");
            indent++;
            for (const GraphNode* n : byEvent[name]) follow(n->id, 0);
            indent--;
            line(byEvent[name][0]->id, "}");
        }
    }
};

}

Generated generate(const Graph& graph) {
    Generator gen(graph);
    gen.run();
    return gen.out;
}

bool compileGraph(const Graph& graph, Program& out, std::vector<CompileError>& errors, std::vector<int>* errorNodes) {
    Generated gen = generate(graph);
    errors.clear();
    if (errorNodes) errorNodes->clear();
    auto nodeOfMessage = [](const std::string& m) {
        return m.compare(0, 6, "node #") == 0 ? std::atoi(m.c_str() + 6) : 0;
    };
    if (!gen.errors.empty()) {
        errors = gen.errors;
        if (errorNodes) for (const auto& e : errors) errorNodes->push_back(nodeOfMessage(e.message));
        return false;
    }
    if (compile(gen.source, out, errors)) return true;
    // Строка сгенерированного кода -> узел графа
    for (auto& e : errors) {
        int node = (e.line >= 1 && e.line <= (int)gen.lineNodes.size()) ? gen.lineNodes[e.line - 1] : 0;
        if (node > 0) e.message = "node #" + std::to_string(node) + ": " + e.message;
        if (errorNodes) errorNodes->push_back(node);
    }
    return false;
}

// =================================================================
// --- Файл .nlsg ---
// =================================================================
namespace {
    Type typeFromName(const std::string& name) {
        for (Type t : { Type::Float, Type::Int, Type::Bool, Type::Vec3, Type::String, Type::Node }) if (name == typeName(t)) return t;
        return Type::Void;
    }

    // Разбивает строку на слова; "..." — одно слово с экранированием \" \\ \n
    std::vector<std::string> split(const std::string& line) {
        std::vector<std::string> out;
        size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && std::isspace((unsigned char)line[i])) i++;
            if (i >= line.size()) break;
            std::string word;
            if (line[i] == '"') {
                i++;
                while (i < line.size() && line[i] != '"') {
                    if (line[i] == '\\' && i + 1 < line.size()) { char e = line[i + 1]; word += (e == 'n') ? '\n' : e; i += 2; continue; }
                    word += line[i++];
                }
                i++;
            } else {
                while (i < line.size() && !std::isspace((unsigned char)line[i])) word += line[i++];
            }
            out.push_back(word);
        }
        return out;
    }
}

bool saveGraph(const Graph& graph, const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << "nlsg 1\n";
    for (const GraphVar& v : graph.vars) f << "var " << v.name << " " << typeName(v.type) << " " << quote(v.initial) << "\n";
    for (const GraphNode& n : graph.nodes) {
        f << "node " << n.id << " " << n.type << " " << n.x << " " << n.y;
        for (const std::string& c : n.constants) f << " " << quote(c);
        f << "\n";
    }
    for (const GraphLink& l : graph.links) f << "link " << l.fromNode << " " << l.fromPin << " " << l.toNode << " " << l.toPin << "\n";
    return (bool)f;
}

bool loadGraph(const std::string& path, Graph& graph, std::string* error) {
    graph = Graph();
    std::ifstream f(path);
    if (!f.is_open()) { if (error) *error = "cannot open " + path; return false; }
    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        lineNo++;
        std::vector<std::string> w = split(line);
        if (w.empty()) continue;
        try {
            if (w[0] == "nlsg") continue;
            if (w[0] == "var" && w.size() >= 3) {
                Type t = typeFromName(w[2]);
                if (t == Type::Void) throw std::runtime_error("unknown type " + w[2]);
                graph.vars.push_back({w[1], t, w.size() > 3 ? w[3] : defaultConstant(t)});
            } else if (w[0] == "node" && w.size() >= 5) {
                GraphNode n;
                n.id = std::stoi(w[1]); n.type = w[2]; n.x = std::stof(w[3]); n.y = std::stof(w[4]);
                n.constants.assign(w.begin() + 5, w.end());
                graph.nodes.push_back(n);
                graph.nextId = std::max(graph.nextId, n.id + 1);
            } else if (w[0] == "link" && w.size() >= 5) {
                graph.links.push_back({std::stoi(w[1]), std::stoi(w[2]), std::stoi(w[3]), std::stoi(w[4])});
            } else throw std::runtime_error("unknown line");
        } catch (const std::exception& e) {
            if (error) *error = path + ":" + std::to_string(lineNo) + ": " + e.what();
            return false;
        }
    }
    // Число входов могло измениться (реестр пополнился) — подгоняем список констант
    std::vector<NodeDef> defs = catalog(graph);
    for (GraphNode& n : graph.nodes) {
        const NodeDef* d = findDef(defs, n.type);
        if (!d) continue;
        n.constants.resize(d->inputs.size());
        for (size_t i = 0; i < d->inputs.size(); i++) if (n.constants[i].empty() && !d->inputs[i].isExec()) n.constants[i] = defaultConstant(d->inputs[i].type);
    }
    return true;
}

}
