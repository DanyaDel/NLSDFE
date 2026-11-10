#pragma once
#include "Common.h"
#include <glm/gtx/intersect.hpp>
#include <glm/gtx/euler_angles.hpp>

// --- Math Utilities using GLM ---

inline mat4 identityMatrix() {
    return mat4(1.0f);
}

inline mat4 composeMatrix(vec3 p, vec3 r, vec3 s) {
    mat4 m = mat4(1.0f);
    m = glm::translate(m, p);
    m = m * glm::eulerAngleYXZ(r.y, r.x, r.z);
    m = glm::scale(m, s);
    return m;
}

// Чистый поворот из TRS-матрицы (столбцы без масштаба)
inline mat3 extractRotation(const mat4& m, vec3& outScale) {
    vec3 c0 = vec3(m[0]), c1 = vec3(m[1]), c2 = vec3(m[2]);
    outScale = { glm::length(c0), glm::length(c1), glm::length(c2) };
    if (outScale.x > 1e-6f) c0 /= outScale.x; else c0 = {1, 0, 0};
    if (outScale.y > 1e-6f) c1 /= outScale.y; else c1 = {0, 1, 0};
    if (outScale.z > 1e-6f) c2 /= outScale.z; else c2 = {0, 0, 1};
    return mat3(c0, c1, c2);
}

// Углы возвращаются в той же конвенции (YXZ), что использует composeMatrix
inline void decomposeMatrix(const mat4& m, vec3& outPos, vec3& outRot, vec3& outScale) {
    outPos = vec3(m[3]);
    mat3 r = extractRotation(m, outScale);
    float y, x, z;
    glm::extractEulerAngleYXZ(mat4(r), y, x, z);
    outRot = { x, y, z };
}

inline float intersectSphere(Ray r, vec3 pos, float rad) {
    float t;
    if (glm::intersectRaySphere(r.origin, r.dir, pos, rad, t)) return t;
    return -1.0f;
}

inline float intersectBox(Ray r, vec3 pos, vec3 size) {
    vec3 minB = pos - size;
    vec3 maxB = pos + size;
    float tmin, tmax;

    vec3 invDir = 1.0f / r.dir;
    vec3 t1 = (minB - r.origin) * invDir;
    vec3 t2 = (maxB - r.origin) * invDir;

    vec3 real_min = glm::min(t1, t2);
    vec3 real_max = glm::max(t1, t2);

    tmin = glm::max(glm::max(real_min.x, real_min.y), real_min.z);
    tmax = glm::min(glm::min(real_max.x, real_max.y), real_max.z);

    if (tmax < 0 || tmin > tmax) return -1.0f;
    return (tmin < 0) ? tmax : tmin;
}

// Луч против повёрнутого бокса: invRot переводит мир в локальные оси бокса
inline float intersectBoxOBB(Ray r, vec3 pos, const mat3& invRot, vec3 halfExtents) {
    Ray local = { invRot * (r.origin - pos), invRot * r.dir };
    return intersectBox(local, vec3(0.0f), halfExtents);
}

inline float intersectRing(Ray r, vec3 pos, int axis, float radius, float thick) {
    vec3 n = {0,0,0};
    if (axis == 0) n = {1,0,0};
    else if (axis == 1) n = {0,1,0};
    else if (axis == 2) n = {0,0,1};

    float denom = glm::dot(n, r.dir);
    if (std::abs(denom) < 1e-6) return 10000.0f;

    float t = glm::dot(pos - r.origin, n) / denom;
    if (t < 0.0f) return 10000.0f;

    vec3 p = r.origin + r.dir * t;
    float d = glm::length(p - pos);
    if (d > (radius - thick) && d < (radius + thick)) return t;
    return 10000.0f;
}
