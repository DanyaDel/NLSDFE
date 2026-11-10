#pragma once
#include <string>
#include <cstdint>

// Глобальный указатель (определен в main.cpp)
extern void* g_engine;

// Функции, реализованные в main.cpp
void Engine_PublicSaveNos(const std::string& path);
void Engine_PublicLoadNos(const std::string& path, bool append);
void Engine_LoadMesh(const std::string& path);
int Engine_GetOrLoadMesh(const std::string& path);   // Индекс в meshCache или -1
void* Engine_CreateUiTexture(const uint8_t* rgba, int w, int h);
void Engine_DestroyUiTexture(void* textureId);
bool Engine_ExportGame(const std::string& directory);
