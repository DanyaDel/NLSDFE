#include "Physics.h"
#include "Scene.h"
#include "MathUtils.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <set>
#include <utility>

namespace physics {

vec3 gravity = {0.0f, -18.0f, 0.0f};

namespace {
    const float SLOP = 0.005f;              // Допустимое проникновение: гасит дрожание покоящихся тел
    const float CONTACT_MARGIN = 0.01f;     // На таком расстоянии контакт уже учитывается
    const int SOLVER_ITERATIONS = 8;
    const int MAX_STEPS_PER_FRAME = 8;
    const float MAX_SPEED = 60.0f;
    const float SLEEP_LINEAR = 0.08f;
    const float SLEEP_ANGULAR = 0.15f;
    const float SLEEP_TIME = 0.6f;

    struct Body {
        int node = -1;
        GpuObject obj;                      // Форма тела; pos/rot обновляются каждый шаг
        vec3 pos;
        glm::quat rot;
        vec3 vel, angVel;
        float invMass = 1.0f;
        vec3 invInertiaLocal;
        mat3 invInertiaWorld;
        float radius = 1.0f;                // Ограничивающая сфера
        float minExtent = 0.1f;
        std::vector<vec4> samples;          // Точки коллайдера в осях тела; w = радиус точки
        std::vector<int> nearStatic;        // Статические объекты поблизости (индексы gpuObjectCache)
        RigidBody* params = nullptr;
        bool touched = false;
        bool disturbed = false;             // В тело въехало что-то движущееся
    };

    struct Contact {
        int a = -1, b = -1;                 // Индексы тел; b = -1 — мир или кинематическая сфера
        int otherNode = -1;                 // Узел для события столкновения
        vec3 point, normal;                 // Нормаль выталкивает тело a
        vec3 t1, t2;
        float pen = 0.0f;
        vec3 velB = {0, 0, 0};              // Скорость поверхности, если b не тело
        float restitution = 0.0f, friction = 0.5f;
        float targetVn = 0.0f;
        float accN = 0.0f, accT1 = 0.0f, accT2 = 0.0f;
    };

    float accumulator = 0.0f;
    std::set<std::pair<int, int>> touchingPairs;   // Пары (тело, другой) в контакте на прошлом кадре
    std::set<std::pair<int, int>> insidePairs;     // Пары (триггер, участник)

    std::vector<Body> bodies;
    std::vector<int> staticObjects;                // Индексы gpuObjectCache, не принадлежащие телам
    std::vector<Contact> contacts;

    // --- Поле расстояний статического мира ---
    vec2 opUnion(vec2 d1, vec2 d2, float k) {
        float h = std::clamp(0.5f + 0.5f * (d2.x - d1.x) / k, 0.0f, 1.0f);
        return vec2(glm::mix(d2.x, d1.x, h) - k * h * (1.0f - h), (d1.x < d2.x) ? d1.y : d2.y);
    }
    vec2 opSub(vec2 d1, vec2 d2, float k) {
        float h = std::clamp(0.5f - 0.5f * (d2.x + d1.x) / k, 0.0f, 1.0f);
        return vec2(glm::mix(d2.x, -d1.x, h) + k * h * (1.0f - h), d2.y);
    }
    vec2 opIntersect(vec2 d1, vec2 d2, float k) {
        float h = std::clamp(0.5f - 0.5f * (d2.x - d1.x) / k, 0.0f, 1.0f);
        return vec2(glm::mix(d2.x, d1.x, h) + k * h * (1.0f - h), (d1.x > d2.x) ? d1.y : d2.y);
    }

    // Та же CSG-цепочка, что в sceneMap, но только по перечисленным объектам
    vec2 mapList(vec3 p, const std::vector<int>& list) {
        vec2 res(100.0f, -1.0f);
        for (int i : list) {
            const GpuObject& o = gpuObjectCache[i];
            vec2 cur(sdfObject(o, p), (float)i);
            int op = (i == 0) ? OP_UNION : (int)o.rot1.w;
            float k = o.rot2.w;
            if (op == OP_UNION) res = opUnion(res, cur, k);
            else if (op == OP_SUBTRACT) res = opSub(cur, res, k);
            else res = opIntersect(res, cur, k);
        }
        return res;
    }

    template <typename F> vec3 gradient(F&& field, vec3 p) {
        const float e = 0.002f;
        const vec2 k(1.0f, -1.0f);
        vec3 n = vec3(k.x, k.y, k.y) * field(p + vec3(k.x, k.y, k.y) * e)
               + vec3(k.y, k.y, k.x) * field(p + vec3(k.y, k.y, k.x) * e)
               + vec3(k.y, k.x, k.y) * field(p + vec3(k.y, k.x, k.y) * e)
               + vec3(k.x, k.x, k.x) * field(p + vec3(k.x, k.x, k.x) * e);
        float len = glm::length(n);
        return len > 1e-8f ? n / len : vec3(0, 1, 0);
    }

    bool hasDynamicAncestor(int nodeIdx) {
        for (int p = nodeIdx; p != -1; p = sceneGraph[p].parent) {
            if (sceneGraph[p].body.type == BODY_DYNAMIC) return true;
        }
        return false;
    }

    // --- Точки коллайдера ---
    void addRing(std::vector<vec4>& out, float y, float radius, int count, vec3 scl) {
        for (int i = 0; i < count; i++) {
            float a = (float)i / count * 6.2831853f;
            out.push_back(vec4(vec3(std::cos(a) * radius, y, std::sin(a) * radius) * scl, 0.0f));
        }
    }

    void buildSamples(Body& b) {
        const GpuObject& o = b.obj;
        int type = (int)o.rot0.w;
        float size = std::abs(o.pos.w), p2 = o.scale.w;
        vec3 scl = vec3(o.scale);
        bool uniform = std::abs(scl.x - scl.y) < 0.01f * scl.x && std::abs(scl.x - scl.z) < 0.01f * scl.x;
        bool uniformXZ = std::abs(scl.x - scl.z) < 0.01f * scl.x;
        auto& s = b.samples;
        s.clear();

        if (type == SDF_SPHERE && uniform) {
            // Шар — одна точка с радиусом: катится гладко и считается точно
            s.push_back(vec4(0, 0, 0, size * scl.x));
        } else if (type == SDF_CAPSULE && uniformXZ) {
            float r = size * p2 * scl.x;
            for (int i = 0; i < 5; i++) s.push_back(vec4(0, (i / 4.0f * 2.0f - 1.0f) * size * scl.y, 0, r));
        } else if (type == SDF_SPHERE) {
            for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++) {
                if (x == 0 && y == 0 && z == 0) continue;
                s.push_back(vec4(glm::normalize(vec3(x, y, z)) * size * scl, 0.0f));
            }
        } else if (type == SDF_BOX) {
            // Углы, середины рёбер и центры граней
            for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++) {
                if (x == 0 && y == 0 && z == 0) continue;
                s.push_back(vec4(vec3(x, y, z) * size * scl, 0.0f));
            }
        } else if (type == SDF_TORUS) {
            for (int i = 0; i < 10; i++) {
                float a = i / 10.0f * 6.2831853f;
                vec3 c(std::cos(a) * size, 0, std::sin(a) * size);
                for (int j = 0; j < 4; j++) {
                    float t = j / 4.0f * 6.2831853f;
                    vec3 p = c + (glm::normalize(c) * std::cos(t) + vec3(0, 1, 0) * std::sin(t)) * (size * p2);
                    s.push_back(vec4(p * scl, 0.0f));
                }
            }
        } else if (type == SDF_CAPSULE) {
            float r = size * p2;
            addRing(s, -size, r, 8, scl); addRing(s, 0, r, 8, scl); addRing(s, size, r, 8, scl);
            s.push_back(vec4(vec3(0, size + r, 0) * scl, 0.0f)); s.push_back(vec4(vec3(0, -size - r, 0) * scl, 0.0f));
        } else {
            // Цилиндр и конус
            float top = (type == SDF_CONE) ? size * p2 : size;
            addRing(s, -size, size, 10, scl); addRing(s, 0, (size + top) * 0.5f, 8, scl);
            if (top > 0.01f * size) addRing(s, size, top, 10, scl);
            s.push_back(vec4(vec3(0, size, 0) * scl, 0.0f)); s.push_back(vec4(vec3(0, -size, 0) * scl, 0.0f));
        }

        // Масса и момент инерции
        vec3 he = shapeHalfExtents(type, size, p2) * scl;
        b.radius = glm::length(he);
        b.minExtent = std::min(he.x, std::min(he.y, he.z));
        float m = std::max(b.params->mass, 0.001f);
        vec3 inertia;
        if (type == SDF_SPHERE) inertia = m / 5.0f * vec3(he.y * he.y + he.z * he.z, he.x * he.x + he.z * he.z, he.x * he.x + he.y * he.y);
        else if (type == SDF_CYLINDER || type == SDF_CAPSULE || type == SDF_CONE) {
            float r2 = 0.5f * (he.x * he.x + he.z * he.z);
            float side = m * (3.0f * r2 + 4.0f * he.y * he.y) / 12.0f;
            inertia = vec3(side, m * r2 * 0.5f, side);
        } else inertia = m / 3.0f * vec3(he.y * he.y + he.z * he.z, he.x * he.x + he.z * he.z, he.x * he.x + he.y * he.y);
        b.invMass = 1.0f / m;
        b.invInertiaLocal = 1.0f / glm::max(inertia, vec3(1e-6f));
    }

    void refreshPose(Body& b) {
        mat3 r = glm::mat3_cast(b.rot);
        mat3 inv = glm::transpose(r);
        b.obj.rot0 = vec4(inv[0], b.obj.rot0.w);
        b.obj.rot1 = vec4(inv[1], b.obj.rot1.w);
        b.obj.rot2 = vec4(inv[2], b.obj.rot2.w);
        b.obj.pos = vec4(b.pos, b.obj.pos.w);
        mat3 d(0.0f);
        d[0][0] = b.invInertiaLocal.x; d[1][1] = b.invInertiaLocal.y; d[2][2] = b.invInertiaLocal.z;
        b.invInertiaWorld = r * d * inv;
    }

    void buildNearList(Body& b, float margin) {
        b.nearStatic.clear();
        float reach = b.radius + margin;
        for (int i : staticObjects) {
            const GpuObject& o = gpuObjectCache[i];
            vec3 local = mat3(vec3(o.rot0), vec3(o.rot1), vec3(o.rot2)) * (b.pos - vec3(o.pos));
            vec3 q = glm::max(glm::abs(local) - vec3(o.bound), vec3(0.0f));
            if (glm::dot(q, q) < reach * reach) b.nearStatic.push_back(i);
        }
    }

    // Собирает тела из сцены. Состояние между кадрами живёт в узлах, не здесь.
    void gather() {
        bodies.clear();
        staticObjects.clear();
        for (int i = 0; i < (int)gpuObjectCache.size(); i++) {
            int nodeIdx = (int)gpuObjectCache[i].extra.y;
            SceneNode& node = sceneGraph[nodeIdx];
            if (node.body.type == BODY_DYNAMIC) {
                Body b;
                b.node = nodeIdx;
                b.obj = gpuObjectCache[i];
                b.params = &node.body;
                b.pos = vec3(node.gpuData.position);
                b.rot = glm::normalize(glm::quat_cast(glm::transpose(mat3(node.gpuData.invRotation))));
                b.vel = node.body.velocity;
                b.angVel = node.body.angularVelocity;
                buildSamples(b);
                refreshPose(b);
                bodies.push_back(b);
            } else if (!hasDynamicAncestor(nodeIdx)) {
                staticObjects.push_back(i);
            }
        }
    }

    // Записывает позу тела обратно в локальную трансформацию узла
    void writeBack(const Body& b) {
        SceneNode& node = sceneGraph[b.node];
        vec3 scl = vec3(node.gpuData.scale);
        mat4 worldMat = glm::translate(mat4(1.0f), b.pos) * glm::mat4_cast(b.rot) * glm::scale(mat4(1.0f), scl);
        mat4 local = (node.parent == -1) ? worldMat : glm::inverse(calculateWorldMatrix(node.parent)) * worldMat;
        vec3 keepScale = node.localScale, newScale;
        decomposeMatrix(local, node.localPosition, node.localRotation, newScale);
        node.localScale = keepScale;   // Масштаб физика не трогает; знак и точность остаются исходными
        node.body.velocity = b.vel;
        node.body.angularVelocity = b.angVel;
    }

    void addContact(int a, int bIdx, int otherNode, vec3 point, vec3 normal, float distance, vec3 velB, float restitution, float friction) {
        Contact c;
        c.a = a; c.b = bIdx; c.otherNode = otherNode;
        c.point = point; c.normal = normal;
        c.pen = std::max(-distance, 0.0f);
        c.velB = velB;
        c.restitution = restitution; c.friction = friction;
        vec3 ref = std::abs(normal.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0);
        c.t1 = glm::normalize(glm::cross(normal, ref));
        c.t2 = glm::cross(normal, c.t1);
        contacts.push_back(c);
    }

    void findContacts(const std::vector<KinematicSphere>& kinematics) {
        contacts.clear();
        for (int i = 0; i < (int)bodies.size(); i++) {
            Body& b = bodies[i];
            mat3 r = glm::mat3_cast(b.rot);

            // Тело против статического мира
            if (!b.params->sleeping && !b.nearStatic.empty()) {
                auto field = [&](vec3 p) { return mapList(p, b.nearStatic).x; };
                for (const vec4& s : b.samples) {
                    vec3 x = b.pos + r * vec3(s);
                    vec2 hit = mapList(x, b.nearStatic);
                    float d = hit.x - s.w;
                    if (d >= CONTACT_MARGIN) continue;
                    vec3 n = gradient(field, x);
                    int other = hit.y >= 0.0f ? (int)gpuObjectCache[(int)hit.y].extra.y : -2;
                    float friction = b.params->friction;
                    addContact(i, -1, other, x - n * s.w, n, d, vec3(0.0f), b.params->restitution, friction);
                }
            }

            // Тело против тела: точки одного по SDF другого
            for (int j = 0; j < (int)bodies.size(); j++) {
                if (j == i) continue;
                Body& o = bodies[j];
                if (b.params->sleeping && o.params->sleeping) continue;
                vec3 delta = b.pos - o.pos;
                float reach = b.radius + o.radius + CONTACT_MARGIN;
                if (glm::dot(delta, delta) > reach * reach) continue;
                auto field = [&](vec3 p) { return sdfObject(o.obj, p); };
                for (const vec4& s : b.samples) {
                    vec3 x = b.pos + r * vec3(s);
                    float d = sdfObject(o.obj, x) - s.w;
                    if (d >= CONTACT_MARGIN) continue;
                    vec3 n = gradient(field, x);
                    addContact(i, j, o.node, x - n * s.w, n, d, vec3(0.0f),
                               std::max(b.params->restitution, o.params->restitution), std::sqrt(b.params->friction * o.params->friction));
                }
            }

            // Кинематические сферы (игрок) толкают тело
            auto field = [&](vec3 p) { return sdfObject(b.obj, p); };
            for (const KinematicSphere& k : kinematics) {
                vec3 delta = k.center - b.pos;
                float reach = b.radius + k.radius + CONTACT_MARGIN;
                if (glm::dot(delta, delta) > reach * reach) continue;
                float d = sdfObject(b.obj, k.center) - k.radius;
                if (d >= CONTACT_MARGIN) continue;
                vec3 outOfBody = gradient(field, k.center);
                addContact(i, -1, k.id, k.center - outOfBody * (d + k.radius), -outOfBody, d, k.velocity, 0.0f, 0.2f);
                if (glm::length(k.velocity) > 0.1f) b.disturbed = true;
            }
        }
    }

    vec3 pointVelocity(const Body& b, vec3 r) { return b.vel + glm::cross(b.angVel, r); }

    void applyImpulseAt(Body& b, vec3 impulse, vec3 r) {
        b.vel += impulse * b.invMass;
        b.angVel += b.invInertiaWorld * glm::cross(r, impulse);
    }

    float effectiveMass(const Body& b, vec3 r, vec3 dir) {
        return b.invMass + glm::dot(dir, glm::cross(b.invInertiaWorld * glm::cross(r, dir), r));
    }

    void solveContacts() {
        // Целевая скорость отскока фиксируется по скорости сближения до решения
        for (Contact& c : contacts) {
            Body& a = bodies[c.a];
            vec3 ra = c.point - a.pos;
            vec3 vRel = pointVelocity(a, ra) - (c.b >= 0 ? pointVelocity(bodies[c.b], c.point - bodies[c.b].pos) : c.velB);
            float vn = glm::dot(vRel, c.normal);
            c.targetVn = (vn < -1.0f) ? -c.restitution * vn : 0.0f;
            a.touched = true;
            if (c.b >= 0) {
                bodies[c.b].touched = true;
                if (glm::length(vRel) > 0.3f) { a.disturbed = true; bodies[c.b].disturbed = true; }
            }
        }

        for (int iter = 0; iter < SOLVER_ITERATIONS; iter++) {
            for (Contact& c : contacts) {
                Body& a = bodies[c.a];
                Body* b = c.b >= 0 ? &bodies[c.b] : nullptr;
                bool aFixed = a.params->sleeping, bFixed = b && b->params->sleeping;
                if (aFixed && (!b || bFixed)) continue;
                vec3 ra = c.point - a.pos;
                vec3 rb = b ? c.point - b->pos : vec3(0.0f);

                auto relVel = [&]() { return pointVelocity(a, ra) - (b ? pointVelocity(*b, rb) : c.velB); };
                auto denom = [&](vec3 dir) {
                    float k = aFixed ? 0.0f : effectiveMass(a, ra, dir);
                    if (b && !bFixed) k += effectiveMass(*b, rb, dir);
                    return std::max(k, 1e-6f);
                };
                auto apply = [&](vec3 impulse) {
                    if (!aFixed) applyImpulseAt(a, impulse, ra);
                    if (b && !bFixed) applyImpulseAt(*b, -impulse, rb);
                };

                // Нормаль: тела не должны сближаться
                float vn = glm::dot(relVel(), c.normal);
                float dj = (c.targetVn - vn) / denom(c.normal);
                float newAcc = std::max(c.accN + dj, 0.0f);
                apply(c.normal * (newAcc - c.accN));
                c.accN = newAcc;

                // Трение: ограничено долей нормального импульса
                float maxT = c.friction * c.accN;
                vec3 v = relVel();
                float j1 = -glm::dot(v, c.t1) / denom(c.t1);
                float acc1 = std::clamp(c.accT1 + j1, -maxT, maxT);
                apply(c.t1 * (acc1 - c.accT1));
                c.accT1 = acc1;
                v = relVel();
                float j2 = -glm::dot(v, c.t2) / denom(c.t2);
                float acc2 = std::clamp(c.accT2 + j2, -maxT, maxT);
                apply(c.t2 * (acc2 - c.accT2));
                c.accT2 = acc2;
            }
        }
    }

    void integrate(float h) {
        for (Body& b : bodies) {
            if (b.params->sleeping) continue;
            float speed = glm::length(b.vel);
            if (speed > MAX_SPEED) { b.vel *= MAX_SPEED / speed; speed = MAX_SPEED; }

            // Быстрое тело не должно перескочить поверхность: центр не уходит дальше,
            // чем до ближайшей стены
            vec3 move = b.vel * h;
            float travel = speed * h;
            if (travel > b.minExtent * 0.5f && !b.nearStatic.empty()) {
                float clearance = mapList(b.pos, b.nearStatic).x;
                if (clearance > 0.0f && travel > clearance * 0.9f) move *= clearance * 0.9f / travel;
            }
            b.pos += move;

            glm::quat spin(0.0f, b.angVel.x, b.angVel.y, b.angVel.z);
            b.rot = glm::normalize(b.rot + 0.5f * h * (spin * b.rot));
        }

        // Выталкивание из проникновения: по самому глубокому контакту на тело
        std::vector<vec3> correction(bodies.size(), vec3(0.0f));
        std::vector<float> deepest(bodies.size(), 0.0f);
        for (const Contact& c : contacts) {
            float depth = c.pen - SLOP;
            if (depth <= 0.0f) continue;
            Body& a = bodies[c.a];
            float wa = a.params->sleeping ? 0.0f : a.invMass;
            float wb = (c.b >= 0 && !bodies[c.b].params->sleeping) ? bodies[c.b].invMass : 0.0f;
            if (wa + wb <= 0.0f) continue;
            float push = depth * 0.8f;
            if (push * wa / (wa + wb) > deepest[c.a]) { deepest[c.a] = push * wa / (wa + wb); correction[c.a] = c.normal * deepest[c.a]; }
            if (c.b >= 0 && push * wb / (wa + wb) > deepest[c.b]) { deepest[c.b] = push * wb / (wa + wb); correction[c.b] = -c.normal * deepest[c.b]; }
        }
        for (size_t i = 0; i < bodies.size(); i++) {
            bodies[i].pos += correction[i];
            if (!bodies[i].params->sleeping) refreshPose(bodies[i]);
        }
    }

    void substep(float h, const std::vector<KinematicSphere>& kinematics) {
        for (Body& b : bodies) {
            if (b.params->sleeping) continue;
            b.vel += gravity * b.params->gravityScale * h;
            b.vel *= 1.0f / (1.0f + b.params->linearDamping * h);
            b.angVel *= 1.0f / (1.0f + b.params->angularDamping * h);
        }
        findContacts(kinematics);
        solveContacts();
        integrate(h);
    }

    void updateSleep(float dt) {
        for (Body& b : bodies) {
            RigidBody& p = *b.params;
            if (p.sleeping) {
                // Будим, если тело задели или из-под него ушла опора
                bool supported = false;
                int self = (int)(&b - bodies.data());
                if (!b.disturbed) {
                    mat3 r = glm::mat3_cast(b.rot);
                    for (const vec4& s : b.samples) {
                        if (b.nearStatic.empty()) break;
                        if (mapList(b.pos + r * vec3(s), b.nearStatic).x - s.w < 0.03f) { supported = true; break; }
                    }
                    // Опорой может быть и другое тело
                    for (const Contact& c : contacts) {
                        if (supported) break;
                        if (c.b >= 0 && (c.a == self || c.b == self)) supported = true;
                    }
                }
                if (b.disturbed || !supported) { p.sleeping = false; p.sleepTimer = 0.0f; }
                continue;
            }
            bool slow = glm::length(b.vel) < SLEEP_LINEAR && glm::length(b.angVel) < SLEEP_ANGULAR;
            if (slow && b.touched && !b.disturbed) {
                p.sleepTimer += dt;
                if (p.sleepTimer >= SLEEP_TIME) { p.sleeping = true; b.vel = vec3(0.0f); b.angVel = vec3(0.0f); }
            } else p.sleepTimer = 0.0f;
        }
    }

    void fireEvents(const std::vector<KinematicSphere>& kinematics, const Events& events, bool simulated) {
        // Столкновения: событие приходит один раз, в момент начала контакта.
        // Пара считается разомкнутой, только когда тела разошлись дальше зазора контакта,
        // поэтому покоящееся тело не шлёт событие каждый кадр.
        if (simulated) {
            std::set<std::pair<int, int>> now;
            for (const Contact& c : contacts) {
                if (c.otherNode < PLAYER_ID) continue;
                std::pair<int, int> key(bodies[c.a].node, c.otherNode);
                bool known = touchingPairs.count(key) > 0;
                if (!known && c.accN <= 0.0f) continue;   // Рядом, но удара ещё не было
                if (!now.insert(key).second) continue;
                if (!known && events.onCollide) events.onCollide(key.first, key.second, c.point, c.normal, c.accN * bodies[c.a].invMass);
            }
            touchingPairs.swap(now);
        }

        // Триггеры: тела и кинематические сферы входят в зоны и выходят из них
        std::set<std::pair<int, int>> inside;
        for (int t = 0; t < (int)sceneGraph.size(); t++) {
            const SceneNode& zone = sceneGraph[t];
            if (!zone.isTrigger || !zone.worldActive) continue;
            mat3 inv = mat3(zone.gpuData.invRotation);
            vec3 center = vec3(zone.gpuData.position);
            vec3 half = std::abs(zone.gpuData.params.x) * vec3(zone.gpuData.scale);
            auto contains = [&](vec3 p) {
                vec3 local = glm::abs(inv * (p - center));
                return local.x <= half.x && local.y <= half.y && local.z <= half.z;
            };
            for (const Body& b : bodies) if (contains(b.pos)) inside.insert({t, b.node});
            for (const KinematicSphere& k : kinematics) if (contains(k.center)) inside.insert({t, k.id});
        }
        if (events.onTrigger) {
            for (const auto& pair : inside) if (!insidePairs.count(pair)) events.onTrigger(pair.first, pair.second, true);
            for (const auto& pair : insidePairs) if (!inside.count(pair)) events.onTrigger(pair.first, pair.second, false);
        }
        insidePairs.swap(inside);
    }
}

void reset() {
    accumulator = 0.0f;
    touchingPairs.clear();
    insidePairs.clear();
}

void step(float dt, const std::vector<KinematicSphere>& kinematics, const Events& events, const std::function<void(float)>& fixedStep) {
    accumulator = std::min(accumulator + dt, FIXED_STEP * MAX_STEPS_PER_FRAME);
    int steps = (int)(accumulator / FIXED_STEP);
    accumulator -= steps * FIXED_STEP;

    gather();
    // Запас на путь, который тело может пройти за кадр
    for (Body& b : bodies) buildNearList(b, 0.3f + glm::length(b.vel) * FIXED_STEP * (steps + 1));

    for (int s = 0; s < steps; s++) {
        if (fixedStep) {
            // Скрипт мог поменять скорости тел: подхватываем и возвращаем обратно
            for (Body& b : bodies) { b.params->velocity = b.vel; b.params->angularVelocity = b.angVel; }
            fixedStep(FIXED_STEP);
            for (Body& b : bodies) { b.vel = b.params->velocity; b.angVel = b.params->angularVelocity; }
        }
        substep(FIXED_STEP, kinematics);
    }
    if (steps > 0) updateSleep(steps * FIXED_STEP);

    for (const Body& b : bodies) writeBack(b);
    fireEvents(kinematics, events, steps > 0);
}

void wake(int node) {
    if (node < 0 || node >= (int)sceneGraph.size()) return;
    sceneGraph[node].body.sleeping = false;
    sceneGraph[node].body.sleepTimer = 0.0f;
}

void applyImpulse(int node, vec3 impulse) {
    if (node < 0 || node >= (int)sceneGraph.size()) return;
    RigidBody& b = sceneGraph[node].body;
    if (b.type != BODY_DYNAMIC) return;
    b.velocity += impulse / std::max(b.mass, 0.001f);
    wake(node);
}

void setVelocity(int node, vec3 velocity) {
    if (node < 0 || node >= (int)sceneGraph.size()) return;
    sceneGraph[node].body.velocity = velocity;
    wake(node);
}

bool raycast(vec3 origin, vec3 direction, float maxDistance, RayHit& hit) {
    float len = glm::length(direction);
    if (len < 1e-8f) return false;
    direction /= len;
    float t = 0.02f;
    for (int i = 0; i < 200 && t < maxDistance; i++) {
        vec3 p = origin + direction * t;
        vec2 r = sceneMap(p);
        if (r.x < 0.0005f + 0.001f * t) {
            hit.distance = t;
            hit.point = p;
            hit.normal = sceneNormal(p);
            hit.node = r.y >= 0.0f ? (int)gpuObjectCache[(int)r.y].extra.y : -1;
            return true;
        }
        t += r.x;
    }
    return false;
}

}
