#pragma once
#include "Common.h"
#include <vector>

// Частицы (компонент Particles): симуляция на CPU, рисует их runtime поверх кадра
namespace particles {

struct Particle {
    vec3 position;
    vec3 velocity;
    float age = 0.0f, life = 1.0f;
    float sizeStart = 0.1f, sizeEnd = 0.0f;
    vec3 colorStart, colorEnd;
    float gravityScale = 0.0f;
    bool collide = false;
    bool lit = false;
    float light = 1.0f;    // Яркость освещаемой частицы — считает тот, кто рисует
    bool hidden = false;   // За геометрией — проверяет тот, кто рисует
};

inline constexpr int MAX_PARTICLES = 4000;

extern std::vector<Particle> pool;

void clear();
// Выпускает count частиц разом из источника узла (источник может быть выключен)
void emit(int node, int count);
// Поток из включённых источников, движение, столкновения, старение.
// Перед вызовом должен отработать updateSceneCaches().
void update(float dt, vec3 gravity);

}
