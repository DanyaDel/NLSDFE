#pragma once
#include "Common.h"

// Панели редактора, из которых собираются рабочие пространства

struct WorkArea { float x, y, w, h; };   // Место под главным меню

extern const char* NOS_FILTER;
extern const char* OBJ_FILTER;
extern const char* IMPORT_FILTER;

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

// Рабочие пространства (editor/workspaces/)
void drawImportWorkspace(GLFWwindow* window, const WorkArea& area);
void drawModelWorkspace(GLFWwindow* window, const WorkArea& area);
void drawSceneWorkspace(GLFWwindow* window, const WorkArea& area);
void drawScriptWorkspace(GLFWwindow* window, const WorkArea& area);
void drawExportWorkspace(GLFWwindow* window, const WorkArea& area);
void openScriptInEditor(const std::string& projectRelativePath);

