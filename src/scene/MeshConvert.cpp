#include "MeshConvert.h"
#include "Scene.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

// =================================================================
// --- SDF -> Mesh ---
// =================================================================
namespace {
    // Мировые габариты объекта по его ограничивающему боксу
    void objectBounds(const GpuObject& o, vec3& lo, vec3& hi) {
        int type = (int)o.rot0.w;
        vec3 he = shapeHalfExtents(type, o.pos.w, o.scale.w) * vec3(o.scale);
        if (type == SDF_VOLUME) he = vec3(o.scale) * std::abs(o.pos.w);
        // Строки обратного поворота — это столбцы прямого
        mat3 rot = glm::transpose(mat3(vec3(o.rot0), vec3(o.rot1), vec3(o.rot2)));
        vec3 extent = glm::abs(rot[0]) * he.x + glm::abs(rot[1]) * he.y + glm::abs(rot[2]) * he.z;
        lo = vec3(o.pos) - extent;
        hi = vec3(o.pos) + extent;
    }

    vec3 fieldGradient(const std::vector<GpuObject>& objects, vec3 p, float e) {
        vec3 n(mapObjectList(objects, p + vec3(e, 0, 0)).x - mapObjectList(objects, p - vec3(e, 0, 0)).x,
               mapObjectList(objects, p + vec3(0, e, 0)).x - mapObjectList(objects, p - vec3(0, e, 0)).x,
               mapObjectList(objects, p + vec3(0, 0, e)).x - mapObjectList(objects, p - vec3(0, 0, e)).x);
        float len = glm::length(n);
        return len > 1e-12f ? n / len : vec3(0, 1, 0);
    }
}

bool sdfToMesh(const std::vector<GpuObject>& objects, int resolution, OBJModel& out, vec3& outCenter) {
    out = OBJModel();
    if (objects.empty()) return false;
    resolution = std::clamp(resolution, 8, 400);

    // Габариты: всё, что объединено. Вычитание и пересечение поверхность только уменьшают.
    vec3 lo(1e9f), hi(-1e9f);
    for (size_t i = 0; i < objects.size(); i++) {
        if (i > 0 && (int)objects[i].rot1.w != OP_UNION) continue;
        vec3 a, b;
        objectBounds(objects[i], a, b);
        lo = glm::min(lo, a); hi = glm::max(hi, b);
    }
    vec3 size = hi - lo;
    float cell = std::max(size.x, std::max(size.y, size.z)) / (float)resolution;
    if (!(cell > 0.0f)) return false;
    lo -= vec3(cell * 2.0f);   // Запас, чтобы поверхность не касалась края сетки
    hi += vec3(cell * 2.0f);
    glm::ivec3 n = glm::ivec3(glm::ceil((hi - lo) / cell));   // Ячеек по осям
    glm::ivec3 np = n + 1;                                     // Узлов сетки
    outCenter = (lo + hi) * 0.5f;

    auto corner = [&](int x, int y, int z) { return lo + vec3(x, y, z) * cell; };
    auto cornerIndex = [&](int x, int y, int z) { return (size_t)x + (size_t)np.x * ((size_t)y + (size_t)np.y * (size_t)z); };
    auto cellIndex = [&](int x, int y, int z) { return (size_t)x + (size_t)n.x * ((size_t)y + (size_t)n.y * (size_t)z); };

    std::vector<float> dist((size_t)np.x * np.y * np.z);
    for (int z = 0; z < np.z; z++) for (int y = 0; y < np.y; y++) for (int x = 0; x < np.x; x++)
        dist[cornerIndex(x, y, z)] = mapObjectList(objects, corner(x, y, z)).x;

    // Одна вершина на ячейку, через которую проходит поверхность
    static const int edgeCorners[12][2] = {
        {0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };
    std::vector<int> cellVertex((size_t)n.x * n.y * n.z, -1);
    std::vector<vec3> positions;
    for (int z = 0; z < n.z; z++) for (int y = 0; y < n.y; y++) for (int x = 0; x < n.x; x++) {
        float d[8];
        vec3 p[8];
        int inside = 0;
        for (int c = 0; c < 8; c++) {
            int cx = x + (c & 1), cy = y + ((c >> 1) & 1), cz = z + ((c >> 2) & 1);
            d[c] = dist[cornerIndex(cx, cy, cz)];
            p[c] = corner(cx, cy, cz);
            if (d[c] < 0.0f) inside++;
        }
        if (inside == 0 || inside == 8) continue;

        // Среднее точек пересечения рёбер, затем шаг Ньютона к поверхности — острее грани
        vec3 sum(0.0f);
        int count = 0;
        for (const auto& e : edgeCorners) {
            float a = d[e[0]], b = d[e[1]];
            if ((a < 0.0f) == (b < 0.0f)) continue;
            sum += glm::mix(p[e[0]], p[e[1]], a / (a - b));
            count++;
        }
        vec3 v = sum / (float)count;
        for (int iter = 0; iter < 2; iter++) {
            float dv = mapObjectList(objects, v).x;
            v -= fieldGradient(objects, v, cell * 0.25f) * dv;
        }
        // Вершина остаётся в своей ячейке (с небольшим запасом), иначе сетка перекручивается
        v = glm::clamp(v, p[0] - vec3(cell * 0.5f), p[7] + vec3(cell * 0.5f));
        cellVertex[cellIndex(x, y, z)] = (int)positions.size();
        positions.push_back(v);
    }
    if (positions.empty()) return false;

    // Каждое ребро сетки со сменой знака даёт четырёхугольник из вершин четырёх соседних ячеек
    auto emit = [&](int a, int b, int c) {
        vec3 pa = positions[a], pb = positions[b], pc = positions[c];
        vec3 face = glm::cross(pb - pa, pc - pa);
        if (glm::dot(face, face) < 1e-20f) return;
        vec3 centroid = (pa + pb + pc) / 3.0f;
        // Ориентация по полю: нормаль треугольника смотрит наружу
        if (glm::dot(face, fieldGradient(objects, centroid, cell * 0.25f)) < 0.0f) std::swap(b, c);
        for (int idx : { a, b, c }) {
            Vertex vert{};
            vert.pos = positions[idx] - outCenter;
            vert.normal = fieldGradient(objects, positions[idx], cell * 0.25f);
            vec2 hit = mapObjectList(objects, positions[idx]);
            int obj = hit.y >= 0.0f ? (int)hit.y : 0;
            vert.color = vec4(vec3(objects[obj].color), 1.0f);
            out.vertices.push_back(vert);
            out.indices.push_back((uint32_t)out.indices.size());
        }
    };
    for (int axis = 0; axis < 3; axis++) {
        int ax1 = (axis + 1) % 3, ax2 = (axis + 2) % 3;
        for (int z = 0; z < np.z; z++) for (int y = 0; y < np.y; y++) for (int x = 0; x < np.x; x++) {
            glm::ivec3 c(x, y, z);
            if (c[axis] >= n[axis]) continue;
            if (c[ax1] < 1 || c[ax1] >= n[ax1] || c[ax2] < 1 || c[ax2] >= n[ax2]) continue;
            glm::ivec3 c2 = c; c2[axis]++;
            float a = dist[cornerIndex(c.x, c.y, c.z)], b = dist[cornerIndex(c2.x, c2.y, c2.z)];
            if ((a < 0.0f) == (b < 0.0f)) continue;
            int quad[4];
            bool complete = true;
            const int offsets[4][2] = { {-1, -1}, {0, -1}, {0, 0}, {-1, 0} };
            for (int k = 0; k < 4; k++) {
                glm::ivec3 cc = c;
                cc[ax1] += offsets[k][0];
                cc[ax2] += offsets[k][1];
                quad[k] = cellVertex[cellIndex(cc.x, cc.y, cc.z)];
                if (quad[k] < 0) complete = false;
            }
            if (!complete) continue;
            emit(quad[0], quad[1], quad[2]);
            emit(quad[0], quad[2], quad[3]);
        }
    }
    return !out.vertices.empty();
}

// =================================================================
// --- Mesh -> SDF ---
// =================================================================
namespace {
    // Ближайшая точка треугольника (Ericson, Real-Time Collision Detection, 5.1.5)
    vec3 closestOnTriangle(vec3 p, vec3 a, vec3 b, vec3 c) {
        vec3 ab = b - a, ac = c - a, ap = p - a;
        float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f) return a;
        vec3 bp = p - b;
        float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
        if (d3 >= 0.0f && d4 <= d3) return b;
        float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) return a + ab * (d1 / (d1 - d3));
        vec3 cp = p - c;
        float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
        if (d6 >= 0.0f && d5 <= d6) return c;
        float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) return a + ac * (d2 / (d2 - d6));
        float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
        float denom = 1.0f / (va + vb + vc);
        return a + ab * (vb * denom) + ac * (vc * denom);
    }
}

std::shared_ptr<SdfVolume> meshToVolume(const std::vector<Vertex>& tris, int res, vec3& outCenter, float& outHalf) {
    if (tris.size() < 3) return nullptr;
    res = std::clamp(res, 8, 256);
    size_t triCount = tris.size() / 3;

    vec3 lo(1e30f), hi(-1e30f);
    for (const Vertex& v : tris) { lo = glm::min(lo, v.pos); hi = glm::max(hi, v.pos); }
    outCenter = (lo + hi) * 0.5f;
    // Куб с запасом: снаружи меша тоже должны быть верные расстояния
    outHalf = std::max(hi.x - lo.x, std::max(hi.y - lo.y, hi.z - lo.z)) * 0.5f * 1.15f + 1e-4f;
    float half = outHalf;
    float cell = 2.0f * half / (float)(res - 1);
    vec3 origin = outCenter - vec3(half);
    auto point = [&](int x, int y, int z) { return origin + vec3(x, y, z) * cell; };
    auto index = [&](int x, int y, int z) { return (size_t)x + (size_t)res * ((size_t)y + (size_t)res * (size_t)z); };
    size_t total = (size_t)res * res * res;

    // 1. Точные расстояния в узкой полосе вокруг треугольников
    const float INF = std::numeric_limits<float>::max();
    std::vector<float> dist(total, INF);
    std::vector<vec3> nearest(total, vec3(0.0f));
    for (size_t t = 0; t < triCount; t++) {
        vec3 a = tris[t * 3].pos, b = tris[t * 3 + 1].pos, c = tris[t * 3 + 2].pos;
        glm::ivec3 i0 = glm::max(glm::ivec3(glm::floor((glm::min(a, glm::min(b, c)) - origin) / cell)) - 1, glm::ivec3(0));
        glm::ivec3 i1 = glm::min(glm::ivec3(glm::ceil((glm::max(a, glm::max(b, c)) - origin) / cell)) + 1, glm::ivec3(res - 1));
        for (int z = i0.z; z <= i1.z; z++) for (int y = i0.y; y <= i1.y; y++) for (int x = i0.x; x <= i1.x; x++) {
            vec3 p = point(x, y, z);
            vec3 q = closestOnTriangle(p, a, b, c);
            float d = glm::length(p - q);
            size_t i = index(x, y, z);
            if (d < dist[i]) { dist[i] = d; nearest[i] = q; }
        }
    }

    // 2. Дальше — передача ближайшей точки соседям (обход в восьми направлениях)
    for (int pass = 0; pass < 2; pass++) {
        for (int dir = 0; dir < 8; dir++) {
            int sx = (dir & 1) ? -1 : 1, sy = (dir & 2) ? -1 : 1, sz = (dir & 4) ? -1 : 1;
            for (int zi = 0; zi < res; zi++) for (int yi = 0; yi < res; yi++) for (int xi = 0; xi < res; xi++) {
                int x = sx > 0 ? xi : res - 1 - xi, y = sy > 0 ? yi : res - 1 - yi, z = sz > 0 ? zi : res - 1 - zi;
                size_t i = index(x, y, z);
                vec3 p = point(x, y, z);
                const int nx = x - sx, ny = y - sy, nz = z - sz;
                const glm::ivec3 neighbours[3] = { {nx, y, z}, {x, ny, z}, {x, y, nz} };
                for (const auto& nb : neighbours) {
                    if (nb.x < 0 || nb.y < 0 || nb.z < 0 || nb.x >= res || nb.y >= res || nb.z >= res) continue;
                    size_t j = index(nb.x, nb.y, nb.z);
                    if (dist[j] == INF) continue;
                    float d = glm::length(p - nearest[j]);
                    if (d < dist[i]) { dist[i] = d; nearest[i] = nearest[j]; }
                }
            }
        }
    }

    // 3. Знак: лучи вдоль каждой оси считают пересечения; узел внутри, если большинство осей
    //    дали нечётное число пересечений до него. Голосование гасит ошибки на рёбрах треугольников.
    std::vector<uint8_t> votes(total, 0);
    for (int axis = 0; axis < 3; axis++) {
        int u = (axis + 1) % 3, v = (axis + 2) % 3;
        std::vector<std::vector<float>> rows((size_t)res * res);
        for (size_t t = 0; t < triCount; t++) {
            vec3 a = tris[t * 3].pos, b = tris[t * 3 + 1].pos, c = tris[t * 3 + 2].pos;
            float minU = std::min(a[u], std::min(b[u], c[u])), maxU = std::max(a[u], std::max(b[u], c[u]));
            float minV = std::min(a[v], std::min(b[v], c[v])), maxV = std::max(a[v], std::max(b[v], c[v]));
            int u0 = std::max(0, (int)std::ceil((minU - origin[u]) / cell)), u1 = std::min(res - 1, (int)std::floor((maxU - origin[u]) / cell));
            int v0 = std::max(0, (int)std::ceil((minV - origin[v]) / cell)), v1 = std::min(res - 1, (int)std::floor((maxV - origin[v]) / cell));
            float area = (b[u] - a[u]) * (c[v] - a[v]) - (c[u] - a[u]) * (b[v] - a[v]);
            if (std::abs(area) < 1e-20f) continue;
            for (int iv = v0; iv <= v1; iv++) for (int iu = u0; iu <= u1; iu++) {
                // Крошечный сдвиг луча, чтобы он не шёл ровно по ребру
                float pu = origin[u] + iu * cell + cell * 1.3e-3f, pv = origin[v] + iv * cell + cell * 0.7e-3f;
                float w0 = ((b[u] - pu) * (c[v] - pv) - (c[u] - pu) * (b[v] - pv)) / area;
                float w1 = ((c[u] - pu) * (a[v] - pv) - (a[u] - pu) * (c[v] - pv)) / area;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                rows[(size_t)iu + (size_t)res * iv].push_back(w0 * a[axis] + w1 * b[axis] + w2 * c[axis]);
            }
        }
        for (int iv = 0; iv < res; iv++) for (int iu = 0; iu < res; iu++) {
            std::vector<float>& hits = rows[(size_t)iu + (size_t)res * iv];
            if (hits.empty()) continue;
            std::sort(hits.begin(), hits.end());
            size_t k = 0;
            for (int ia = 0; ia < res; ia++) {
                float pa = origin[axis] + ia * cell;
                while (k < hits.size() && hits[k] < pa) k++;
                if (k & 1) {
                    glm::ivec3 g;
                    g[axis] = ia; g[u] = iu; g[v] = iv;
                    votes[index(g.x, g.y, g.z)]++;
                }
            }
        }
    }

    auto volume = std::make_shared<SdfVolume>();
    volume->resolution = res;
    volume->distances.resize(total);
    for (size_t i = 0; i < total; i++) {
        float d = (dist[i] == INF ? 2.0f * half : dist[i]) / half;   // В локальных единицах куба [-1, 1]
        volume->distances[i] = votes[i] >= 2 ? -d : d;
    }
    return volume;
}
