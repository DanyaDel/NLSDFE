#pragma once
#include <string>

// Оставляем только те объявления, которые нужны, хотя в Common.h они уже есть.
// Этот файл нужен скорее для совместимости с CMake списком файлов.
void saveNos(const std::string& path);
void loadNos(const std::string& path);
void loadMesh(const std::string& path);