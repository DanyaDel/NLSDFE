#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"
#include "Audio.h"
#include <map>
#include <algorithm>
#include <memory>

namespace audio {

namespace {
    ma_engine engine;
    bool ready = false;
    int nextId = 1;
    std::map<int, std::unique_ptr<ma_sound>> sounds;

    // Две группы микшера: громкость каждой меняется независимо
    ma_sound_group sfxGroup, musicGroup;
    std::unique_ptr<ma_sound> music;
    std::string musicFile;
    float musicLevel = 1.0f, sfxLevel = 1.0f;
}

bool init() {
    if (ready) return true;
    ma_engine_config config = ma_engine_config_init();
    config.listenerCount = 1;
    if (ma_engine_init(&config, &engine) != MA_SUCCESS) {
        spdlog::warn("Audio: no output device, sound is disabled");
        return false;
    }
    ma_sound_group_init(&engine, 0, nullptr, &sfxGroup);
    ma_sound_group_init(&engine, 0, nullptr, &musicGroup);
    ma_sound_group_set_volume(&sfxGroup, sfxLevel);
    ma_sound_group_set_volume(&musicGroup, musicLevel);
    ready = true;
    return true;
}

void shutdown() {
    if (!ready) return;
    stopAll();
    stopMusic();
    ma_sound_group_uninit(&sfxGroup);
    ma_sound_group_uninit(&musicGroup);
    ma_engine_uninit(&engine);
    ready = false;
}

bool available() { return ready; }

int play(const std::string& file, float volume, bool loop, bool spatial, vec3 position, float range) {
    if (!ready || file.empty()) return 0;
    auto sound = std::make_unique<ma_sound>();
    ma_uint32 flags = MA_SOUND_FLAG_DECODE | (spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION);
    if (ma_sound_init_from_file(&engine, file.c_str(), flags, &sfxGroup, nullptr, sound.get()) != MA_SUCCESS) {
        spdlog::error("Audio: cannot load {}", file);
        return 0;
    }
    ma_sound_set_volume(sound.get(), volume);
    ma_sound_set_looping(sound.get(), loop ? MA_TRUE : MA_FALSE);
    if (spatial) {
        ma_sound_set_position(sound.get(), position.x, position.y, position.z);
        ma_sound_set_attenuation_model(sound.get(), ma_attenuation_model_linear);
        ma_sound_set_min_distance(sound.get(), 1.0f);
        ma_sound_set_max_distance(sound.get(), std::max(range, 1.1f));
    }
    ma_sound_start(sound.get());
    int id = nextId++;
    sounds[id] = std::move(sound);
    return id;
}

void stop(int id) {
    auto it = sounds.find(id);
    if (it == sounds.end()) return;
    ma_sound_uninit(it->second.get());
    sounds.erase(it);
}

void stopAll() {
    for (auto& [id, sound] : sounds) ma_sound_uninit(sound.get());
    sounds.clear();
}

bool isPlaying(int id) {
    auto it = sounds.find(id);
    return it != sounds.end() && ma_sound_is_playing(it->second.get());
}

void setPosition(int id, vec3 p) {
    auto it = sounds.find(id);
    if (it != sounds.end()) ma_sound_set_position(it->second.get(), p.x, p.y, p.z);
}

void setVolume(int id, float volume) {
    auto it = sounds.find(id);
    if (it != sounds.end()) ma_sound_set_volume(it->second.get(), volume);
}

void setListener(vec3 p, vec3 f, vec3 u) {
    if (!ready) return;
    ma_engine_listener_set_position(&engine, 0, p.x, p.y, p.z);
    ma_engine_listener_set_direction(&engine, 0, f.x, f.y, f.z);
    ma_engine_listener_set_world_up(&engine, 0, u.x, u.y, u.z);
}

void collectFinished() {
    for (auto it = sounds.begin(); it != sounds.end();) {
        if (ma_sound_at_end(it->second.get())) { ma_sound_uninit(it->second.get()); it = sounds.erase(it); }
        else ++it;
    }
}

bool playMusic(const std::string& file, float volume) {
    if (file.empty()) return false;
    if (music && musicFile == file) { ma_sound_set_volume(music.get(), volume); return true; }
    stopMusic();
    if (!ready) return false;
    auto sound = std::make_unique<ma_sound>();
    ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_file(&engine, file.c_str(), flags, &musicGroup, nullptr, sound.get()) != MA_SUCCESS) {
        spdlog::error("Audio: cannot load {}", file);
        return false;
    }
    ma_sound_set_volume(sound.get(), volume);
    ma_sound_set_looping(sound.get(), MA_TRUE);
    ma_sound_start(sound.get());
    music = std::move(sound);
    musicFile = file;
    return true;
}

void stopMusic() {
    if (music) ma_sound_uninit(music.get());
    music.reset();
    musicFile.clear();
}

const std::string& currentMusic() { return musicFile; }

void setMusicVolume(float volume) {
    musicLevel = std::min(std::max(volume, 0.0f), 2.0f);
    if (ready) ma_sound_group_set_volume(&musicGroup, musicLevel);
}

void setSfxVolume(float volume) {
    sfxLevel = std::min(std::max(volume, 0.0f), 2.0f);
    if (ready) ma_sound_group_set_volume(&sfxGroup, sfxLevel);
}

float musicVolume() { return musicLevel; }
float sfxVolume() { return sfxLevel; }

}
