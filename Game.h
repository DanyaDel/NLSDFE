#pragma once
#include "Common.h"

// Play Mode: игрок от первого лица, SDF-коллизии, компоненты логики и триггеры
struct PlayState {
    bool active = false;
    bool standalone = false;   // Запущено как игра (без редактора): Esc закрывает окно
    vec3 pos = {0, 0, 0};      // Центр нижней сферы коллайдера
    vec3 vel = {0, 0, 0};
    vec3 spawn = {0, 0, 0};
    float yaw = 0.0f, pitch = 0.0f;
    bool grounded = false;
    double lastMouseX = 0.0, lastMouseY = 0.0;
};
extern PlayState play;

inline constexpr float PLAYER_RADIUS = 0.4f;
inline constexpr float PLAYER_EYE_HEIGHT = 0.8f;   // Глаза над центром нижней сферы

bool isPlayMode();
void startPlay(GLFWwindow* window);
void stopPlay(GLFWwindow* window);
void updatePlay(GLFWwindow* window, float deltaTime);
CameraBasis getPlayCameraBasis();
