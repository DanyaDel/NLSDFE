#pragma once
#include "Common.h"
#include <map>
#include <string>
#include <vector>

// Обмен с Blender (аддон tools/blender/nlsdfe_link.py). Движок пишет exchange.json,
// Blender при сохранении пишет result.json (SDF) или result.obj (меш).
struct BlenderSession {
    bool mesh = false;
    bool modelDocument = false;            // Открыто из Model Editor (другой документ, чем сцена)
    std::string name;
    std::string dir, exchange, result;
    long long resultTime = 0;              // Время изменения результата, который уже применён

    // SDF: какие узлы движка соответствуют объектам Blender
    std::map<std::string, int> keyToNode;  // Имя объекта в Blender -> индекс узла
    std::map<int, std::string> nodeNames;  // Индекс узла -> его имя в движке (проверка, что узел тот же)
    std::vector<int> exported;             // Узлы, ушедшие в Blender (их id лежат в exchange.json)

    int meshNode = -1;                     // Меш: узел, чей меш правится
};

// Оси: у движка Y вверх, у Blender — Z. Матрицы переводятся вместе с осями объекта.
mat4 engineToBlender(const mat4& m);
mat4 blenderToEngine(const mat4& m);

bool writeSdfExchange(const std::vector<int>& nodes, const std::string& dir, BlenderSession& session, std::string* error = nullptr);
bool writeMeshExchange(int node, const std::vector<Vertex>& vertices, const std::string& dir, BlenderSession& session, std::string* error = nullptr);

// Новый результат на диске? (время изменения больше уже применённого)
bool blenderResultChanged(const BlenderSession& session, long long* time = nullptr);

// Применяет result.json к сцене одним шагом undo: двигает, меняет, добавляет и удаляет узлы
bool applySdfResult(BlenderSession& session, std::string* error = nullptr);
