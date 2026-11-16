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
    void stopAll();           // Все звуки, кроме музыки
    bool isPlaying(int sound);
    void setPosition(int sound, vec3 position);
    void setVolume(int sound, float volume);
    void setListener(vec3 position, vec3 forward, vec3 up);
    void collectFinished();   // Освобождает доигравшие звуки; раз в кадр

    // Музыка: одна зацикленная дорожка, читается с диска потоком и переживает смену сцены.
    // Тот же файл повторно — ничего не меняется; другой — сменяет прежний.
    bool playMusic(const std::string& file, float volume);
    void stopMusic();
    const std::string& currentMusic();   // Пусто — музыка не играет

    // Общая громкость двух групп: музыка и все остальные звуки (0..2)
    void setMusicVolume(float volume);
    void setSfxVolume(float volume);
    float musicVolume();
    float sfxVolume();
}
