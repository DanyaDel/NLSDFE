#pragma once
#include "Common.h"

void beginUIFrame();                 // ImGui::NewFrame и стиль
void renderUI(GLFWwindow* window);   // Весь интерфейс + ImGui::Render
bool uiNeedsAnimation();             // Есть ли на экране что-то, что меняется само (тосты)

void setupImGuiStyle();

// Действия редактора — общие для меню и горячих клавиш
void actionSaveScene(GLFWwindow* window);
void actionSaveSceneAs(GLFWwindow* window);
void actionOpenScene(GLFWwindow* window);
void actionSelectAll();
void actionPlay(GLFWwindow* window);   // Play Mode всегда играет сцену, с какой бы вкладки его ни запустили
