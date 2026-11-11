#pragma once
#include "VisualScript.h"
#include <string>

// Холст визуального скрипта (.nlsg) для Script Editor
namespace grapheditor {
    bool open(const std::string& fullPath);
    bool save();
    bool createDefault(const std::string& fullPath);   // Новый граф-пример: крутит объект

    void drawCanvas();     // Внутри текущего окна, занимает всё доступное место
    void drawSidebar();    // Переменные и палитра узлов

    bool modified();
    bool compiledOk();
    const std::vector<nls::CompileError>& problems();
    const std::string& generatedSource();
}
