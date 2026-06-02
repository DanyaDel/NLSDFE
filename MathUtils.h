#pragma once
#include "Common.h"
#include <glm/gtx/intersect.hpp>
#include <glm/gtx/matrix_decompose.hpp>
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

inline void decomposeMatrix(const mat4& m, vec3& outPos, vec3& outRot, vec3& outScale) {
    glm::quat q;
    vec3 skew;
    vec4 perspective;
    glm::decompose(m, outScale, q, outPos, skew, perspective);
    outRot = glm::eulerAngles(q);
}

inline mat4 createInverseRotationMatrix4x4(vec3 r) {
    return glm::transpose(glm::eulerAngleYXZ(r.y, r.x, r.z));
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

inline float intersectBoxOBB(Ray r, vec3 pos, vec3 rot, vec3 scale, float size) {
    mat4 model = composeMatrix(pos, rot, scale * size);
    mat4 invModel = glm::inverse(model);
    
    vec3 localRo = vec3(invModel * vec4(r.origin, 1.0f));
    vec3 localRd = glm::normalize(vec3(invModel * vec4(r.dir, 0.0f)));
    
    Ray localRay = { localRo, localRd };
    float t = intersectBox(localRay, vec3(0.0f), vec3(1.0f));
    if (t > 0.0f) {
        vec3 hitPos = localRo + localRd * t;
        vec4 worldHitPos = model * vec4(hitPos, 1.0f);
        return glm::length(vec3(worldHitPos) - r.origin);
    }
    return -1.0f;
}

inline float intersectEllipsoid(Ray r, vec3 pos, vec3 rot, vec3 scale, float radius) {
    mat4 model = composeMatrix(pos, rot, scale);
    mat4 invModel = glm::inverse(model);
    
    vec3 localRo = vec3(invModel * vec4(r.origin, 1.0f));
    vec3 localRd = glm::normalize(vec3(invModel * vec4(r.dir, 0.0f)));
    
    float t;
    if (glm::intersectRaySphere(localRo, localRd, vec3(0.0f), radius, t)) {
        vec3 hitPos = localRo + localRd * t;
        vec4 worldHitPos = model * vec4(hitPos, 1.0f);
        return glm::length(vec3(worldHitPos) - r.origin);
    }
    return -1.0f;
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
