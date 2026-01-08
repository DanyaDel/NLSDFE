#pragma once
#include "Common.h"
#include <cmath>
#include <algorithm>

inline vec3 operator+(vec3 a, vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
inline vec3 operator-(vec3 a, vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
inline vec3 operator*(vec3 a, float b) { return {a.x*b, a.y*b, a.z*b}; }
inline vec3 operator*(float b, vec3 a) { return {a.x*b, a.y*b, a.z*b}; }
inline vec3 operator*(vec3 a, vec3 b) { return {a.x*b.x, a.y*b.y, a.z*b.z}; }
inline vec3 operator-(vec3 v) { return {-v.x, -v.y, -v.z}; }
inline vec3& operator+=(vec3& a, vec3 b) { a.x+=b.x; a.y+=b.y; a.z+=b.z; return a; }
inline vec3& operator-=(vec3& a, vec3 b) { a.x-=b.x; a.y-=b.y; a.z-=b.z; return a; }

inline float dot(vec3 a, vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline vec3 cross(vec3 a, vec3 b) { return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x}; }
inline float length(vec3 v) { return std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z); }
inline vec3 normalize(vec3 v) { float l = length(v); if(l==0) return {0,0,0}; return {v.x/l, v.y/l, v.z/l}; }

// FIXED: Функция перенесена сюда из main.cpp
inline vec3 rotateVector(vec3 v, vec3 axis, float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1.0f - c);
}

inline mat4 createInverseRotationMatrix4x4(vec3 r) {
    float cx = cos(r.x), sx = sin(r.x);
    float cy = cos(r.y), sy = sin(r.y);
    float cz = cos(r.z), sz = sin(r.z);
    float m00 = cy * cz;             float m01 = cz * sx * sy - cx * sz;   float m02 = cx * cz * sy + sx * sz;
    float m10 = cy * sz;             float m11 = cx * cz + sx * sy * sz;   float m12 = -cz * sx + cx * sy * sz;
    float m20 = -sy;                 float m21 = cy * sx;                  float m22 = cx * cy;
    mat4 res;
    res.m[0] = m00; res.m[1] = m01; res.m[2] = m02; res.m[3] = 0.0f;
    res.m[4] = m10; res.m[5] = m11; res.m[6] = m12; res.m[7] = 0.0f;
    res.m[8] = m20; res.m[9] = m21; res.m[10]= m22; res.m[11]= 0.0f;
    res.m[12]= 0.0f; res.m[13]=0.0f; res.m[14]=0.0f; res.m[15]=1.0f;
    return res;
}

inline void createRotationMatrix3x3(vec3 r, float M[9]) {
    float cx = cos(r.x), sx = sin(r.x);
    float cy = cos(r.y), sy = sin(r.y);
    float cz = cos(r.z), sz = sin(r.z);
    M[0] = cy * cz;             M[1] = cz * sx * sy - cx * sz;   M[2] = cx * cz * sy + sx * sz;
    M[3] = cy * sz;             M[4] = cx * cz + sx * sy * sz;   M[5] = -cz * sx + cx * sy * sz;
    M[6] = -sy;                 M[7] = cy * sx;                  M[8] = cx * cy;
}

inline void transpose(float M[9], float T[9]) {
    T[0]=M[0]; T[1]=M[3]; T[2]=M[6]; T[3]=M[1]; T[4]=M[4]; T[5]=M[7]; T[6]=M[2]; T[7]=M[5]; T[8]=M[8];
}

inline vec3 mul(float M[9], vec3 v) {
    return { M[0]*v.x + M[1]*v.y + M[2]*v.z, M[3]*v.x + M[4]*v.y + M[5]*v.z, M[6]*v.x + M[7]*v.y + M[8]*v.z };
}

inline float intersectSphere(Ray r, vec3 pos, float rad) {
    vec3 oc = r.origin - pos;
    float b = dot(oc, r.dir);
    float c = dot(oc, oc) - rad*rad;
    float h = b*b - c;
    if(h < 0.0f) return -1.0f;
    return -b - std::sqrt(h);
}

inline float intersectBox(Ray r, vec3 pos, vec3 size) {
    vec3 ro = r.origin - pos;
    vec3 m = {1.0f/r.dir.x, 1.0f/r.dir.y, 1.0f/r.dir.z};
    vec3 n = m * ro;
    vec3 k = {std::abs(m.x)*size.x, std::abs(m.y)*size.y, std::abs(m.z)*size.z};
    vec3 t1 = -n - k; vec3 t2 = -n + k;
    float tN = std::max(std::max(t1.x, t1.y), t1.z);
    float tF = std::min(std::min(t2.x, t2.y), t2.z);
    if(tN > tF || tF < 0.0f) return -1.0f;
    return tN;
}

inline float intersectBoxOBB(Ray r, vec3 pos, vec3 rot, vec3 scale, float size) {
    float Mat[9], InvMat[9];
    createRotationMatrix3x3(rot, Mat);
    transpose(Mat, InvMat);
    vec3 rayOrigin = mul(InvMat, r.origin - pos);
    vec3 rayDir = mul(InvMat, r.dir);
    vec3 bound = vec3{size, size, size} * scale;
    vec3 invDir = {1.0f/rayDir.x, 1.0f/rayDir.y, 1.0f/rayDir.z};
    vec3 tMin = (-bound - rayOrigin) * invDir;
    vec3 tMax = (bound - rayOrigin) * invDir;
    vec3 t1 = {std::min(tMin.x, tMax.x), std::min(tMin.y, tMax.y), std::min(tMin.z, tMax.z)};
    vec3 t2 = {std::max(tMin.x, tMax.x), std::max(tMin.y, tMax.y), std::max(tMin.z, tMax.z)};
    float tN = std::max(std::max(t1.x, t1.y), t1.z);
    float tF = std::min(std::min(t2.x, t2.y), t2.z);
    if (tN > tF || tF < 0.0f) return -1.0f;
    return tN;
}

inline float intersectEllipsoid(Ray r, vec3 pos, vec3 rot, vec3 scale, float radius) {
    float Mat[9], InvMat[9];
    createRotationMatrix3x3(rot, Mat);
    transpose(Mat, InvMat);
    vec3 ro = mul(InvMat, r.origin - pos);
    vec3 rd = mul(InvMat, r.dir);
    ro = {ro.x/scale.x, ro.y/scale.y, ro.z/scale.z};
    rd = {rd.x/scale.x, rd.y/scale.y, rd.z/scale.z};
    vec3 oc = ro;
    float a = dot(rd, rd);
    float b = 2.0f * dot(oc, rd);
    float c = dot(oc, oc) - radius*radius;
    float h = b*b - 4.0f*a*c;
    if(h < 0.0f) return -1.0f;
    return (-b - std::sqrt(h)) / (2.0f*a);
}

inline float intersectRing(Ray r, vec3 pos, int axis, float radius, float thick) {
    vec3 n = {0,0,0};
    if (axis == 0) n = {1,0,0}; 
    if (axis == 1) n = {0,1,0}; 
    if (axis == 2) n = {0,0,1}; 
    float denom = dot(n, r.dir);
    if (std::abs(denom) < 1e-6) return 10000.0f;
    float t = dot(pos - r.origin, n) / denom;
    if (t < 0.0f) return 10000.0f;
    vec3 p = r.origin + r.dir * t;
    float d = length(p - pos);
    if (d > (radius - thick) && d < (radius + thick)) return t;
    return 10000.0f;
}