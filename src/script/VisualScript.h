#pragma once
// Визуальные скрипты: граф узлов (.nlsg), который генерирует обычный код NLS.
// Один язык и одна VM: граф всегда можно открыть как текст, а всё, что движок
// открывает скриптам (реестр в Bindings.cpp), автоматически появляется и узлами.
#include "Script.h"
#include <string>
#include <vector>

namespace nls::visual {

struct PinDef {
    std::string name;
    Type type = Type::Void;   // Void — пин потока выполнения (exec)
    bool isExec() const { return type == Type::Void; }
};

enum class NodeKind {
    Event,          // on start / update / ...
    Call,           // Функция из реестра (с exec-пинами, если что-то меняет)
    Pure,           // Функция или оператор без побочных эффектов: только данные
    GetProperty, SetProperty,
    GetVariable, SetVariable,
    Branch, Sequence, ForLoop,
    Self,
};

struct NodeDef {
    std::string id;           // "event.update", "call.log", "get.position", "op.add_f", ...
    std::string title;
    std::string category;     // Для палитры
    NodeKind kind = NodeKind::Pure;
    std::vector<PinDef> inputs, outputs;
    std::string target;       // Имя функции, свойства, события или переменной
    std::string pattern;      // Для операторов: шаблон выражения, $0 $1 ... — входы, $out — номер выхода
};

struct GraphVar {
    std::string name;
    Type type = Type::Float;
    std::string initial;      // Текст значения по умолчанию (как во вводе узла)
};

struct GraphNode {
    int id = 0;
    std::string type;                      // NodeDef::id
    float x = 0.0f, y = 0.0f;              // Позиция на холсте
    std::vector<std::string> constants;    // Значения несоединённых входов, по индексу входа
};

struct GraphLink {
    int fromNode = 0, fromPin = 0;         // Выход
    int toNode = 0, toPin = 0;             // Вход
};

struct Graph {
    std::vector<GraphVar> vars;
    std::vector<GraphNode> nodes;
    std::vector<GraphLink> links;
    int nextId = 1;

    GraphNode* find(int id);
    const GraphNode* find(int id) const;
    int addNode(const std::string& type, float x, float y);   // 0, если тип неизвестен
    void removeNode(int id);
    // Проверяет типы, убирает конфликтующие связи; false — соединять нельзя
    bool connect(int fromNode, int fromPin, int toNode, int toPin, std::string* why = nullptr);
    const GraphLink* linkInto(int node, int pin) const;
};

// Каталог узлов: встроенные плюс всё из реестра и переменные графа.
// Строится заново по требованию — реестр может пополняться.
std::vector<NodeDef> catalog(const Graph& graph);
const NodeDef* findDef(const std::vector<NodeDef>& defs, const std::string& id);

// Для узлов-переменных тип зависит от графа: "var.get.speed"
std::string variableNodeId(bool set, const std::string& name);

// Граф -> NLS. lineNodes[i] — id узла, породившего строку i+1 (0 — служебная строка),
// чтобы ошибки компилятора показывать на узлах.
struct Generated {
    std::string source;
    std::vector<int> lineNodes;
    std::vector<CompileError> errors;      // Ошибки самого графа (циклы, неверные имена)
};
Generated generate(const Graph& graph);

// Граф целиком: генерация и компиляция. Ошибки — с номером узла в начале сообщения.
bool compileGraph(const Graph& graph, Program& out, std::vector<CompileError>& errors, std::vector<int>* errorNodes = nullptr);

bool saveGraph(const Graph& graph, const std::string& path);
bool loadGraph(const std::string& path, Graph& graph, std::string* error = nullptr);

bool isIdentifier(const std::string& name);
std::string defaultConstant(Type type);

}
