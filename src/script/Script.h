#pragma once
// NLS — скриптовый язык движка: статическая типизация, компиляция в байткод, стековая VM.
// Ядро языка (лексер, парсер, компилятор, VM) не знает о движке: всё, что скрипт может
// делать с миром, приходит через реестр нативных функций и свойств (см. Bindings.cpp).
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace nls {

enum class Type : uint8_t { Void, Float, Int, Bool, Vec3, String, Node };
const char* typeName(Type t);

// Слот стека и переменной. Тип известен на этапе компиляции, поэтому метки нет.
// Строка и узел хранятся индексом в i (строка — в таблице строк VM, узел — в sceneGraph, -1 = null).
struct Value {
    union { float f; int32_t i; float v[3]; };
    Value() { v[0] = v[1] = v[2] = 0.0f; }
    static Value Float(float x) { Value r; r.f = x; return r; }
    static Value Int(int32_t x) { Value r; r.i = x; return r; }
    static Value Vec(float x, float y, float z) { Value r; r.v[0] = x; r.v[1] = y; r.v[2] = z; return r; }
};

struct VM;
using NativeFn = void (*)(VM& vm, Value* args, Value& result);

struct NativeDef {
    std::string name;            // "log", "input.pressed", "node.applyImpulse" (первый аргумент — узел)
    Type ret = Type::Void;
    std::vector<Type> params;
    NativeFn fn = nullptr;
};

struct PropertyDef {
    std::string name;            // Свойство узла: "position", "visible", ...
    Type type = Type::Float;
    NativeFn get = nullptr;      // args[0] = узел
    NativeFn set = nullptr;      // args[0] = узел, args[1] = значение; nullptr = только чтение
};

// Всё, что движок открывает скриптам. Новая возможность = одна строка регистрации.
class Registry {
public:
    void function(const std::string& name, Type ret, std::vector<Type> params, NativeFn fn);
    void property(const std::string& name, Type type, NativeFn get, NativeFn set = nullptr);
    int findFunction(const std::string& name) const;
    int findProperty(const std::string& name) const;
    bool isNamespace(const std::string& name) const;   // "input", "scene", ...
    std::vector<NativeDef> functions;
    std::vector<PropertyDef> properties;
private:
    std::unordered_map<std::string, int> functionIndex, propertyIndex;
};
Registry& registry();

struct CompileError {
    int line = 0, column = 0;
    std::string message;
};

struct GlobalVar {
    std::string name;
    Type type = Type::Float;
};

struct Function {
    std::string name;
    std::vector<Type> params;
    Type ret = Type::Void;
    int entry = 0;
    int localSlots = 0;
    bool isEvent = false;
};

struct Program {
    std::vector<uint32_t> code;            // Младшие 8 бит — операция, старшие 24 — аргумент
    std::vector<int32_t> lines;            // Строка исходника на каждую инструкцию
    std::vector<Value> constants;
    std::vector<std::string> strings;
    std::vector<GlobalVar> globals;
    std::vector<Function> functions;
    std::vector<std::string> nativeNames;  // Нативные функции и свойства по именам:
    std::vector<std::string> propertyNames;// индексы реестра подставляются при link()
    std::vector<int> nativeLink, propertyLink;
    int initFunction = -1;                 // Вычисляет начальные значения var

    int findFunction(const std::string& name, bool event) const;
};

// Исходник -> байткод. false и список ошибок, если скрипт не собрался.
bool compile(const std::string& source, Program& out, std::vector<CompileError>& errors);
// Привязывает имена нативных функций к текущему реестру (после compile или loadProgram).
bool link(Program& program, std::string& error);
bool saveProgram(const Program& program, const std::string& path);   // .nlsc
bool loadProgram(const std::string& path, Program& program);

// Один экземпляр скрипта на узел: свои значения var, общий байткод
struct VM {
    const Program* program = nullptr;
    std::vector<Value> globals;
    std::vector<std::string> strings;
    int self = -1;                         // Узел, на котором висит скрипт
    bool failed = false;                   // После ошибки скрипт больше не вызывается
    std::string error;
    uint64_t instructionLimit = 2000000;   // На один вызов события: защита от вечных циклов

    void init(const Program* p);           // Создаёт переменные и считает их начальные значения
    bool call(int functionIndex, const Value* args = nullptr, int argc = 0, Value* result = nullptr);
    void fail(const std::string& message); // Для нативных функций: прерывает скрипт с ошибкой

    Value newString(const std::string& s);
    const std::string& string(const Value& v) const;

private:
    std::vector<Value> stack;
    int currentLine = 0;
    void compactStrings();
};

std::string formatFloat(float f);

// Как показать узел в строке ("Cube" вместо индекса); задаёт движок
extern std::string (*nodeNameHook)(int node);

}
