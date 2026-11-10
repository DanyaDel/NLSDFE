#pragma once
#include "Common.h"
#include "Renderer.h"
#include <string>
#include <vector>

// Общая основа редактора и плеера: окно, рендерер, загрузка мешей
class App {
public:
    virtual ~App() = default;

    // Меши кэшируются по пути: один файл — один набор буферов на GPU
    int getOrLoadMesh(const std::string& path);

    bool framebufferResized = false;

protected:
    GLFWwindow* window = nullptr;
    Renderer renderer;

    // fixedSize: окно нельзя растянуть (замеры и скриншоты в тайловых WM)
    void initApp(const char* title, bool fixedSize);
    void shutdownApp();

    // Первый запуск: стартовое качество подбирается по типу видеокарты
    void applyFirstRunPreset();

    std::vector<uint8_t> captureThumbnail();            // Миниатюра сцены для .nos
    void writeScreenshot(const std::string& path);      // SDF-кадр в .ppm

private:
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};
