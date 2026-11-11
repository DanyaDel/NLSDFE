#pragma once
#include "Common.h"

// Play Mode: сцена оживает — физика, скрипты, звук, персонажи.
// Смотрим через узел-камеру; управляются только узлы с компонентом Character Controller.
struct PlayState {
    bool active = false;
    bool standalone = false;   // Запущено как игра (без редактора): Esc закрывает окно
    bool mouseLook = true;     // Выключается в автоматических прогонах плеера
    int cameraNode = -1;       // Узел-камера; -1 — в сцене её нет, показываем вид редактора
    bool cursorCaptured = false;
    double lastMouseX = 0.0, lastMouseY = 0.0;
    int mouseSettle = 0;       // Кадры после захвата курсора, в которые его скачок игнорируется
};
extern PlayState play;

bool isPlayMode();
void startPlay(GLFWwindow* window);
void stopPlay(GLFWwindow* window);
void updatePlay(GLFWwindow* window, float deltaTime);

// Интерфейс поверх игры (ImGui-кадр уже начат, ImGui::Render вызывает приложение)
void drawPlayHud();
void drawToast();
