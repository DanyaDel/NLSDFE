#include "Particles.h"
#include "Scene.h"
#include "MathUtils.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace particles {

std::vector<Particle> pool;

namespace {
    std::map<int, float> debt;   // Дробная часть «частиц в секунду» по узлам
    uint32_t seed = 0x9E3779B9u;

    float rnd() {
        seed = seed * 1664525u + 1013904223u;
        return (float)(seed >> 8) / 16777216.0f;
    }

    void spawn(int index, int count) {
        if (index < 0 || index >= (int)sceneGraph.size() || count <= 0) return;
        const SceneNode& node = sceneGraph[index];
        const ParticleEmitter& e = node.emitter;
        mat4 world = calculateWorldMatrix(index);
        vec3 scale;
        mat3 rot = extractRotation(world, scale);
        vec3 origin = vec3(world[3]);
        for (int i = 0; i < count && (int)pool.size() < MAX_PARTICLES; i++) {
            // Равномерно внутри конуса вокруг +Y
            float cosMax = std::cos(std::clamp(e.spread, 0.0f, 3.14159f));
            float cosA = 1.0f - rnd() * (1.0f - cosMax);
            float sinA = std::sqrt(std::max(0.0f, 1.0f - cosA * cosA));
            float turn = rnd() * 6.2831853f;
            vec3 dir = rot * vec3(sinA * std::cos(turn), cosA, sinA * std::sin(turn));
            Particle p;
            p.position = origin;
            p.velocity = dir * e.speed * (0.75f + 0.5f * rnd());
            p.life = std::max(e.lifetime * (0.75f + 0.5f * rnd()), 0.02f);
            p.sizeStart = e.sizeStart; p.sizeEnd = e.sizeEnd;
            p.colorStart = e.colorStart; p.colorEnd = e.colorEnd;
            p.gravityScale = e.gravityScale;
            p.collide = e.collide;
            p.lit = e.lit;
            pool.push_back(p);
        }
    }
}

void clear() { pool.clear(); debt.clear(); }

void emit(int node, int count) { spawn(node, std::min(count, MAX_PARTICLES)); }

void update(float dt, vec3 gravity) {
    if (dt <= 0.0f) return;
    for (int i = 0; i < (int)sceneGraph.size(); i++) {
        const SceneNode& node = sceneGraph[i];
        if (!node.emitter.enabled || !node.emitter.emitting || !node.worldActive) { debt.erase(i); continue; }
        float& owed = debt[i];
        owed += std::max(node.emitter.rate, 0.0f) * dt;
        int count = (int)owed;
        owed -= (float)count;
        spawn(i, count);
    }

    for (Particle& p : pool) {
        p.age += dt;
        p.velocity += gravity * p.gravityScale * dt;
        vec3 next = p.position + p.velocity * dt;
        if (p.collide && sceneDistance(next) < 0.0f) {
            // Отскок от поверхности с потерей скорости
            vec3 n = sceneNormal(next);
            p.velocity = (p.velocity - 2.0f * glm::dot(p.velocity, n) * n) * 0.5f;
            next = p.position;
        }
        p.position = next;
    }
    pool.erase(std::remove_if(pool.begin(), pool.end(), [](const Particle& p) { return p.age >= p.life; }), pool.end());
}

}
