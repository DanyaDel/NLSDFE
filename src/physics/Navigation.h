#pragma once
#include "Common.h"
#include <vector>

// Поиск пути по сцене: клетки на высоте агента, проходимость — по полю расстояний коллайдеров.
// Мир считается плоским: путь ищется в горизонтальной плоскости на высоте from.y.
namespace nav {

struct Query {
    vec3 from = {0, 0, 0};
    vec3 to = {0, 0, 0};
    float radius = 0.4f;       // Зазор до стен
    float height = 1.6f;       // Рост агента: под ногами (from.y - height / 2) должна быть опора
    float cell = 0.5f;
    int ignoreNode = -1;       // Сам агент (с потомками) препятствием не считается
    int maxCells = 6000;       // Предел поиска: дальше цель считается недостижимой
    bool needGround = true;    // Не ходить над пустотой (если агент и так стоит на опоре)
};

// Точки пути от from до to (from не входит). Если цель недостижима или стоит в стене,
// путь ведёт к ближайшей достижимой точке; reached говорит, дошёл ли он до самой цели.
// false — идти некуда вовсе. Перед вызовом должен отработать updateSceneCaches().
bool findPath(const Query& query, std::vector<vec3>& path, bool* reached = nullptr);

float pathLength(vec3 from, const std::vector<vec3>& path);

}
