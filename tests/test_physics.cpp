#include "TestUtil.h"
#include "Scene.h"
#include "Physics.h"

// Пол (верхняя грань на y = -0.9) плюс то, что добавит тест
static void emptyWorld() {
    resetScene();
    sceneGraph.erase(sceneGraph.begin(), sceneGraph.begin() + 2);   // Остаётся только Floor
    spdlog::set_level(spdlog::level::warn);
    physics::reset();
}

static int addBody(int type, vec3 pos, vec3 scale, float restitution = 0.0f) {
    SceneNode n;
    n.name = "Body";
    n.gpuData.position.w = (float)type;
    n.gpuData.params = vec4(1.0f, 0, 0.001f, 0);
    n.localPosition = pos;
    n.localScale = scale;
    n.body.type = BODY_DYNAMIC;
    n.body.restitution = restitution;
    sceneGraph.push_back(n);
    return (int)sceneGraph.size() - 1;
}

static void simulate(float seconds, const physics::Events& events = {}, const std::vector<physics::KinematicSphere>& spheres = {}) {
    const float dt = 1.0f / 60.0f;
    for (float t = 0.0f; t < seconds; t += dt) {
        updateSceneCaches();
        physics::step(dt, spheres, events);
    }
    updateSceneCaches();
}

void runPhysicsTests() {
    const float floorTop = -0.9f;

    TEST_SECTION("physics: box falls, settles and sleeps");
    emptyWorld();
    int box = addBody(SDF_BOX, {0, 3, 0}, vec3(0.5f));
    simulate(4.0f);
    CHECK_NEAR(sceneGraph[box].localPosition.y, floorTop + 0.5, 0.02);
    CHECK_NEAR(sceneGraph[box].localPosition.x, 0.0, 0.02);
    CHECK(sceneGraph[box].body.sleeping);
    CHECK(glm::length(sceneGraph[box].body.velocity) < 0.01f);

    TEST_SECTION("physics: tilted box ends up flat");
    emptyWorld();
    box = addBody(SDF_BOX, {0, 2, 0}, vec3(0.5f));
    sceneGraph[box].localRotation = vec3(0.5f, 0.3f, 0.4f);
    simulate(6.0f);
    CHECK_NEAR(sceneGraph[box].gpuData.position.y, floorTop + 0.5, 0.03);

    TEST_SECTION("physics: ball bounces with restitution");
    emptyWorld();
    int ball = addBody(SDF_SPHERE, {0, 3, 0}, vec3(0.5f), 0.8f);
    sceneGraph[ball].body.linearDamping = 0.0f;
    float peak = -100.0f;
    bool hitFloor = false;
    {
        const float dt = 1.0f / 60.0f;
        float prevVy = 0.0f;
        for (float t = 0.0f; t < 3.0f; t += dt) {
            updateSceneCaches();
            physics::step(dt, {}, {});
            float vy = sceneGraph[ball].body.velocity.y;
            if (prevVy < 0.0f && vy > 0.0f) hitFloor = true;
            if (hitFloor) peak = std::max(peak, sceneGraph[ball].localPosition.y);
            prevVy = vy;
        }
    }
    CHECK(hitFloor);
    // Падение с высоты 3.4 над полом, упругость 0.8 -> отскок на 0.64 от неё
    float dropHeight = 3.0f - (floorTop + 0.5f);
    float expectedPeak = floorTop + 0.5f + dropHeight * 0.64f;
    CHECK_NEAR(peak, expectedPeak, 0.25);

    TEST_SECTION("physics: fast body does not tunnel through a thin wall");
    emptyWorld();
    {
        SceneNode wall;
        wall.name = "Wall";
        wall.gpuData.position.w = (float)SDF_BOX;
        wall.gpuData.params = vec4(1.0f, 0, 0.001f, 0);
        wall.localPosition = {4, 0.5f, 0};
        wall.localScale = {0.05f, 2.0f, 2.0f};
        sceneGraph.push_back(wall);
    }
    int bullet = addBody(SDF_SPHERE, {0, 0, 0}, vec3(0.15f));
    sceneGraph[bullet].body.gravityScale = 0.0f;
    sceneGraph[bullet].body.velocity = {55, 0, 0};
    simulate(1.0f);
    CHECK(sceneGraph[bullet].localPosition.x < 4.0f);

    TEST_SECTION("physics: stack of two boxes stays stacked");
    emptyWorld();
    int lower = addBody(SDF_BOX, {0, floorTop + 0.5f, 0}, vec3(0.5f));
    int upper = addBody(SDF_BOX, {0, floorTop + 1.6f, 0}, vec3(0.5f));
    simulate(4.0f);
    CHECK_NEAR(sceneGraph[lower].localPosition.y, floorTop + 0.5, 0.03);
    CHECK_NEAR(sceneGraph[upper].localPosition.y, floorTop + 1.5, 0.05);
    CHECK_NEAR(sceneGraph[upper].localPosition.x, 0.0, 0.05);

    TEST_SECTION("physics: momentum transfer between equal balls");
    emptyWorld();
    int a = addBody(SDF_SPHERE, {-2, 0, 0}, vec3(0.5f), 1.0f);
    int b = addBody(SDF_SPHERE, {0, 0, 0}, vec3(0.5f), 1.0f);
    for (int n : {a, b}) { sceneGraph[n].body.gravityScale = 0.0f; sceneGraph[n].body.linearDamping = 0.0f; sceneGraph[n].body.friction = 0.0f; }
    sceneGraph[a].body.velocity = {4, 0, 0};
    simulate(1.0f);
    CHECK_NEAR(sceneGraph[b].body.velocity.x, 4.0, 0.4);
    CHECK_NEAR(sceneGraph[a].body.velocity.x, 0.0, 0.4);

    TEST_SECTION("physics: events");
    emptyWorld();
    box = addBody(SDF_BOX, {0, 1, 0}, vec3(0.3f));
    {
        SceneNode zone;
        zone.name = "Zone";
        zone.gpuData.position.w = (float)SDF_BOX;
        zone.isTrigger = true;
        zone.localPosition = {0, floorTop + 0.3f, 0};
        zone.localScale = {1, 0.5f, 1};
        sceneGraph.push_back(zone);
    }
    int zoneIdx = (int)sceneGraph.size() - 1;
    int collisions = 0, enters = 0, exits = 0, collidedWith = -5;
    physics::Events events;
    events.onCollide = [&](int self, int other, vec3, vec3, float) { if (self == box) { collisions++; collidedWith = other; } };
    events.onTrigger = [&](int trigger, int other, bool entered) { if (trigger == zoneIdx && other == box) (entered ? enters : exits)++; };
    simulate(3.0f, events);
    CHECK(collisions == 1);          // Один удар об пол, покой событий не шлёт
    CHECK(collidedWith == 0);        // Узел пола
    CHECK(enters == 1);
    CHECK(exits == 0);

    TEST_SECTION("physics: kinematic sphere pushes a body and wakes it");
    emptyWorld();
    box = addBody(SDF_BOX, {0, floorTop + 0.5f, 0}, vec3(0.5f));
    simulate(2.0f);
    CHECK(sceneGraph[box].body.sleeping);
    std::vector<physics::KinematicSphere> pusher = { { vec3(-0.8f, floorTop + 0.5f, 0), 0.4f, vec3(2, 0, 0), physics::PLAYER_ID } };
    simulate(0.5f, {}, pusher);
    CHECK(sceneGraph[box].localPosition.x > 0.05f);

    TEST_SECTION("physics: raycast");
    emptyWorld();
    updateSceneCaches();
    physics::RayHit hit;
    CHECK(physics::raycast({0, 5, 0}, {0, -1, 0}, 100.0f, hit));
    CHECK_NEAR(hit.distance, 5.0 - floorTop, 0.02);
    CHECK(hit.node == 0);
    CHECK_NEAR(hit.normal.y, 1.0, 0.01);
    CHECK(!physics::raycast({0, 5, 0}, {0, 1, 0}, 100.0f, hit));

    spdlog::set_level(spdlog::level::info);
    resetScene();
}
