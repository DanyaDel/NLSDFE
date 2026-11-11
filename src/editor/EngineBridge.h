#pragma once
#include <string>
#include <cstdint>
#include <vector>

// Мост между интерфейсом и движком (main.cpp): всё, чему нужен рендерер или файлы
void saveNos(const std::string& path);
void loadNos(const std::string& path);
void appendNos(const std::string& path);
void loadMesh(const std::string& path);         // Новый объект с мешем, отмасштабированным под сцену
int loadMeshFile(const std::string& path);     // Только загрузить меш: индекс в meshCache или -1
void* createUiTexture(const uint8_t* rgba, int w, int h);
void destroyUiTexture(void* textureId);
bool exportGame(const std::string& directory);
const std::vector<std::string>& exportLog();
void updateMesh(int meshIdx);                  // Перезалить вершины меша в GPU
bool saveScreenshot(const std::string& path);  // SDF-кадр в .ppm   // Что попало в последнюю сборку и что пошло не так
