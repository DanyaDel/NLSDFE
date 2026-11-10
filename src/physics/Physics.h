#pragma once
#include "Common.h"
#include <functional>
#include <vector>

// Физика твёрдых тел поверх SDF: мир — это поле расстояний сцены,
// тело — его SDF-примитив, контакты ищутся точками на поверхности тела.
namespace physics {

    inline constexpr int PLAYER_ID = -1;        // «Другой участник» в событиях, когда это игрок
    inline constexpr float FIXED_STEP = 1.0f / 120.0f;

    extern vec3 gravity;

    // Сфера, которой управляет не физика (игрок): толкает тела и входит в триггеры
    struct KinematicSphere {
        vec3 center;
        float radius;
        vec3 velocity;
        int id = PLAYER_ID;
    };

    struct Events {
        // a — динамическое тело; b — узел, с которым оно столкнулось, либо PLAYER_ID
        std::function<void(int a, int b, vec3 point, vec3 normal, float speed)> onCollide;
        // other — тело или PLAYER_ID
        std::function<void(int trigger, int other, bool entered)> onTrigger;
    };

    struct RayHit {
        int node = -1;
        float distance = 0.0f;
        vec3 point = {0, 0, 0};
        vec3 normal = {0, 1, 0};
    };

    // Сбрасывает накопитель времени и память о контактах (старт Play Mode)
    void reset();

    // Продвигает симуляцию на dt фиксированными шагами. Перед вызовом должен
    // отработать updateSceneCaches(); результат пишется в трансформации узлов.
    // fixedStep вызывается перед каждым шагом (скриптовый fixedUpdate).
    void step(float dt, const std::vector<KinematicSphere>& kinematics, const Events& events,
              const std::function<void(float)>& fixedStep = nullptr);

    // Управление телом из игрового кода; будят тело
    void applyImpulse(int node, vec3 impulse);
    void setVelocity(int node, vec3 velocity);
    void wake(int node);

    bool raycast(vec3 origin, vec3 direction, float maxDistance, RayHit& hit);
}
