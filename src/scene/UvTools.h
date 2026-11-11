#pragma once
#include "Common.h"
#include <string>
#include <vector>

// Развёртка мешей. Вершины идут тройками: по три на треугольник.
enum UvProjection { UV_PLANAR_X = 0, UV_PLANAR_Y, UV_PLANAR_Z, UV_BOX, UV_SPHERE, UV_CYLINDER, UV_PROJECTION_COUNT };
const char* uvProjectionName(int projection);

void uvProject(std::vector<Vertex>& vertices, int projection);
void uvTransform(std::vector<Vertex>& vertices, vec2 offset, vec2 scale, float rotationDeg);   // Вокруг центра (0.5, 0.5)
void uvFit(std::vector<Vertex>& vertices);                                                     // Вписать в квадрат 0..1
bool writeOBJ(const std::string& path, const std::vector<Vertex>& vertices);
