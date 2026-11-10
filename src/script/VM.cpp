#include "Ast.h"
#include <cmath>
#include <cstdio>
#include <fstream>

namespace nls {

std::string (*nodeNameHook)(int node) = nullptr;

// =================================================================
// --- Реестр ---
// =================================================================
Registry& registry() {
    static Registry r;
    return r;
}

void Registry::function(const std::string& name, Type ret, std::vector<Type> params, NativeFn fn) {
    auto it = functionIndex.find(name);
    NativeDef def{name, ret, std::move(params), fn};
    if (it != functionIndex.end()) { functions[it->second] = def; return; }
    functionIndex[name] = (int)functions.size();
    functions.push_back(def);
}

void Registry::property(const std::string& name, Type type, NativeFn get, NativeFn set) {
    auto it = propertyIndex.find(name);
    PropertyDef def{name, type, get, set};
    if (it != propertyIndex.end()) { properties[it->second] = def; return; }
    propertyIndex[name] = (int)properties.size();
    properties.push_back(def);
}

int Registry::findFunction(const std::string& name) const {
    auto it = functionIndex.find(name);
    return it == functionIndex.end() ? -1 : it->second;
}

int Registry::findProperty(const std::string& name) const {
    auto it = propertyIndex.find(name);
    return it == propertyIndex.end() ? -1 : it->second;
}

bool Registry::isNamespace(const std::string& name) const {
    if (name == "node") return false;
    std::string prefix = name + ".";
    for (const auto& f : functions) if (f.name.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
}

bool link(Program& program, std::string& error) {
    program.nativeLink.clear();
    program.propertyLink.clear();
    for (const auto& name : program.nativeNames) {
        int index = registry().findFunction(name);
        if (index < 0) { error = "script needs function '" + name + "' which this engine build does not provide"; return false; }
        program.nativeLink.push_back(index);
    }
    for (const auto& name : program.propertyNames) {
        int index = registry().findProperty(name);
        if (index < 0) { error = "script needs node property '" + name + "' which this engine build does not provide"; return false; }
        program.propertyLink.push_back(index);
    }
    return true;
}

std::string formatFloat(float f) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%g", f);
    return buf;
}

// =================================================================
// --- VM ---
// =================================================================
namespace {
    const int STACK_SIZE = 8192;
    const int MAX_FRAMES = 200;
    struct Frame { int returnPc; int base; };
}

void VM::init(const Program* p) {
    program = p;
    failed = false;
    error.clear();
    globals.assign(p ? p->globals.size() : 0, Value());
    strings = p ? p->strings : std::vector<std::string>{""};
    if (stack.size() != (size_t)STACK_SIZE) stack.resize(STACK_SIZE);
    if (p && p->initFunction >= 0) call(p->initFunction);
}

void VM::fail(const std::string& message) {
    if (failed) return;
    failed = true;
    error = (currentLine > 0 ? "line " + std::to_string(currentLine) + ": " : "") + message;
}

Value VM::newString(const std::string& s) {
    strings.push_back(s);
    return Value::Int((int32_t)strings.size() - 1);
}

const std::string& VM::string(const Value& v) const {
    static const std::string empty;
    return (v.i >= 0 && v.i < (int)strings.size()) ? strings[v.i] : empty;
}

// Временные строки живут до конца вызова; выживают только константы и то, что лежит в переменных
void VM::compactStrings() {
    size_t constants = program->strings.size();
    if (strings.size() <= constants + 64) {
        bool anyTemp = false;
        for (size_t g = 0; g < globals.size(); g++) if (program->globals[g].type == Type::String && globals[g].i >= (int)constants) anyTemp = true;
        if (!anyTemp) { strings.resize(constants); return; }
    }
    std::vector<std::string> kept(strings.begin(), strings.begin() + constants);
    for (size_t g = 0; g < globals.size(); g++) {
        if (program->globals[g].type != Type::String || globals[g].i < (int)constants) continue;
        kept.push_back(string(globals[g]));
        globals[g].i = (int32_t)kept.size() - 1;
    }
    strings.swap(kept);
}

bool VM::call(int functionIndex, const Value* args, int argc, Value* result) {
    if (failed || !program || functionIndex < 0 || functionIndex >= (int)program->functions.size()) return false;
    const Program& p = *program;
    const Function& entry = p.functions[functionIndex];
    if (argc != (int)entry.params.size()) { fail("wrong argument count for '" + entry.name + "'"); return false; }

    Value* st = stack.data();
    Frame frames[MAX_FRAMES];
    int depth = 0;
    int base = 0, sp = 0;
    for (int i = 0; i < argc; i++) st[sp++] = args[i];
    sp = base + std::max(entry.localSlots, argc);
    int pc = entry.entry;
    uint64_t budget = instructionLimit;
    const uint32_t* code = p.code.data();
    const Registry& reg = registry();

    #define BIN_F(op) { st[sp - 2].f = st[sp - 2].f op st[sp - 1].f; sp--; break; }
    #define BIN_I(op) { st[sp - 2].i = st[sp - 2].i op st[sp - 1].i; sp--; break; }
    #define CMP_F(op) { st[sp - 2].i = (st[sp - 2].f op st[sp - 1].f) ? 1 : 0; sp--; break; }
    #define CMP_I(op) { st[sp - 2].i = (st[sp - 2].i op st[sp - 1].i) ? 1 : 0; sp--; break; }

    for (;;) {
        if (--budget == 0) { currentLine = p.lines[pc]; fail("script ran too long (infinite loop?)"); break; }
        uint32_t ins = code[pc];
        int arg = argOf(ins);
        switch (opOf(ins)) {
            case OP_HALT: goto done;
            case OP_CONST: st[sp++] = p.constants[arg]; break;
            case OP_INT: st[sp++] = Value::Int(arg); break;
            case OP_STR: st[sp++] = Value::Int(arg); break;
            case OP_NULL: st[sp++] = Value::Int(-1); break;
            case OP_SELF: st[sp++] = Value::Int(self); break;
            case OP_POP: sp--; break;
            case OP_LOAD_LOCAL: st[sp++] = st[base + arg]; break;
            case OP_STORE_LOCAL: st[base + arg] = st[--sp]; break;
            case OP_LOAD_GLOBAL: st[sp++] = globals[arg]; break;
            case OP_STORE_GLOBAL: globals[arg] = st[--sp]; break;

            case OP_ADD_F: BIN_F(+)
            case OP_SUB_F: BIN_F(-)
            case OP_MUL_F: BIN_F(*)
            case OP_DIV_F: BIN_F(/)
            case OP_MOD_F: st[sp - 2].f = std::fmod(st[sp - 2].f, st[sp - 1].f); sp--; break;
            case OP_NEG_F: st[sp - 1].f = -st[sp - 1].f; break;
            case OP_ADD_I: BIN_I(+)
            case OP_SUB_I: BIN_I(-)
            case OP_MUL_I: BIN_I(*)
            case OP_DIV_I:
            case OP_MOD_I:
                if (st[sp - 1].i == 0) { currentLine = p.lines[pc]; fail("division by zero"); goto done; }
                if (opOf(ins) == OP_DIV_I) st[sp - 2].i = st[sp - 2].i / st[sp - 1].i; else st[sp - 2].i = st[sp - 2].i % st[sp - 1].i;
                sp--; break;
            case OP_NEG_I: st[sp - 1].i = -st[sp - 1].i; break;

            case OP_ADD_V: for (int k = 0; k < 3; k++) st[sp - 2].v[k] += st[sp - 1].v[k]; sp--; break;
            case OP_SUB_V: for (int k = 0; k < 3; k++) st[sp - 2].v[k] -= st[sp - 1].v[k]; sp--; break;
            case OP_MUL_V: for (int k = 0; k < 3; k++) st[sp - 2].v[k] *= st[sp - 1].v[k]; sp--; break;
            case OP_MUL_VF: { float s = st[sp - 1].f; for (int k = 0; k < 3; k++) st[sp - 2].v[k] *= s; sp--; break; }
            case OP_MUL_FV: { float s = st[sp - 2].f; for (int k = 0; k < 3; k++) st[sp - 2].v[k] = st[sp - 1].v[k] * s; sp--; break; }
            case OP_DIV_VF: { float s = st[sp - 1].f; for (int k = 0; k < 3; k++) st[sp - 2].v[k] /= s; sp--; break; }
            case OP_NEG_V: for (int k = 0; k < 3; k++) st[sp - 1].v[k] = -st[sp - 1].v[k]; break;

            case OP_CONCAT: { Value r = newString(string(st[sp - 2]) + string(st[sp - 1])); sp--; st[sp - 1] = r; break; }
            case OP_I2F: st[sp - 1] = Value::Float((float)st[sp - 1].i); break;
            case OP_I2F_UNDER: st[sp - 2] = Value::Float((float)st[sp - 2].i); break;
            case OP_F2I: st[sp - 1] = Value::Int((int32_t)std::floor(st[sp - 1].f)); break;
            case OP_TOSTR: {
                Value& v = st[sp - ((arg & 16) ? 2 : 1)];
                std::string text;
                switch ((Type)(arg & 15)) {
                    case Type::Float: text = formatFloat(v.f); break;
                    case Type::Int: text = std::to_string(v.i); break;
                    case Type::Bool: text = v.i ? "true" : "false"; break;
                    case Type::Vec3: text = "(" + formatFloat(v.v[0]) + ", " + formatFloat(v.v[1]) + ", " + formatFloat(v.v[2]) + ")"; break;
                    case Type::Node: text = v.i < 0 ? "null" : (nodeNameHook ? nodeNameHook(v.i) : "node#" + std::to_string(v.i)); break;
                    default: break;
                }
                v = newString(text);
                break;
            }

            case OP_EQ_F: CMP_F(==)
            case OP_LT_F: CMP_F(<)
            case OP_LE_F: CMP_F(<=)
            case OP_GT_F: CMP_F(>)
            case OP_GE_F: CMP_F(>=)
            case OP_EQ_I: CMP_I(==)
            case OP_LT_I: CMP_I(<)
            case OP_LE_I: CMP_I(<=)
            case OP_GT_I: CMP_I(>)
            case OP_GE_I: CMP_I(>=)
            case OP_EQ_V: {
                bool eq = st[sp - 2].v[0] == st[sp - 1].v[0] && st[sp - 2].v[1] == st[sp - 1].v[1] && st[sp - 2].v[2] == st[sp - 1].v[2];
                sp--; st[sp - 1] = Value::Int(eq ? 1 : 0); break;
            }
            case OP_EQ_S: { bool eq = string(st[sp - 2]) == string(st[sp - 1]); sp--; st[sp - 1] = Value::Int(eq ? 1 : 0); break; }
            case OP_NOT: st[sp - 1].i = st[sp - 1].i ? 0 : 1; break;

            case OP_MAKE_VEC: { Value r = Value::Vec(st[sp - 3].f, st[sp - 2].f, st[sp - 1].f); sp -= 2; st[sp - 1] = r; break; }
            case OP_GET_COMP: st[sp - 1] = Value::Float(st[sp - 1].v[arg]); break;
            case OP_SET_COMP: st[sp - 2].v[arg] = st[sp - 1].f; sp--; break;

            case OP_JMP: pc = arg; continue;
            case OP_JMP_FALSE: if (!st[--sp].i) { pc = arg; continue; } break;
            case OP_JMP_FALSE_KEEP: if (!st[sp - 1].i) { pc = arg; continue; } break;
            case OP_JMP_TRUE_KEEP: if (st[sp - 1].i) { pc = arg; continue; } break;

            case OP_CALL: {
                const Function& f = p.functions[arg];
                int argCount = (int)f.params.size();
                if (depth >= MAX_FRAMES || sp + f.localSlots + 256 > STACK_SIZE) { currentLine = p.lines[pc]; fail("stack overflow (endless recursion?)"); goto done; }
                frames[depth++] = { pc + 1, base };
                base = sp - argCount;
                sp = base + std::max(f.localSlots, argCount);
                pc = f.entry;
                continue;
            }
            case OP_RET:
            case OP_RET_VOID: {
                bool hasValue = opOf(ins) == OP_RET;
                Value r = hasValue ? st[sp - 1] : Value();
                sp = base;
                if (depth == 0) { if (result && hasValue) *result = r; goto done; }
                Frame f = frames[--depth];
                base = f.base;
                pc = f.returnPc;
                if (hasValue) st[sp++] = r;
                continue;
            }
            case OP_NATIVE: {
                const NativeDef& def = reg.functions[p.nativeLink[arg]];
                int argCount = (int)def.params.size();
                Value r;
                currentLine = p.lines[pc];
                def.fn(*this, st + sp - argCount, r);
                if (failed) goto done;
                sp -= argCount;
                if (def.ret != Type::Void) st[sp++] = r;
                break;
            }
            case OP_GET_PROP: {
                const PropertyDef& def = reg.properties[p.propertyLink[arg]];
                Value r;
                currentLine = p.lines[pc];
                def.get(*this, st + sp - 1, r);
                if (failed) goto done;
                st[sp - 1] = r;
                break;
            }
            case OP_SET_PROP: {
                const PropertyDef& def = reg.properties[p.propertyLink[arg]];
                Value r;
                currentLine = p.lines[pc];
                def.set(*this, st + sp - 2, r);
                if (failed) goto done;
                sp -= 2;
                break;
            }
        }
        pc++;
    }
done:
    #undef BIN_F
    #undef BIN_I
    #undef CMP_F
    #undef CMP_I
    // Строку-результат забирают сразу после вызова, поэтому чистим таблицу только когда результата нет
    if (!failed && !result) compactStrings();
    return !failed;
}

// =================================================================
// --- .nlsc ---
// =================================================================
namespace {
    const uint32_t NLSC_VERSION = 1;
    template <typename T> void put(std::ostream& f, const T& v) { f.write(reinterpret_cast<const char*>(&v), sizeof(T)); }
    template <typename T> bool get(std::istream& f, T& v) { f.read(reinterpret_cast<char*>(&v), sizeof(T)); return (bool)f; }
    void putString(std::ostream& f, const std::string& s) { put(f, (uint32_t)s.size()); f.write(s.data(), s.size()); }
    bool getString(std::istream& f, std::string& s) {
        uint32_t n = 0;
        if (!get(f, n) || n > (1u << 20)) return false;
        s.resize(n); f.read(s.data(), n);
        return (bool)f;
    }
    template <typename T> void putVector(std::ostream& f, const std::vector<T>& v) { put(f, (uint32_t)v.size()); if (!v.empty()) f.write(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(T)); }
    template <typename T> bool getVector(std::istream& f, std::vector<T>& v) {
        uint32_t n = 0;
        if (!get(f, n) || n > (1u << 24)) return false;
        v.resize(n);
        if (n) f.read(reinterpret_cast<char*>(v.data()), n * sizeof(T));
        return (bool)f;
    }
    void putStrings(std::ostream& f, const std::vector<std::string>& v) { put(f, (uint32_t)v.size()); for (const auto& s : v) putString(f, s); }
    bool getStrings(std::istream& f, std::vector<std::string>& v) {
        uint32_t n = 0;
        if (!get(f, n) || n > (1u << 20)) return false;
        v.resize(n);
        for (auto& s : v) if (!getString(f, s)) return false;
        return true;
    }
}

bool saveProgram(const Program& p, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.write("NLSC", 4);
    put(f, NLSC_VERSION);
    putVector(f, p.code); putVector(f, p.lines); putVector(f, p.constants);
    putStrings(f, p.strings); putStrings(f, p.nativeNames); putStrings(f, p.propertyNames);
    put(f, (uint32_t)p.globals.size());
    for (const auto& g : p.globals) { putString(f, g.name); put(f, (uint8_t)g.type); }
    put(f, (uint32_t)p.functions.size());
    for (const auto& fn : p.functions) {
        putString(f, fn.name); putVector(f, fn.params); put(f, (uint8_t)fn.ret);
        put(f, (int32_t)fn.entry); put(f, (int32_t)fn.localSlots); put(f, (uint8_t)fn.isEvent);
    }
    put(f, (int32_t)p.initFunction);
    return (bool)f;
}

bool loadProgram(const std::string& path, Program& p) {
    p = Program();
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    char magic[4] = {};
    uint32_t version = 0, count = 0;
    if (!f.read(magic, 4) || std::string(magic, 4) != "NLSC" || !get(f, version) || version != NLSC_VERSION) return false;
    if (!getVector(f, p.code) || !getVector(f, p.lines) || !getVector(f, p.constants)) return false;
    if (!getStrings(f, p.strings) || !getStrings(f, p.nativeNames) || !getStrings(f, p.propertyNames)) return false;
    if (!get(f, count) || count > 65536) return false;
    p.globals.resize(count);
    for (auto& g : p.globals) { uint8_t t = 0; if (!getString(f, g.name) || !get(f, t)) return false; g.type = (Type)t; }
    if (!get(f, count) || count > 65536) return false;
    p.functions.resize(count);
    for (auto& fn : p.functions) {
        uint8_t ret = 0, isEvent = 0; int32_t entry = 0, slots = 0;
        if (!getString(f, fn.name) || !getVector(f, fn.params) || !get(f, ret) || !get(f, entry) || !get(f, slots) || !get(f, isEvent)) return false;
        fn.ret = (Type)ret; fn.entry = entry; fn.localSlots = slots; fn.isEvent = isEvent != 0;
    }
    int32_t init = -1;
    if (!get(f, init)) return false;
    p.initFunction = init;
    // Базовая проверка целостности: файл собран самим движком, но мог быть обрезан
    if (p.lines.size() != p.code.size() || p.strings.empty()) return false;
    for (const auto& fn : p.functions) if (fn.entry < 0 || fn.entry >= (int)p.code.size() || fn.localSlots < 0 || fn.localSlots > 4096) return false;
    if (p.initFunction < -1 || p.initFunction >= (int)p.functions.size()) return false;
    std::string error;
    return link(p, error);
}

}
