#pragma once
#include <string>

// Глобальный указатель (определен в main.cpp)
extern void* g_engine;

// Функции, реализованные в main.cpp
// FIXED: Оставляем только IO, остальная логика теперь глобальная в Scene.cpp
void Engine_PublicSaveNos(const std::string& path);
void Engine_PublicLoadNos(const std::string& path);