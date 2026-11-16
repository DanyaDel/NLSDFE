#include "Animation.h"
#include <algorithm>
#include <cmath>

namespace anim {

float length(const NodeAnimation& a) { return a.keys.empty() ? 0.0f : a.keys.back().time; }

Keyframe sample(const NodeAnimation& a, float time) {
    if (a.keys.empty()) return Keyframe();
    if (time <= a.keys.front().time) return a.keys.front();
    if (time >= a.keys.back().time) return a.keys.back();
    size_t next = 1;
    while (next < a.keys.size() - 1 && a.keys[next].time < time) next++;
    const Keyframe& k0 = a.keys[next - 1];
    const Keyframe& k1 = a.keys[next];
    float span = k1.time - k0.time;
    float t = span > 1e-6f ? (time - k0.time) / span : 1.0f;
    t = t * t * (3.0f - 2.0f * t);   // Плавный вход и выход из ключа
    Keyframe out;
    out.time = time;
    out.position = glm::mix(k0.position, k1.position, t);
    out.rotation = glm::mix(k0.rotation, k1.rotation, t);
    out.scale = glm::mix(k0.scale, k1.scale, t);
    return out;
}

int insertKey(NodeAnimation& a, const Keyframe& key) {
    for (size_t i = 0; i < a.keys.size(); i++) {
        if (std::fabs(a.keys[i].time - key.time) < 1e-3f) { a.keys[i] = key; return (int)i; }
        if (a.keys[i].time > key.time) { a.keys.insert(a.keys.begin() + i, key); return (int)i; }
    }
    a.keys.push_back(key);
    return (int)a.keys.size() - 1;
}

Keyframe keyFromNode(const SceneNode& node, float time) {
    Keyframe k;
    k.time = std::max(time, 0.0f);
    k.position = node.localPosition;
    k.rotation = node.localRotation;
    k.scale = node.localScale;
    return k;
}

void apply(SceneNode& node, float time) {
    if (node.animation.keys.empty()) return;
    Keyframe k = sample(node.animation, time);
    node.localPosition = k.position;
    node.localRotation = k.rotation;
    node.localScale = glm::max(glm::abs(k.scale), vec3(0.001f));
    node.body.sleeping = false;
}

void play(SceneNode& node, bool restart) {
    if (restart || node.animation.time >= length(node.animation)) node.animation.time = 0.0f;
    node.animation.playing = true;
}

void stop(SceneNode& node) { node.animation.playing = false; }

void update(float dt) {
    for (SceneNode& node : sceneGraph) {
        NodeAnimation& a = node.animation;
        if (!a.enabled || !a.playing || a.keys.size() < 2 || !node.worldActive) continue;
        float len = length(a);
        if (len <= 0.0f) continue;
        a.time += dt * a.speed;
        float pose = a.time;
        if (a.loop) {
            if (a.pingPong) {
                // Полный цикл — туда и обратно
                float cycle = std::fmod(a.time, len * 2.0f);
                if (cycle < 0.0f) cycle += len * 2.0f;
                pose = cycle <= len ? cycle : len * 2.0f - cycle;
            } else {
                pose = std::fmod(a.time, len);
                if (pose < 0.0f) pose += len;
                a.time = pose;
            }
        } else if (a.time >= len) {
            pose = a.time = len;
            a.playing = false;
        } else if (a.time < 0.0f) {
            pose = a.time = 0.0f;
            a.playing = false;
        }
        apply(node, pose);
    }
}

void startAll() {
    for (SceneNode& node : sceneGraph) {
        node.animation.time = 0.0f;
        node.animation.playing = node.animation.enabled && node.animation.playOnStart;
    }
}

}
