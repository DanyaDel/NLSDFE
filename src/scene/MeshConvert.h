#pragma once
#include "Common.h"
#include <vector>

struct OBJModel;

// SDF -> полигоны: поверхность набора объектов (с их CSG) строится методом surface nets.
// resolution — число ячеек по длинной стороне габаритов. Вершины ложатся вокруг outCenter
// (его же стоит сделать позицией нового узла), цвета берутся из материалов объектов.
bool sdfToMesh(const std::vector<GpuObject>& objects, int resolution, OBJModel& out, vec3& outCenter);

// Полигоны -> воксельное SDF. Вершины идут тройками (треугольник за треугольником), в осях меша.
// Сетка покрывает куб outCenter ± outHalf; знак (внутри/снаружи) определяется по лучам,
// поэтому меш должен быть замкнутым.
std::shared_ptr<SdfVolume> meshToVolume(const std::vector<Vertex>& triangles, int resolution, vec3& outCenter, float& outHalf);
