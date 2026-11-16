#pragma once
#include "Common.h"

// Панели редактора, из которых собираются рабочие пространства

struct WorkArea { float x, y, w, h; };   // Место под главным меню

extern const char* NOS_FILTER;
extern const char* OBJ_FILTER;
extern const char* SCRIPT_FILTER;
extern const char* SOUND_FILTER;

void TextCentered(std::string text);
void addMenuItems(GLFWwindow* window);

void drawToolbar(float x, float y, float w, float h);
void drawHierarchyPanel(float x, float y, float w, float h);
void drawInspectorPanel(float x, float y, float w, float h, bool gameplay);   // gameplay: тело, логика, скрипт
void drawAssetBrowser(float x, float y, float w, float h);
void drawViewportOverlays();
void rescanAssets();

// Общая раскладка моделлера и сцены: тулбар, вьюпорт, иерархия, инспектор, ассеты
void drawEditingLayout(GLFWwindow* window, const WorkArea& area, bool gameplay);

// Режимы редактора (editor/workspaces/)
void drawModelWorkspace(GLFWwindow* window, const WorkArea& area);
void drawSceneWorkspace(GLFWwindow* window, const WorkArea& area);
void drawUvWorkspace(GLFWwindow* window, const WorkArea& area);
void drawScriptWorkspace(GLFWwindow* window, const WorkArea& area);
void drawUiWorkspace(GLFWwindow* window, const WorkArea& area);

// Компоненты Animation и Particles (workspaces/Timeline.cpp)
struct ImDrawList;
void drawAnimationComponent(SceneNode& node, int nodeIdx);
void drawParticlesComponent(SceneNode& node, int nodeIdx);
void drawEditorParticlePreview(ImDrawList* dl);
void drawTimelinePanel();           // Вкладка Timeline нижней панели: дорожки всех анимированных объектов
bool editorPreviewActive();          // Что-то проигрывается во вьюпорте — кадры нужны непрерывно
void stopEditorPreview();
void openScriptInEditor(const std::string& projectRelativePath);

// Окно File > Export > Game
extern bool showExportWindow;
void drawExportWindow(GLFWwindow* window);
