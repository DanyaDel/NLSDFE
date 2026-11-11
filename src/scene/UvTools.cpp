#include "UvTools.h"
#include <cmath>
#include <algorithm>
#include <fstream>

const char* uvProjectionName(int projection) {
    static const char* names[] = { "Planar X", "Planar Y", "Planar Z", "Box", "Sphere", "Cylinder" };
    return names[std::clamp(projection, 0, (int)UV_PROJECTION_COUNT - 1)];
}

namespace {
    void bounds(const std::vector<Vertex>& vertices, vec3& lo, vec3& hi) {
        lo = vec3(1e10f); hi = vec3(-1e10f);
        for (const auto& v : vertices) { lo = glm::min(lo, v.pos); hi = glm::max(hi, v.pos); }
    }

    // Координаты точки в плоскости, перпендикулярной оси, нормированные габаритами
    vec2 planar(vec3 p, int axis) {
        if (axis == 0) return vec2(p.z, p.y);
        if (axis == 1) return vec2(p.x, p.z);
        return vec2(p.x, p.y);
    }
}

void uvProject(std::vector<Vertex>& vertices, int projection) {
    if (vertices.empty()) return;
    vec3 lo, hi;
    bounds(vertices, lo, hi);
    vec3 size = glm::max(hi - lo, vec3(1e-6f));
    vec3 center = (lo + hi) * 0.5f;
    const float pi = 3.14159265f;

    if (projection <= UV_PLANAR_Z) {
        for (auto& v : vertices) v.uv = planar((v.pos - lo) / size, projection);
        return;
    }

    if (projection == UV_BOX) {
        // Каждый треугольник проецируется вдоль своей главной оси; три направления
        // раскладываются по третям полосы, чтобы острова не накладывались
        for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
            vec3 n = glm::abs(glm::cross(vertices[i + 1].pos - vertices[i].pos, vertices[i + 2].pos - vertices[i].pos));
            int axis = (n.x >= n.y && n.x >= n.z) ? 0 : (n.y >= n.z ? 1 : 2);
            for (int k = 0; k < 3; k++) {
                vec2 uv = planar((vertices[i + k].pos - lo) / size, axis);
                vertices[i + k].uv = vec2((uv.x + (float)axis) / 3.0f, uv.y);
            }
        }
        return;
    }

    // Сфера и цилиндр: угол вокруг вертикальной оси. Шов разворачивается потреугольно,
    // иначе треугольники, пересекающие его, растянулись бы через всю текстуру.
    for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
        float u[3];
        for (int k = 0; k < 3; k++) {
            vec3 d = (vertices[i + k].pos - center) / (size * 0.5f);
            u[k] = std::atan2(d.z, d.x) / (2.0f * pi) + 0.5f;
            float v;
            if (projection == UV_SPHERE) {
                float len = glm::length(d);
                v = len > 1e-6f ? std::acos(std::clamp(-d.y / len, -1.0f, 1.0f)) / pi : 0.5f;
            } else v = d.y * 0.5f + 0.5f;
            vertices[i + k].uv.y = v;
        }
        float maxU = std::max(u[0], std::max(u[1], u[2]));
        for (int k = 0; k < 3; k++) {
            if (maxU - u[k] > 0.5f) u[k] += 1.0f;
            vertices[i + k].uv.x = u[k];
        }
    }
}

void uvTransform(std::vector<Vertex>& vertices, vec2 offset, vec2 scale, float rotationDeg) {
    float a = glm::radians(rotationDeg), c = std::cos(a), s = std::sin(a);
    const vec2 pivot(0.5f);
    for (auto& v : vertices) {
        vec2 p = (v.uv - pivot) * scale;
        v.uv = vec2(p.x * c - p.y * s, p.x * s + p.y * c) + pivot + offset;
    }
}

void uvFit(std::vector<Vertex>& vertices) {
    if (vertices.empty()) return;
    vec2 lo(1e10f), hi(-1e10f);
    for (const auto& v : vertices) { lo = glm::min(lo, v.uv); hi = glm::max(hi, v.uv); }
    vec2 size = glm::max(hi - lo, vec2(1e-6f));
    for (auto& v : vertices) v.uv = (v.uv - lo) / size;
}

bool writeOBJ(const std::string& path, const std::vector<Vertex>& vertices) {
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << "# Exported by NLSDFE\n";
    for (const auto& v : vertices) f << "v " << v.pos.x << " " << v.pos.y << " " << v.pos.z << "\n";
    for (const auto& v : vertices) f << "vt " << v.uv.x << " " << v.uv.y << "\n";
    for (const auto& v : vertices) f << "vn " << v.normal.x << " " << v.normal.y << " " << v.normal.z << "\n";
    for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
        f << "f";
        for (size_t k = i + 1; k <= i + 3; k++) f << " " << k << "/" << k << "/" << k;
        f << "\n";
    }
    return (bool)f;
}
