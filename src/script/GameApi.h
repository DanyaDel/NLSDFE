#pragma once
#include "Common.h"
#include <string>
#include <vector>

// Игровые сервисы для скриптов: интерфейс поверх игры (ui.*), плавные переходы (node.moveTo...),
// сохранения (save.*), смена сцен и управление временем. Состояние живёт здесь,
// рисует и исполняет его runtime (Game.cpp).
namespace nls::game {

enum HudKind { HUD_TEXT, HUD_RECT, HUD_BAR };

// Координаты и размеры — в долях экрана (0..1), чтобы интерфейс не зависел от разрешения
struct HudItem {
    int kind = HUD_TEXT;
    float x = 0, y = 0, w = 0, h = 0;
    float value = 0;       // HUD_BAR: заполнение 0..1; HUD_TEXT: размер текста (пиксели при высоте экрана 720)
    float textSize = 20;   // HUD_RECT с подписью: размер текста
    vec3 color = vec3(1.0f);
    std::string text;
};

struct State {
    std::vector<HudItem> hud;        // Заполняется скриптами каждый кадр
    std::string pendingScene;        // scene.load(): сцена сменится в конце кадра
    bool restartRequested = false;   // scene.restart()
    bool quitRequested = false;      // game.quit()
    float timeScale = 1.0f;
    int cursorVisible = -1;          // input.showCursor(): 1 / 0; -1 — как решил движок (скрыт при обзоре мышью)
    // Мышь в долях экрана — задаёт runtime
    float mouseX = 0, mouseY = 0, mouseDX = 0, mouseDY = 0;
    bool mouseDown = false, mousePressed = false;
    // Геймпад — задаёт runtime
    float padAxes[6] = {};
    bool padButtons[15] = {}, padButtonsBefore[15] = {};
};
extern State state;

void registerBindings();

// Действия ввода: имя -> клавиша ("jump" -> "space"). Меняются скриптом (input.bind) и сохраняются вместе с игрой.
bool actionDown(const std::string& action);
bool actionPressed(const std::string& action);
const std::string& actionKey(const std::string& action);   // Пусто — такого действия нет
void bindAction(const std::string& action, const std::string& key);

// Агенты, идущие по найденному пути (obj.navigateTo): раз в кадр, после updateSceneCaches()
void updateNavigation(float dt);
int activeAgents();

void reset();                 // Начало игры или смена сцены: переходы и запросы сбрасываются
void beginFrame();            // Перед скриптами: очищает список интерфейса
void updateTweens(float dt);  // После скриптов: двигает узлы к целям
int activeTweens();

// Сохранения: пары ключ-значение в текстовом файле
void setSaveFile(const std::string& path);   // Читает файл, если он есть
bool flushSave();                            // Пишет, если были изменения

}
