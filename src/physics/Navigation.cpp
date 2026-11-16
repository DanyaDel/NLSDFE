#include "Navigation.h"
#include "Scene.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace nav {

namespace {
    struct Cell { int x, z; };
    inline int64_t keyOf(int x, int z) { return ((int64_t)x << 32) ^ (uint32_t)z; }

    struct Field {
        std::vector<GpuObject> obstacles;   // Неподвижные коллайдеры без самого агента
        const Query* q = nullptr;
        std::unordered_map<int64_t, bool> walkable;

        vec3 center(int x, int z) const { return vec3(q->from.x + x * q->cell, q->from.y, q->from.z + z * q->cell); }

        float distance(vec3 p) const { return obstacles.empty() ? 1e9f : mapObjectList(obstacles, p).x; }

        bool pointFree(vec3 p) const {
            if (!q->needGround) return distance(p) >= q->radius;
            // Зазор до стен меряется выше подошв на радиус с запасом: сам пол (и порожки до 15 см) стеной не считается
            vec3 feet = p - vec3(0.0f, q->height * 0.5f, 0.0f);
            vec3 body = feet + vec3(0.0f, std::max(q->radius + 0.15f, q->height * 0.5f), 0.0f);
            if (distance(body) < q->radius) return false;
            // Опора: что-то твёрдое чуть ниже подошв
            return distance(feet) < 0.5f;
        }

        bool free(int x, int z) {
            int64_t key = keyOf(x, z);
            auto it = walkable.find(key);
            if (it != walkable.end()) return it->second;
            bool ok = pointFree(center(x, z));
            walkable[key] = ok;
            return ok;
        }

        // Прямая между точками свободна на всём протяжении
        bool clear(vec3 a, vec3 b) const {
            float length = glm::distance(a, b);
            int steps = std::max((int)std::ceil(length / (q->cell * 0.5f)), 1);
            for (int i = 1; i <= steps; i++) if (!pointFree(glm::mix(a, b, (float)i / (float)steps))) return false;
            return true;
        }
    };

    bool belongsTo(int node, int ancestor) {
        for (int guard = 0; node >= 0 && node < (int)sceneGraph.size() && guard < 64; guard++) {
            if (node == ancestor) return true;
            node = sceneGraph[node].parent;
        }
        return false;
    }
}

float pathLength(vec3 from, const std::vector<vec3>& path) {
    float total = 0.0f;
    for (const vec3& p : path) { total += glm::distance(from, p); from = p; }
    return total;
}

bool findPath(const Query& query, std::vector<vec3>& path, bool* reached) {
    path.clear();
    if (reached) *reached = false;
    Field field;
    field.q = &query;
    for (const GpuObject& o : collisionCache) {
        int node = (int)std::lround(o.extra.y);
        if (node >= 0 && node < (int)sceneGraph.size()) {
            const SceneNode& n = sceneGraph[node];
            // Триггеры проходимы; тела и персонажи двигаются — в обход них путь не строится
            if (n.isTrigger || n.body.type == BODY_DYNAMIC || n.controller.enabled) continue;
            if (query.ignoreNode >= 0 && belongsTo(node, query.ignoreNode)) continue;
        }
        field.obstacles.push_back(o);
    }
    // Список начинается с другого объекта, чем в сцене: первый всегда объединение
    if (!field.obstacles.empty()) field.obstacles[0].extra.z = 1.0f;

    // Агент, под которым опоры нет с самого начала (висит или летает), ходит без неё
    Query flying = query;
    if (query.needGround && field.distance(query.from - vec3(0.0f, query.height * 0.5f, 0.0f)) >= 0.5f) {
        flying.needGround = false;
        field.q = &flying;
    }

    vec3 goalPoint = vec3(query.to.x, query.from.y, query.to.z);
    if (field.pointFree(goalPoint) && field.clear(query.from, goalPoint)) {
        // Цель видна напрямую
        path.push_back(goalPoint);
        if (reached) *reached = true;
        return true;
    }

    Cell goal = { (int)std::lround((query.to.x - query.from.x) / query.cell), (int)std::lround((query.to.z - query.from.z) / query.cell) };
    auto heuristic = [&](int x, int z) {
        float dx = (float)std::abs(x - goal.x), dz = (float)std::abs(z - goal.z);
        return (dx + dz) + (1.41421356f - 2.0f) * std::min(dx, dz);
    };

    struct Node { float g; int64_t parent; int x, z; bool closed; };
    std::unordered_map<int64_t, Node> nodes;
    using Open = std::pair<float, int64_t>;
    std::priority_queue<Open, std::vector<Open>, std::greater<Open>> open;
    int64_t startKey = keyOf(0, 0);
    nodes[startKey] = { 0.0f, startKey, 0, 0, false };
    open.push({ heuristic(0, 0), startKey });

    int64_t best = startKey;
    float bestH = heuristic(0, 0);
    bool found = false;
    int expanded = 0;
    while (!open.empty() && expanded < query.maxCells) {
        int64_t key = open.top().second;
        open.pop();
        Node& current = nodes[key];
        if (current.closed) continue;
        current.closed = true;
        expanded++;
        int cx = current.x, cz = current.z;
        float cg = current.g;
        float h = heuristic(cx, cz);
        if (h < bestH) { bestH = h; best = key; }
        if (cx == goal.x && cz == goal.z) { found = true; best = key; break; }

        for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
            if (!dx && !dz) continue;
            int nx = cx + dx, nz = cz + dz;
            if (!field.free(nx, nz)) continue;
            // По диагонали — только если не срезается угол стены
            if (dx && dz && (!field.free(cx + dx, cz) || !field.free(cx, cz + dz))) continue;
            float g = cg + ((dx && dz) ? 1.41421356f : 1.0f);
            int64_t nk = keyOf(nx, nz);
            auto it = nodes.find(nk);
            if (it != nodes.end() && (it->second.closed || it->second.g <= g)) continue;
            nodes[nk] = { g, key, nx, nz, false };
            open.push({ g + heuristic(nx, nz), nk });
        }
    }
    if (best == startKey) return false;

    std::vector<vec3> cells;
    for (int64_t key = best; key != startKey; key = nodes[key].parent) cells.push_back(field.center(nodes[key].x, nodes[key].z));
    std::reverse(cells.begin(), cells.end());
    // До самой цели, если она свободна и видна из последней клетки
    if (found && field.pointFree(goalPoint) && field.clear(cells.back(), goalPoint)) cells.back() = goalPoint;
    else if (found && !field.pointFree(goalPoint)) found = false;

    // Спрямление: из каждой точки идём к самой дальней, которая видна напрямую
    vec3 from = query.from;
    for (size_t i = 0; i < cells.size();) {
        size_t far = i;
        for (size_t j = cells.size() - 1; j > i; j--) if (field.clear(from, cells[j])) { far = j; break; }
        path.push_back(cells[far]);
        from = cells[far];
        i = far + 1;
    }
    if (reached) *reached = found;
    return true;
}

}
