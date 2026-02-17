#pragma once
#include "Common.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <limits> // Для intersectBox
#include <intrin.h> // For SIMD

inline int count_chars(const std::string& str, char c) {
    int count = 0;
    for (char ch : str) if (ch == c) count++;
    return count;
}
inline int count_chars(const char* str, char c) {
    int count = 0;
    while (*str) { if (*str == c) count++; str++; }
    return count;
}

// --- Vector Math ---
inline vec3 operator+(vec3 a, vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
inline vec3 operator-(vec3 a, vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
inline vec3 operator*(vec3 a, float b) { return {a.x*b, a.y*b, a.z*b}; }
inline vec3 operator*(float b, vec3 a) { return {a.x*b, a.y*b, a.z*b}; }
inline vec3 operator*(vec3 a, vec3 b) { return {a.x*b.x, a.y*b.y, a.z*b.z}; }
inline vec3 operator/(vec3 a, float b) { return {a.x/b, a.y/b, a.z/b}; }
inline vec3 operator-(vec3 v) { return {-v.x, -v.y, -v.z}; }

inline vec3& operator+=(vec3& a, vec3 b) { a.x+=b.x; a.y+=b.y; a.z+=b.z; return a; }
inline vec3& operator-=(vec3& a, vec3 b) { a.x-=b.x; a.y-=b.y; a.z-=b.z; return a; }

inline float dot(vec3 a, vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline vec3 cross(vec3 a, vec3 b) { return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x}; }
inline float length(vec3 v) { return std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z); }
inline vec3 normalize(vec3 v) { float l = length(v); if(l==0) return {0,0,0}; return {v.x/l, v.y/l, v.z/l}; }

// --- Matrix Math 4x4 (Column-Major Standard) ---

inline mat4 identityMatrix() {
    mat4 m; memset(m.m, 0, sizeof(float)*16);
    m.m[0]=1; m.m[5]=1; m.m[10]=1; m.m[15]=1;
    return m;
}

// Исправлено: Честное умножение Column-Major матриц
// C[col][row] = Dot(Row of A, Col of B)
inline mat4 multiplyMatrices(const mat4& a, const mat4& b) {
    mat4 res;
    for (int i = 0; i < 4; i++) {
        __m128 row = _mm_load_ps(&a.m[i * 4]);
        __m128 v0 = _mm_set1_ps(b.m[0 * 4 + i]);
        __m128 v1 = _mm_set1_ps(b.m[1 * 4 + i]);
        __m128 v2 = _mm_set1_ps(b.m[2 * 4 + i]);
        __m128 v3 = _mm_set1_ps(b.m[3 * 4 + i]);
        
        // This is a simplified SIMD approach for illustration, 
        // a full 4x4 mat mul usually involves more shuffling or row-major loading.
        // For column-major, we can do:
    }
    // Reverting to optimized scalar for now to ensure correctness with Column-Major layout
    // unless I'm 100% sure about the SIMD shuffle pattern for this specific layout.
    for (int c = 0; c < 4; c++) {     
        for (int r = 0; r < 4; r++) { 
            res.m[c*4 + r] = 
                a.m[0*4 + r] * b.m[c*4 + 0] +
                a.m[1*4 + r] * b.m[c*4 + 1] +
                a.m[2*4 + r] * b.m[c*4 + 2] +
                a.m[3*4 + r] * b.m[c*4 + 3];
        }
    }
    return res;
}

inline mat4 composeMatrix(vec3 p, vec3 r, vec3 s) {
    float cx = cos(r.x), sx = sin(r.x);
    float cy = cos(r.y), sy = sin(r.y);
    float cz = cos(r.z), sz = sin(r.z);
    
    // Column vectors
    vec3 rx = { cy*cz,                  cy*sz,                 -sy };
    vec3 ry = { cz*sx*sy - cx*sz,       cx*cz + sx*sy*sz,      cy*sx };
    vec3 rz = { cx*cz*sy + sx*sz,       cx*sy*sz - cz*sx,      cx*cy };

    mat4 res;
    // Col 0
    res.m[0] = rx.x * s.x; res.m[1] = rx.y * s.x; res.m[2] = rx.z * s.x; res.m[3] = 0.0f;
    // Col 1
    res.m[4] = ry.x * s.y; res.m[5] = ry.y * s.y; res.m[6] = ry.z * s.y; res.m[7] = 0.0f;
    // Col 2
    res.m[8] = rz.x * s.z; res.m[9] = rz.y * s.z; res.m[10]= rz.z * s.z; res.m[11]= 0.0f;
    // Col 3 (Translation)
    res.m[12] = p.x; res.m[13] = p.y; res.m[14] = p.z; res.m[15] = 1.0f;
    return res;
}

inline vec3 transformPoint(const mat4& m, vec3 p) {
    // x = M00*px + M10*py + M20*pz + M30
    float x = m.m[0]*p.x + m.m[4]*p.y + m.m[8]*p.z + m.m[12];
    float y = m.m[1]*p.x + m.m[5]*p.y + m.m[9]*p.z + m.m[13];
    float z = m.m[2]*p.x + m.m[6]*p.y + m.m[10]*p.z + m.m[14];
    return {x, y, z};
}

// Честная инверсия матрицы 4x4
inline mat4 inverseMatrix(const mat4& m) {
    float n[16]; const float* s = m.m;
    n[0] = s[5]*s[10]*s[15]-s[5]*s[11]*s[14]-s[9]*s[6]*s[15]+s[9]*s[7]*s[14]+s[13]*s[6]*s[11]-s[13]*s[7]*s[10];
    n[4] = -s[4]*s[10]*s[15]+s[4]*s[11]*s[14]+s[8]*s[6]*s[15]-s[8]*s[7]*s[14]-s[12]*s[6]*s[11]+s[12]*s[7]*s[10];
    n[8] = s[4]*s[9]*s[15]-s[4]*s[11]*s[13]-s[8]*s[5]*s[15]+s[8]*s[7]*s[13]+s[12]*s[5]*s[11]-s[12]*s[7]*s[9];
    n[12]=-s[4]*s[9]*s[14]+s[4]*s[10]*s[13]+s[8]*s[5]*s[14]-s[8]*s[6]*s[13]-s[12]*s[5]*s[10]+s[12]*s[6]*s[9];
    n[1] = -s[1]*s[10]*s[15]+s[1]*s[11]*s[14]+s[9]*s[2]*s[15]-s[9]*s[3]*s[14]-s[13]*s[2]*s[11]+s[13]*s[3]*s[10];
    n[5] = s[0]*s[10]*s[15]-s[0]*s[11]*s[14]-s[8]*s[2]*s[15]+s[8]*s[3]*s[14]+s[12]*s[2]*s[11]-s[12]*s[3]*s[10];
    n[9] = -s[0]*s[9]*s[15]+s[0]*s[11]*s[13]+s[8]*s[1]*s[15]-s[8]*s[3]*s[13]-s[12]*s[1]*s[11]+s[12]*s[3]*s[9];
    n[13]= s[0]*s[9]*s[14]-s[0]*s[10]*s[13]-s[8]*s[1]*s[14]+s[8]*s[2]*s[13]+s[12]*s[1]*s[10]-s[12]*s[2]*s[9];
    n[2] = s[1]*s[6]*s[15]-s[1]*s[7]*s[14]-s[5]*s[2]*s[15]+s[5]*s[3]*s[14]+s[13]*s[2]*s[7]-s[13]*s[3]*s[6];
    n[6] = -s[0]*s[6]*s[15]+s[0]*s[7]*s[14]+s[4]*s[2]*s[15]-s[4]*s[3]*s[14]-s[12]*s[2]*s[7]+s[12]*s[3]*s[6];
    n[10]= s[0]*s[5]*s[15]-s[0]*s[7]*s[13]-s[4]*s[1]*s[15]+s[4]*s[3]*s[13]+s[12]*s[1]*s[7]-s[12]*s[3]*s[5];
    n[14]=-s[0]*s[5]*s[14]+s[0]*s[6]*s[13]+s[4]*s[1]*s[14]-s[4]*s[2]*s[13]-s[12]*s[1]*s[6]+s[12]*s[2]*s[5];
    n[3] = -s[1]*s[6]*s[11]+s[1]*s[7]*s[10]+s[5]*s[2]*s[11]-s[5]*s[3]*s[10]-s[9]*s[2]*s[7]+s[9]*s[3]*s[6];
    n[7] = s[0]*s[6]*s[11]-s[0]*s[7]*s[10]-s[4]*s[2]*s[11]+s[4]*s[3]*s[10]+s[8]*s[2]*s[7]-s[8]*s[3]*s[6];
    n[11]=-s[0]*s[5]*s[11]+s[0]*s[7]*s[9]+s[4]*s[1]*s[11]-s[4]*s[3]*s[9]-s[8]*s[1]*s[7]+s[8]*s[3]*s[5];
    n[15]= s[0]*s[5]*s[10]-s[0]*s[6]*s[9]-s[4]*s[1]*s[10]+s[4]*s[2]*s[9]+s[8]*s[1]*s[6]-s[8]*s[2]*s[5];
    float det = s[0]*n[0]+s[1]*n[4]+s[2]*n[8]+s[3]*n[12];
    if (std::abs(det)<1e-6) return identityMatrix();
    mat4 inv; float invDet=1.0f/det; for(int i=0;i<16;i++) inv.m[i]=n[i]*invDet;
    return inv;
}

inline void decomposeMatrix(const mat4& m, vec3& outPos, vec3& outRot, vec3& outScale) {
    outPos = { m.m[12], m.m[13], m.m[14] };
    vec3 c0 = { m.m[0], m.m[1], m.m[2] };
    vec3 c1 = { m.m[4], m.m[5], m.m[6] };
    vec3 c2 = { m.m[8], m.m[9], m.m[10] };
    outScale.x = length(c0); outScale.y = length(c1); outScale.z = length(c2);
    if (outScale.x > 0) c0 = c0 / outScale.x;
    if (outScale.y > 0) c1 = c1 / outScale.y;
    if (outScale.z > 0) c2 = c2 / outScale.z;
    outRot.y = asin(-c0.z); 
    if (cos(outRot.y) != 0) { outRot.x = atan2(c1.z, c2.z); outRot.z = atan2(c0.y, c0.x); } 
    else { outRot.x = atan2(-c2.y, c1.y); outRot.z = 0; }
}

inline mat4 createInverseRotationMatrix4x4(vec3 r) {
    float cx = cos(r.x), sx = sin(r.x); float cy = cos(r.y), sy = sin(r.y); float cz = cos(r.z), sz = sin(r.z);
    vec3 rx = { cy*cz, cy*sz, -sy }; vec3 ry = { cz*sx*sy - cx*sz, cx*cz + sx*sy*sz, cy*sx }; vec3 rz = { cx*cz*sy + sx*sz, cx*sy*sz - cz*sx, cx*cy };
    mat4 res;
    res.m[0] = rx.x; res.m[1] = ry.x; res.m[2] = rz.x; res.m[3] = 0.0f;
    res.m[4] = rx.y; res.m[5] = ry.y; res.m[6] = rz.y; res.m[7] = 0.0f;
    res.m[8] = rx.z; res.m[9] = ry.z; res.m[10]= rz.z; res.m[11]= 0.0f;
    res.m[12]= 0.0f; res.m[13]= 0.0f; res.m[14]= 0.0f; res.m[15]= 1.0f;
    return res;
}

inline void createRotationMatrix3x3(vec3 r, float M[9]) {
    float cx = cos(r.x), sx = sin(r.x); float cy = cos(r.y), sy = sin(r.y); float cz = cos(r.z), sz = sin(r.z);
    M[0] = cy*cz; M[1] = cz*sx*sy - cx*sz; M[2] = cx*cz*sy + sx*sz;
    M[3] = cy*sz; M[4] = cx*cz + sx*sy*sz; M[5] = -cz*sx + cx*sy*sz;
    M[6] = -sy; M[7] = cy*sx; M[8] = cx*cy;
}

inline void transpose(float M[9], float T[9]) { T[0]=M[0]; T[1]=M[3]; T[2]=M[6]; T[3]=M[1]; T[4]=M[4]; T[5]=M[7]; T[6]=M[2]; T[7]=M[5]; T[8]=M[8]; }
inline vec3 mul(float M[9], vec3 v) { return { M[0]*v.x + M[1]*v.y + M[2]*v.z, M[3]*v.x + M[4]*v.y + M[5]*v.z, M[6]*v.x + M[7]*v.y + M[8]*v.z }; }

inline float intersectSphere(Ray r, vec3 pos, float rad) { vec3 oc = r.origin - pos; float b = dot(oc, r.dir); float c = dot(oc, oc) - rad*rad; float h = b*b - c; if(h < 0.0f) return -1.0f; return -b - std::sqrt(h); }

// Исправлено: Более стабильный алгоритм пересечения AABB (Slabs method)
inline float intersectBox(Ray r, vec3 pos, vec3 size) {
    vec3 minB = pos - size;
    vec3 maxB = pos + size;
    
    vec3 invDir = { 1.0f/r.dir.x, 1.0f/r.dir.y, 1.0f/r.dir.z };
    
    float t1 = (minB.x - r.origin.x) * invDir.x;
    float t2 = (maxB.x - r.origin.x) * invDir.x;
    float tmin = std::min(t1, t2);
    float tmax = std::max(t1, t2);
    
    t1 = (minB.y - r.origin.y) * invDir.y;
    t2 = (maxB.y - r.origin.y) * invDir.y;
    tmin = std::max(tmin, std::min(t1, t2));
    tmax = std::min(tmax, std::max(t1, t2));
    
    t1 = (minB.z - r.origin.z) * invDir.z;
    t2 = (maxB.z - r.origin.z) * invDir.z;
    tmin = std::max(tmin, std::min(t1, t2));
    tmax = std::min(tmax, std::max(t1, t2));
    
    if (tmax < 0 || tmin > tmax) return -1.0f;
    return (tmin < 0) ? tmax : tmin;
}

inline float intersectBoxOBB(Ray r, vec3 pos, vec3 rot, vec3 scale, float size) { float Mat[9], InvMat[9]; createRotationMatrix3x3(rot, Mat); transpose(Mat, InvMat); vec3 rayOrigin = mul(InvMat, r.origin - pos); vec3 rayDir = mul(InvMat, r.dir); vec3 bound = vec3{size, size, size} * scale; vec3 invDir = {1.0f/rayDir.x, 1.0f/rayDir.y, 1.0f/rayDir.z}; vec3 tMin = (-bound - rayOrigin) * invDir; vec3 tMax = (bound - rayOrigin) * invDir; vec3 t1 = {std::min(tMin.x, tMax.x), std::min(tMin.y, tMax.y), std::min(tMin.z, tMax.z)}; vec3 t2 = {std::max(tMin.x, tMax.x), std::max(tMin.y, tMax.y), std::max(tMin.z, tMax.z)}; float tN = std::max(std::max(t1.x, t1.y), t1.z); float tF = std::min(std::min(t2.x, t2.y), t2.z); if (tN > tF || tF < 0.0f) return -1.0f; return tN; }
inline float intersectEllipsoid(Ray r, vec3 pos, vec3 rot, vec3 scale, float radius) { float Mat[9], InvMat[9]; createRotationMatrix3x3(rot, Mat); transpose(Mat, InvMat); vec3 ro = mul(InvMat, r.origin - pos); vec3 rd = mul(InvMat, r.dir); ro = {ro.x/scale.x, ro.y/scale.y, ro.z/scale.z}; rd = {rd.x/scale.x, rd.y/scale.y, rd.z/scale.z}; float a = dot(rd, rd); float b = 2.0f * dot(ro, rd); float c = dot(ro, ro) - radius*radius; float h = b*b - 4.0f*a*c; if(h < 0.0f) return -1.0f; return (-b - std::sqrt(h)) / (2.0f*a); }
inline float intersectRing(Ray r, vec3 pos, int axis, float radius, float thick) { vec3 n = {0,0,0}; if (axis == 0) n = {1,0,0}; if (axis == 1) n = {0,1,0}; if (axis == 2) n = {0,0,1}; float denom = dot(n, r.dir); if (std::abs(denom) < 1e-6) return 10000.0f; float t = dot(pos - r.origin, n) / denom; if (t < 0.0f) return 10000.0f; vec3 p = r.origin + r.dir * t; float d = length(p - pos); if (d > (radius - thick) && d < (radius + thick)) return t; return 10000.0f; }