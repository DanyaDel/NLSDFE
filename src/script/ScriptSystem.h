#pragma once
#include "Script.h"
#include "Common.h"
#include <functional>
#include <memory>
#include <filesystem>

namespace nls {

// То, что скриптам даёт приложение, а не сцена: ввод, звук, время
struct Host {
    std::function<bool(const std::string& key)> keyDown;      // "w", "space", "shift", "up", "mouse1" ...
    std::function<bool(const std::string& key)> keyPressed;   // Только в кадр нажатия
    std::function<std::string()> lastKey;                     // Имя клавиши, нажатой в этом кадре ("" — ни одной): для экрана переназначения
    std::function<void(int node)> playSound;                  // Звук компонента Audio Source узла
    std::function<void(int node)> stopSound;
    std::function<std::string(const std::string&)> resolvePath;   // Путь ассета относительно проекта
    float time = 0.0f;
    float delta = 0.0f;
};
extern Host host;

// Регистрирует в реестре всё, что движок открывает скриптам. Можно вызывать повторно.
void registerEngineBindings();

Type toScriptType(int scriptVarType);
int toScriptVarType(Type type);

// Компиляция с кэшем и жизнь экземпляров скриптов на узлах сцены
class ScriptSystem {
public:
    std::string rootDir;                       // Относительные пути скриптов считаются отсюда
    std::vector<std::string> console;          // Вывод log() и ошибки — для окна редактора

    std::string resolve(const std::string& path) const;

    // Собирает скрипт (или берёт из кэша, если файл не менялся). nullptr при ошибках.
    // Если исходника нет, пробует готовый байткод .nlsc рядом.
    std::shared_ptr<const Program> load(const std::string& path, std::vector<CompileError>* errors = nullptr);

    // Приводит node.script.vars в соответствие с объявлениями var в скрипте:
    // значения с тем же именем и типом сохраняются, новые получают значение по умолчанию.
    bool syncVars(SceneNode& node, std::vector<CompileError>* errors = nullptr);

    // --- Play Mode ---
    void start();
    void stop();
    void update(float dt);
    void fixedUpdate(float dt);
    void onCollide(int self, int other);                  // other = -1 — игрок
    void onTrigger(int trigger, int other, bool entered);
    // Сообщения между скриптами: on message(name: string, value: float) у получателя.
    // Доставляются после того, как все скрипты отработали кадр, — отправитель и получатель не мешают друг другу.
    void send(int node, const std::string& name, float value);
    void broadcast(const std::string& name, float value);
    void deliverMessages();

    void print(const std::string& line);

private:
    struct Cached {
        std::shared_ptr<const Program> program;
        std::filesystem::file_time_type time;
        std::vector<CompileError> errors;
        bool fromSource = false;
    };
    struct Instance {
        std::shared_ptr<const Program> program;
        VM vm;
        std::string path;
        std::filesystem::file_time_type seen{};   // Версия файла, которую уже пробовали собрать
        uint32_t generation = 0;                  // Поколение узла: место удалённого узла мог занять новый
        bool started = false;
        bool reported = false;
        int evUpdate = -1, evFixed = -1, evCollide = -1, evEnter = -1, evExit = -1, evMessage = -1;
    };

    std::unordered_map<std::string, Cached> cache;
    std::vector<std::unique_ptr<Instance>> instances;     // По индексу узла
    bool running = false;
    struct Message { int node; uint32_t generation; std::string name; float value; };
    std::vector<Message> mailbox;
    float reloadTimer = 0.0f;

    void ensureInstances();
    void bind(Instance& inst, int node, std::shared_ptr<const Program> program, const VM* previous);
    void invoke(int node, int Instance::*event, const Value* args, int argc);
    void report(Instance& inst, int node);
    void hotReload();
};
extern ScriptSystem scripts;

}
