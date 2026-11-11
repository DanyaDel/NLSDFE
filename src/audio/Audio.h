#pragma once
#include "Common.h"
#include <string>

// Звук: тонкая обёртка над miniaudio. Без устройства вывода все вызовы тихо ничего не делают.
namespace audio {
    bool init();
    void shutdown();
    bool available();

    // Возвращает номер звука (0 — не удалось). spatial: громкость и панорама зависят от положения.
    int play(const std::string& file, float volume, bool loop, bool spatial, vec3 position = {0, 0, 0}, float range = 15.0f);
    void stop(int sound);
    void stopAll();
    bool isPlaying(int sound);
    void setPosition(int sound, vec3 position);
    void setVolume(int sound, float volume);
    void setListener(vec3 position, vec3 forward, vec3 up);
    void collectFinished();   // Освобождает доигравшие звуки; раз в кадр
}
