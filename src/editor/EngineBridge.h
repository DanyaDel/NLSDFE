#pragma once
#include <string>
#include <cstdint>
#include <vector>

// Мост между интерфейсом и движком (main.cpp): всё, чему нужен рендерер или файлы
void saveNos(const std::string& path);
void loadNos(const std::string& path);
void appendNos(const std::string& path);
void loadMesh(const std::string& path);
void* createUiTexture(const uint8_t* rgba, int w, int h);
void destroyUiTexture(void* textureId);
bool exportGame(const std::string& directory);
const std::vector<std::string>& exportLog();   // Что попало в последнюю сборку и что пошло не так
