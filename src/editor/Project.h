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

// Рабочие пространства идут по ходу работы: принести материал -> сделать -> собрать игру
enum Workspace { WS_IMPORT = 0, WS_MODEL, WS_SCENE, WS_SCRIPT, WS_EXPORT, WS_COUNT };
extern Workspace currentWorkspace;
const char* workspaceName(Workspace ws);

// У моделлера и сцены свои документы; при смене вкладки нужный подставляется в sceneGraph.
// Все вкладки, кроме Model, работают со сценой.
void switchWorkspace(Workspace ws);
void newDocument();                // Чистый документ текущего вида: модель или сцена
bool editingModel();
std::string documentDirectory();   // Куда по умолчанию сохраняется текущий документ
