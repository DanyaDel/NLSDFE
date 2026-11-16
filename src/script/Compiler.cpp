#include "Ast.h"
#include <cstring>
#include <map>

namespace nls {

const char* typeName(Type t) {
    switch (t) {
        case Type::Float: return "float";
        case Type::Int: return "int";
        case Type::Bool: return "bool";
        case Type::Vec3: return "vec3";
        case Type::String: return "string";
        case Type::Node: return "node";
        default: return "void";
    }
}

namespace {

struct CompileAbort {};

struct EventSignature { const char* name; std::vector<Type> params; };
const std::vector<EventSignature> eventSignatures = {
    {"start", {}},
    {"update", {Type::Float}},
    {"fixedUpdate", {Type::Float}},
    {"collide", {Type::Node}},
    {"triggerEnter", {Type::Node}},
    {"triggerExit", {Type::Node}},
    {"message", {Type::String, Type::Float}},
};

class Compiler {
public:
    Compiler(Program& p, std::vector<CompileError>& e) : prog(p), errors(e) {}

    void compileProgram(const std::vector<Decl>& decls) {
        prog.strings.push_back("");   // Строка по умолчанию: нулевой Value — это ""

        // Проход 1: сигнатуры функций и событий
        for (const Decl& d : decls) {
            if (d.kind == Decl::Var) continue;
            try { declareFunction(d); } catch (CompileAbort&) {}
        }

        // Проход 2: глобальные переменные и функция их инициализации
        Function init;
        init.name = "__init";
        init.entry = (int)prog.code.size();
        beginFunction(Type::Void);
        for (const Decl& d : decls) {
            if (d.kind != Decl::Var) continue;
            try { compileGlobal(d); } catch (CompileAbort&) {}
        }
        emit(OP_RET_VOID, 0, 0);
        init.localSlots = maxSlot;
        prog.initFunction = (int)prog.functions.size();
        prog.functions.push_back(init);

        // Проход 3: тела
        for (const Decl& d : decls) {
            if (d.kind == Decl::Var) continue;
            auto it = declIndex.find(&d);
            if (it == declIndex.end()) continue;
            try { compileFunction(d, it->second); } catch (CompileAbort&) {}
        }
    }

private:
    Program& prog;
    std::vector<CompileError>& errors;

    struct Local { std::string name; Type type; int slot; };
    struct Loop { std::vector<int> breaks, continues; };

    std::vector<std::vector<Local>> scopes;
    std::vector<Loop> loops;
    int nextSlot = 0, maxSlot = 0;
    Type returnType = Type::Void;
    bool inEvent = false;

    std::map<std::string, int> globalIndex, functionIndex, nativeIndex, propertyIndex, stringIndex;
    std::map<const Decl*, int> declIndex;

    template <typename N> [[noreturn]] void fail(const N& at, const std::string& message) {
        errors.push_back({at.line, at.column, message});
        throw CompileAbort{};
    }

    int emit(Op op, int arg, int line) {
        prog.code.push_back(encode(op, arg));
        prog.lines.push_back(line);
        return (int)prog.code.size() - 1;
    }
    void patch(int at) { prog.code[at] = encode(opOf(prog.code[at]), (int)prog.code.size()); }

    int constant(Value v) {
        for (size_t i = 0; i < prog.constants.size(); i++) if (memcmp(&prog.constants[i], &v, sizeof(Value)) == 0) return (int)i;
        prog.constants.push_back(v);
        return (int)prog.constants.size() - 1;
    }
    int stringConstant(const std::string& s) {
        auto it = stringIndex.find(s);
        if (it != stringIndex.end()) return it->second;
        for (size_t i = 0; i < prog.strings.size(); i++) if (prog.strings[i] == s) return stringIndex[s] = (int)i;
        prog.strings.push_back(s);
        return stringIndex[s] = (int)prog.strings.size() - 1;
    }
    int nativeSlot(const std::string& name) {
        auto it = nativeIndex.find(name);
        if (it != nativeIndex.end()) return it->second;
        prog.nativeNames.push_back(name);
        return nativeIndex[name] = (int)prog.nativeNames.size() - 1;
    }
    int propertySlot(const std::string& name) {
        auto it = propertyIndex.find(name);
        if (it != propertyIndex.end()) return it->second;
        prog.propertyNames.push_back(name);
        return propertyIndex[name] = (int)prog.propertyNames.size() - 1;
    }

    // --- Области видимости ---
    void beginFunction(Type ret) {
        scopes.clear(); scopes.emplace_back();
        loops.clear();
        nextSlot = maxSlot = 0;
        returnType = ret;
    }
    int declareLocal(const std::string& name, Type type, const Stmt* at = nullptr) {
        for (const Local& l : scopes.back()) if (l.name == name && at) fail(*at, "variable '" + name + "' is already declared in this block");
        scopes.back().push_back({name, type, nextSlot});
        maxSlot = std::max(maxSlot, nextSlot + 1);
        return nextSlot++;
    }
    const Local* findLocal(const std::string& name) const {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
            for (auto l = scope->rbegin(); l != scope->rend(); ++l) if (l->name == name) return &*l;
        return nullptr;
    }
    bool isVariable(const std::string& name) const { return findLocal(name) || globalIndex.count(name); }

    // --- Объявления ---
    void declareFunction(const Decl& d) {
        Function f;
        f.name = d.name;
        f.isEvent = d.kind == Decl::Event;
        f.ret = d.hasType ? d.type : Type::Void;
        for (const Param& p : d.params) f.params.push_back(p.type);

        if (f.isEvent) {
            const EventSignature* sig = nullptr;
            for (const auto& e : eventSignatures) if (d.name == e.name) sig = &e;
            if (!sig) fail(d, "unknown event '" + d.name + "' (start, update, fixedUpdate, collide, triggerEnter, triggerExit)");
            if (sig->params != f.params) {
                std::string expected;
                for (Type t : sig->params) expected += std::string(expected.empty() ? "" : ", ") + typeName(t);
                fail(d, "event '" + d.name + "' takes (" + expected + ")");
            }
            if (d.hasType) fail(d, "events do not return a value");
            if (prog.findFunction(d.name, true) >= 0) fail(d, "event '" + d.name + "' is defined twice");
        } else {
            if (functionIndex.count(d.name)) fail(d, "function '" + d.name + "' is defined twice");
            if (registry().findFunction(d.name) >= 0 || d.name == "vec3" || d.name == "float" || d.name == "int" || d.name == "str")
                fail(d, "'" + d.name + "' is a built-in function");
            functionIndex[d.name] = (int)prog.functions.size();
        }
        declIndex[&d] = (int)prog.functions.size();
        prog.functions.push_back(f);
    }

    void compileGlobal(const Decl& d) {
        if (globalIndex.count(d.name)) fail(d, "variable '" + d.name + "' is defined twice");
        if (d.name == "self") fail(d, "'self' is reserved");
        Type type = d.type;
        if (d.init) {
            Type valueType = expr(*d.init);
            if (!d.hasType) {
                if (valueType == Type::Void) fail(d, "initial value of '" + d.name + "' has no type");
                type = valueType;
            } else convert(valueType, type, *d.init);
        } else {
            pushDefault(type, d.line);
        }
        int index = (int)prog.globals.size();
        prog.globals.push_back({d.name, type});
        globalIndex[d.name] = index;
        emit(OP_STORE_GLOBAL, index, d.line);
    }

    void compileFunction(const Decl& d, int index) {
        prog.functions[index].entry = (int)prog.code.size();
        beginFunction(prog.functions[index].ret);
        inEvent = d.kind == Decl::Event;
        for (const Param& p : d.params) declareLocal(p.name, p.type);
        block(d.body, false);
        // Страховочный возврат, если тело дошло до конца
        if (returnType != Type::Void) { pushDefault(returnType, d.line); emit(OP_RET, 0, d.line); }
        else emit(OP_RET_VOID, 0, d.line);
        prog.functions[index].localSlots = maxSlot;
    }

    void pushDefault(Type type, int line) {
        if (type == Type::Node) emit(OP_NULL, 0, line);
        else if (type == Type::String) emit(OP_STR, 0, line);
        else emit(OP_CONST, constant(Value()), line);
    }

    // --- Преобразования типов ---
    template <typename N> void convert(Type from, Type to, const N& at) {
        if (from == to) return;
        if (from == Type::Int && to == Type::Float) { emit(OP_I2F, 0, at.line); return; }
        fail(at, std::string("expected ") + typeName(to) + ", got " + typeName(from)
                 + (from == Type::Float && to == Type::Int ? " (use int(x) to round down)" : ""));
    }

    // --- Операторы ---
    Type binaryOp(const std::string& op, Type l, Type r, const Expr& at) {
        int line = at.line;
        auto mismatch = [&]() -> Type { fail(at, "operator '" + op + "' cannot be applied to " + typeName(l) + " and " + typeName(r)); };

        if (op == "+" && (l == Type::String || r == Type::String)) {
            if (l == Type::Void || r == Type::Void) mismatch();
            if (l != Type::String) emit(OP_TOSTR, (int)l | 16, line);
            if (r != Type::String) emit(OP_TOSTR, (int)r, line);
            emit(OP_CONCAT, 0, line);
            return Type::String;
        }

        bool numeric = (l == Type::Float || l == Type::Int) && (r == Type::Float || r == Type::Int);
        if (numeric && l != r) {
            // Смешанная арифметика считается во float
            if (l == Type::Int) emit(OP_I2F_UNDER, 0, line); else emit(OP_I2F, 0, line);
            l = r = Type::Float;
        }
        bool isFloat = numeric && l == Type::Float;

        if (op == "+" || op == "-") {
            if (numeric) { emit(op == "+" ? (isFloat ? OP_ADD_F : OP_ADD_I) : (isFloat ? OP_SUB_F : OP_SUB_I), 0, line); return l; }
            if (l == Type::Vec3 && r == Type::Vec3) { emit(op == "+" ? OP_ADD_V : OP_SUB_V, 0, line); return Type::Vec3; }
            mismatch();
        }
        if (op == "*") {
            if (numeric) { emit(isFloat ? OP_MUL_F : OP_MUL_I, 0, line); return l; }
            if (l == Type::Vec3 && r == Type::Vec3) { emit(OP_MUL_V, 0, line); return Type::Vec3; }
            if (l == Type::Vec3 && (r == Type::Float || r == Type::Int)) { if (r == Type::Int) emit(OP_I2F, 0, line); emit(OP_MUL_VF, 0, line); return Type::Vec3; }
            if ((l == Type::Float || l == Type::Int) && r == Type::Vec3) { if (l == Type::Int) emit(OP_I2F_UNDER, 0, line); emit(OP_MUL_FV, 0, line); return Type::Vec3; }
            mismatch();
        }
        if (op == "/") {
            if (numeric) { emit(isFloat ? OP_DIV_F : OP_DIV_I, 0, line); return l; }
            if (l == Type::Vec3 && (r == Type::Float || r == Type::Int)) { if (r == Type::Int) emit(OP_I2F, 0, line); emit(OP_DIV_VF, 0, line); return Type::Vec3; }
            mismatch();
        }
        if (op == "%") {
            if (numeric) { emit(isFloat ? OP_MOD_F : OP_MOD_I, 0, line); return l; }
            mismatch();
        }
        if (op == "==" || op == "!=") {
            if (l != r) mismatch();
            if (l == Type::Float) emit(OP_EQ_F, 0, line);
            else if (l == Type::Vec3) emit(OP_EQ_V, 0, line);
            else if (l == Type::String) emit(OP_EQ_S, 0, line);
            else if (l == Type::Void) mismatch();
            else emit(OP_EQ_I, 0, line);
            if (op == "!=") emit(OP_NOT, 0, line);
            return Type::Bool;
        }
        if (op == "<" || op == "<=" || op == ">" || op == ">=") {
            if (!numeric) mismatch();
            Op f = op == "<" ? OP_LT_F : op == "<=" ? OP_LE_F : op == ">" ? OP_GT_F : OP_GE_F;
            Op i = op == "<" ? OP_LT_I : op == "<=" ? OP_LE_I : op == ">" ? OP_GT_I : OP_GE_I;
            emit(isFloat ? f : i, 0, line);
            return Type::Bool;
        }
        mismatch();
    }

    // --- Выражения ---
    Type expr(const Expr& e) {
        switch (e.kind) {
            case Expr::Number: emit(OP_CONST, constant(Value::Float((float)e.number)), e.line); return Type::Float;
            case Expr::IntLit:
                if (e.number < 8000000.0) emit(OP_INT, (int)e.number, e.line);
                else emit(OP_CONST, constant(Value::Int((int32_t)e.number)), e.line);
                return Type::Int;
            case Expr::BoolLit: emit(OP_INT, e.number != 0.0 ? 1 : 0, e.line); return Type::Bool;
            case Expr::StringLit: emit(OP_STR, stringConstant(e.name), e.line); return Type::String;
            case Expr::NullLit: emit(OP_NULL, 0, e.line); return Type::Node;
            case Expr::Name: return nameExpr(e);
            case Expr::Unary: {
                Type t = expr(*e.args[0]);
                if (e.name == "not") {
                    if (t != Type::Bool) fail(e, std::string("'not' needs a bool, got ") + typeName(t));
                    emit(OP_NOT, 0, e.line);
                    return Type::Bool;
                }
                if (t == Type::Float) emit(OP_NEG_F, 0, e.line);
                else if (t == Type::Int) emit(OP_NEG_I, 0, e.line);
                else if (t == Type::Vec3) emit(OP_NEG_V, 0, e.line);
                else fail(e, std::string("cannot negate ") + typeName(t));
                return t;
            }
            case Expr::Binary: {
                if (e.name == "and" || e.name == "or") {
                    Type l = expr(*e.args[0]);
                    if (l != Type::Bool) fail(*e.args[0], "'" + e.name + "' needs bool operands, got " + typeName(l));
                    // Правая часть не вычисляется, если результат уже известен
                    int skip = emit(e.name == "and" ? OP_JMP_FALSE_KEEP : OP_JMP_TRUE_KEEP, 0, e.line);
                    emit(OP_POP, 0, e.line);
                    Type r = expr(*e.args[1]);
                    if (r != Type::Bool) fail(*e.args[1], "'" + e.name + "' needs bool operands, got " + typeName(r));
                    patch(skip);
                    return Type::Bool;
                }
                Type l = expr(*e.args[0]);
                Type r = expr(*e.args[1]);
                return binaryOp(e.name, l, r, e);
            }
            case Expr::Call: return callExpr(e);
            case Expr::Member: return memberExpr(e);
            case Expr::Method: return methodExpr(e);
        }
        return Type::Void;
    }

    Type nameExpr(const Expr& e) {
        if (const Local* l = findLocal(e.name)) { emit(OP_LOAD_LOCAL, l->slot, e.line); return l->type; }
        auto g = globalIndex.find(e.name);
        if (g != globalIndex.end()) { emit(OP_LOAD_GLOBAL, g->second, e.line); return prog.globals[g->second].type; }
        if (e.name == "self") { emit(OP_SELF, 0, e.line); return Type::Node; }
        if (registry().isNamespace(e.name)) fail(e, "'" + e.name + "' is a module: call one of its functions, e.g. " + e.name + ".name(...)");
        fail(e, "unknown name '" + e.name + "'");
    }

    void arguments(const Expr& call, size_t first, const std::vector<Type>& params, size_t paramOffset, const std::string& what) {
        size_t given = call.args.size() - first, wanted = params.size() - paramOffset;
        if (given != wanted) fail(call, what + " takes " + std::to_string(wanted) + " argument(s), got " + std::to_string(given));
        for (size_t i = 0; i < given; i++) {
            Type t = expr(*call.args[first + i]);
            convert(t, params[paramOffset + i], *call.args[first + i]);
        }
    }

    Type nativeCall(const Expr& e, const std::string& fullName, size_t firstArg, size_t paramOffset) {
        int index = registry().findFunction(fullName);
        const NativeDef& def = registry().functions[index];
        arguments(e, firstArg, def.params, paramOffset, fullName + "()");
        emit(OP_NATIVE, nativeSlot(fullName), e.line);
        return def.ret;
    }

    Type callExpr(const Expr& e) {
        // Конструкторы и приведения типов
        if (e.name == "vec3") {
            if (e.args.size() != 3) fail(e, "vec3() takes 3 numbers");
            for (const auto& a : e.args) convert(expr(*a), Type::Float, *a);
            emit(OP_MAKE_VEC, 0, e.line);
            return Type::Vec3;
        }
        if (e.name == "float" || e.name == "int" || e.name == "str") {
            if (e.args.size() != 1) fail(e, e.name + "() takes 1 argument");
            Type t = expr(*e.args[0]);
            if (e.name == "str") {
                if (t == Type::Void) fail(e, "str() needs a value");
                if (t != Type::String) emit(OP_TOSTR, (int)t, e.line);
                return Type::String;
            }
            if (t != Type::Float && t != Type::Int) fail(e, e.name + "() needs a number, got " + typeName(t));
            if (e.name == "float") { if (t == Type::Int) emit(OP_I2F, 0, e.line); return Type::Float; }
            if (t == Type::Float) emit(OP_F2I, 0, e.line);
            return Type::Int;
        }

        auto f = functionIndex.find(e.name);
        if (f != functionIndex.end()) {
            const Function& fn = prog.functions[f->second];
            std::vector<Type> params = fn.params;
            Type ret = fn.ret;
            arguments(e, 0, params, 0, e.name + "()");
            emit(OP_CALL, f->second, e.line);
            return ret;
        }
        if (registry().findFunction(e.name) >= 0) return nativeCall(e, e.name, 0, 0);
        fail(e, "unknown function '" + e.name + "'");
    }

    // true, если выражение — имя модуля (input, scene, ...), а не переменная
    bool isNamespaceRef(const Expr& e) const {
        return e.kind == Expr::Name && !isVariable(e.name) && e.name != "self" && registry().isNamespace(e.name);
    }

    Type memberExpr(const Expr& e) {
        const Expr& object = *e.args[0];
        if (isNamespaceRef(object)) fail(e, "'" + object.name + "." + e.name + "' is a function: add ()");
        Type t = expr(object);
        if (t == Type::Vec3) {
            int comp = e.name == "x" ? 0 : e.name == "y" ? 1 : e.name == "z" ? 2 : -1;
            if (comp < 0) fail(e, "vec3 has fields x, y, z, not '" + e.name + "'");
            emit(OP_GET_COMP, comp, e.line);
            return Type::Float;
        }
        if (t == Type::Node) {
            if (e.name == "body") return Type::Node;   // node.body.velocity — то же, что node.velocity
            int index = registry().findProperty(e.name);
            if (index < 0) {
                if (registry().findFunction("node." + e.name) >= 0) fail(e, "'" + e.name + "' is a method: add ()");
                fail(e, "node has no property '" + e.name + "'");
            }
            emit(OP_GET_PROP, propertySlot(e.name), e.line);
            return registry().properties[index].type;
        }
        fail(e, std::string(typeName(t)) + " has no field '" + e.name + "'");
    }

    Type methodExpr(const Expr& e) {
        const Expr& object = *e.args[0];
        if (isNamespaceRef(object)) {
            std::string fullName = object.name + "." + e.name;
            if (registry().findFunction(fullName) < 0) fail(e, "module '" + object.name + "' has no function '" + e.name + "'");
            return nativeCall(e, fullName, 1, 0);
        }
        Type t = expr(object);
        if (t != Type::Node) fail(e, std::string(typeName(t)) + " has no method '" + e.name + "'");
        std::string fullName = "node." + e.name;
        if (registry().findFunction(fullName) < 0) fail(e, "node has no method '" + e.name + "'");
        return nativeCall(e, fullName, 1, 1);
    }

    // --- Операторы ---
    void block(const std::vector<StmtPtr>& body, bool newScope = true) {
        int savedSlot = nextSlot;
        if (newScope) scopes.emplace_back();
        for (const auto& s : body) stmt(*s);
        if (newScope) { scopes.pop_back(); nextSlot = savedSlot; }
    }

    void condition(const Expr& e, const char* what) {
        Type t = expr(e);
        if (t != Type::Bool) fail(e, std::string(what) + " condition must be bool, got " + typeName(t));
    }

    void stmt(const Stmt& s) {
        switch (s.kind) {
            case Stmt::Let: {
                Type type = s.type;
                if (s.value) {
                    Type valueType = expr(*s.value);
                    if (!s.hasType) {
                        if (valueType == Type::Void) fail(s, "value of '" + s.name + "' has no type");
                        type = valueType;
                    } else convert(valueType, type, *s.value);
                } else pushDefault(type, s.line);
                if (s.name == "self") fail(s, "'self' is reserved");
                emit(OP_STORE_LOCAL, declareLocal(s.name, type, &s), s.line);
                break;
            }
            case Stmt::Assign: assignment(s); break;
            case Stmt::ExprStmt: {
                if (s.value->kind != Expr::Call && s.value->kind != Expr::Method) fail(s, "this expression does nothing: expected an assignment or a call");
                if (expr(*s.value) != Type::Void) emit(OP_POP, 0, s.line);
                break;
            }
            case Stmt::If: {
                condition(*s.value, "'if'");
                int toElse = emit(OP_JMP_FALSE, 0, s.line);
                block(s.body);
                if (s.elseBody.empty()) { patch(toElse); break; }
                int toEnd = emit(OP_JMP, 0, s.line);
                patch(toElse);
                block(s.elseBody);
                patch(toEnd);
                break;
            }
            case Stmt::While: {
                int start = (int)prog.code.size();
                condition(*s.value, "'while'");
                int exit = emit(OP_JMP_FALSE, 0, s.line);
                loops.emplace_back();
                block(s.body);
                for (int c : loops.back().continues) prog.code[c] = encode(OP_JMP, start);
                emit(OP_JMP, start, s.line);
                patch(exit);
                for (int b : loops.back().breaks) patch(b);
                loops.pop_back();
                break;
            }
            case Stmt::For: {
                int savedSlot = nextSlot;
                scopes.emplace_back();
                Type from = expr(*s.value);
                if (from != Type::Int) fail(*s.value, std::string("range bounds must be int, got ") + typeName(from));
                int counter = declareLocal(s.name, Type::Int);
                emit(OP_STORE_LOCAL, counter, s.line);
                Type to = expr(*s.extra);
                if (to != Type::Int) fail(*s.extra, std::string("range bounds must be int, got ") + typeName(to));
                int limit = declareLocal("", Type::Int);
                emit(OP_STORE_LOCAL, limit, s.line);

                int start = (int)prog.code.size();
                emit(OP_LOAD_LOCAL, counter, s.line);
                emit(OP_LOAD_LOCAL, limit, s.line);
                emit(OP_LT_I, 0, s.line);
                int exit = emit(OP_JMP_FALSE, 0, s.line);
                loops.emplace_back();
                block(s.body);
                int step = (int)prog.code.size();
                for (int c : loops.back().continues) prog.code[c] = encode(OP_JMP, step);
                emit(OP_LOAD_LOCAL, counter, s.line);
                emit(OP_INT, 1, s.line);
                emit(OP_ADD_I, 0, s.line);
                emit(OP_STORE_LOCAL, counter, s.line);
                emit(OP_JMP, start, s.line);
                patch(exit);
                for (int b : loops.back().breaks) patch(b);
                loops.pop_back();
                scopes.pop_back();
                nextSlot = savedSlot;
                break;
            }
            case Stmt::Return: {
                if (returnType == Type::Void) {
                    if (s.value) fail(s, inEvent ? "events do not return a value" : "this function has no return type");
                    emit(OP_RET_VOID, 0, s.line);
                } else {
                    if (!s.value) fail(s, std::string("return needs a ") + typeName(returnType) + " value");
                    convert(expr(*s.value), returnType, *s.value);
                    emit(OP_RET, 0, s.line);
                }
                break;
            }
            case Stmt::Break:
                if (loops.empty()) fail(s, "'break' outside of a loop");
                loops.back().breaks.push_back(emit(OP_JMP, 0, s.line));
                break;
            case Stmt::Continue:
                if (loops.empty()) fail(s, "'continue' outside of a loop");
                loops.back().continues.push_back(emit(OP_JMP, 0, s.line));
                break;
            case Stmt::Block: block(s.body); break;
        }
    }

    // Куда пишет присваивание: переменная, её компонента, свойство узла или компонента свойства
    struct Target {
        bool isLocal = false;
        int variable = -1;       // Слот или индекс глобальной; -1 — цель не переменная
        int property = -1;       // Слот в propertyNames
        int component = -1;      // x/y/z
        Type type = Type::Void;  // Тип присваиваемого значения
        const Expr* nodeExpr = nullptr;
    };

    Target resolveTarget(const Expr& e) {
        Target t;
        auto variable = [&](const Expr& name) {
            if (const Local* l = findLocal(name.name)) { t.isLocal = true; t.variable = l->slot; return l->type; }
            auto g = globalIndex.find(name.name);
            if (g != globalIndex.end()) { t.variable = g->second; return prog.globals[g->second].type; }
            if (name.name == "self") fail(name, "'self' cannot be assigned");
            fail(name, "unknown variable '" + name.name + "'");
        };
        auto component = [&](const Expr& member) {
            int comp = member.name == "x" ? 0 : member.name == "y" ? 1 : member.name == "z" ? 2 : -1;
            if (comp < 0) fail(member, "vec3 has fields x, y, z, not '" + member.name + "'");
            return comp;
        };
        auto property = [&](const Expr& member) {
            int index = registry().findProperty(member.name);
            if (index < 0) fail(member, "node has no property '" + member.name + "'");
            if (!registry().properties[index].set) fail(member, "property '" + member.name + "' is read-only");
            t.property = propertySlot(member.name);
            // node.body.x — то же, что node.x
            const Expr* owner = member.args[0].get();
            while (owner->kind == Expr::Member && owner->name == "body") owner = owner->args[0].get();
            t.nodeExpr = owner;
            return registry().properties[index].type;
        };

        if (e.kind == Expr::Name) { t.type = variable(e); return t; }
        if (e.kind != Expr::Member) fail(e, "cannot assign to this expression");

        const Expr& object = *e.args[0];
        // переменная.x
        if (object.kind == Expr::Name && isVariable(object.name)) {
            Type vt = variable(object);
            if (vt == Type::Vec3) { t.component = component(e); t.type = Type::Float; return t; }
            if (vt == Type::Node) { t.variable = -1; t.type = property(e); return t; }
            fail(e, std::string(typeName(vt)) + " has no field '" + e.name + "'");
        }
        // узел.свойство.x
        if (object.kind == Expr::Member && object.name != "body" && registry().findProperty(object.name) >= 0
            && registry().properties[registry().findProperty(object.name)].type == Type::Vec3
            && (e.name == "x" || e.name == "y" || e.name == "z")) {
            property(object);
            t.component = component(e);
            t.type = Type::Float;
            return t;
        }
        // узел.свойство
        t.type = property(e);
        return t;
    }

    void nodeOperand(const Expr& e) {
        Type t = expr(e);
        if (t != Type::Node) fail(e, std::string("expected a node, got ") + typeName(t));
    }

    void assignment(const Stmt& s) {
        Target t = resolveTarget(*s.target);
        int line = s.line;

        // Под значение кладём то, что понадобится при записи
        if (t.property >= 0) {
            nodeOperand(*t.nodeExpr);
            if (t.component >= 0) { nodeOperand(*t.nodeExpr); emit(OP_GET_PROP, t.property, line); }
        } else if (t.component >= 0) {
            emit(t.isLocal ? OP_LOAD_LOCAL : OP_LOAD_GLOBAL, t.variable, line);
        }

        if (s.name == "=") {
            convert(expr(*s.value), t.type, *s.value);
        } else {
            // a += b  ->  a = a + b
            Type current = expr(*s.target);
            Type value = expr(*s.value);
            Type result = binaryOp(s.name.substr(0, 1), current, value, *s.value);
            convert(result, t.type, *s.value);
        }

        if (t.component >= 0) emit(OP_SET_COMP, t.component, line);
        if (t.property >= 0) emit(OP_SET_PROP, t.property, line);
        else emit(t.isLocal ? OP_STORE_LOCAL : OP_STORE_GLOBAL, t.variable, line);
    }
};

}

int Program::findFunction(const std::string& name, bool event) const {
    for (size_t i = 0; i < functions.size(); i++) if (functions[i].isEvent == event && functions[i].name == name) return (int)i;
    return -1;
}

bool compile(const std::string& source, Program& out, std::vector<CompileError>& errors) {
    out = Program();
    errors.clear();
    std::vector<Token> tokens;
    if (!tokenize(source, tokens, errors)) return false;
    std::vector<Decl> decls;
    parse(tokens, decls, errors);
    // Даже при синтаксических ошибках проверяем то, что удалось разобрать: больше ошибок за один проход
    Compiler(out, errors).compileProgram(decls);
    if (!errors.empty()) return false;
    std::string linkError;
    if (!link(out, linkError)) { errors.push_back({0, 0, linkError}); return false; }
    return true;
}

}
