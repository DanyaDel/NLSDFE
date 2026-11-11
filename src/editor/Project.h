#pragma once
#include "Common.h"
#include <string>
#include <vector>

// Проект — папка с ассетами, сценами и скриптами:
//   <root>/assets/   модели .nos и меши
//   <root>/scenes/   уровни .nos
//   <root>/scripts/  скрипты .nls
//   <root>/project.nlsproj
struct Project {
    std::string root;
    std::string name = "My Game";
    int exportPreset = -1;         // Пресет качества для игры; -1 — как в редакторе
    std::string lastExportDir;

    std::string path(const std::string& relative) const;      // Абсолютный путь внутри проекта
    std::string relative(const std::string& absolute) const;  // Путь относительно root (или как есть, если файл снаружи)
    std::vector<std::string> list(const std::string& subdir, const std::string& extension) const;   // Имена файлов, по алфавиту
};
extern Project project;

bool openProject(const std::string& directory);   // Создаёт недостающие папки
void saveProject();
// Копирует внешний файл в папку проекта; возвращает путь внутри проекта или "" при ошибке
std::string importIntoProject(const std::string& file, const std::string& subdir);

// Режимы редактора (панель под главным меню)
enum Workspace { WS_MODEL = 0, WS_SCENE, WS_UV, WS_SCRIPT, WS_COUNT };
extern Workspace currentWorkspace;
const char* workspaceName(Workspace ws);
inline bool workspaceHasViewport(Workspace ws) { return ws != WS_SCRIPT; }

// У моделлера и сцены свои документы; при смене вкладки нужный подставляется в sceneGraph.
// Все режимы, кроме Model Editor, работают со сценой.
void switchWorkspace(Workspace ws);
void newDocument();                // Чистый документ текущего вида: модель или сцена
bool editingModel();
std::string documentDirectory();   // Куда по умолчанию сохраняется текущий документ
