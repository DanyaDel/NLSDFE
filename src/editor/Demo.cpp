#include "Demo.h"
#include "Project.h"
#include "Scene.h"
#include "ScriptSystem.h"
#include <filesystem>
#include <fstream>

namespace {
    void writeScript(const char* name, const char* source) {
        std::string path = project.path(std::string("scripts/") + name);
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) return;
        std::ofstream(path) << source;
    }

    int add(const char* name, int type, vec3 pos, vec3 scale, vec3 color) {
        SceneNode n;
        n.name = name;
        n.gpuData.position.w = (float)type;
        n.gpuData.params = vec4(1.0f, 0, 0.02f, 0);
        n.gpuData.color = vec4(color, 0.5f);
        n.localPosition = pos;
        n.localScale = scale;
        sceneGraph.push_back(n);
        return (int)sceneGraph.size() - 1;
    }

    void attach(int node, const char* script) {
        sceneGraph[node].script.path = std::string("scripts/") + script;
        nls::scripts.syncVars(sceneGraph[node]);
    }
}

void loadDemoScene() {
    writeScript("spinner.nls",
        "// Rotates the object; speed is editable in the Inspector\n"
        "var speed: float = 60.0\n"
        "\n"
        "on update(dt: float) {\n"
        "    self.rotation.y += speed * dt\n"
        "}\n");
    writeScript("ball.nls",
        "// Changes colour on every hit and jumps when you press E nearby\n"
        "var jump: float = 6.0\n"
        "var hits = 0\n"
        "\n"
        "on start() {\n"
        "    log(self.name + \" is ready\")\n"
        "}\n"
        "\n"
        "on collide(other: node) {\n"
        "    hits += 1\n"
        "    self.color = vec3(random(), random(), random())\n"
        "}\n"
        "\n"
        "on update(dt: float) {\n"
        "    if input.pressed(\"e\") and distance(player.position(), self.worldPosition) < 3.0 {\n"
        "        self.applyImpulse(vec3(0, jump * self.mass, 0))\n"
        "    }\n"
        "}\n");
    writeScript("lamp.nls",
        "// Pulsing light\n"
        "var base: float = 6.0\n"
        "var amount: float = 3.0\n"
        "\n"
        "on update(dt: float) {\n"
        "    self.lightIntensity = base + amount * sin(time.now() * 3.0)\n"
        "}\n");
    writeScript("launcher.nls",
        "// Trigger zone: throws the target into the air when something walks in\n"
        "var target: node\n"
        "var power: float = 8.0\n"
        "\n"
        "on triggerEnter(other: node) {\n"
        "    if target != null {\n"
        "        target.applyImpulse(vec3(0, power * target.mass, 0))\n"
        "        toast(\"Launched \" + target.name)\n"
        "    }\n"
        "}\n");

    nls::registerEngineBindings();
    resetScene();
    sceneGraph.erase(sceneGraph.begin(), sceneGraph.begin() + 2);   // Остаётся пол
    sceneGraph[0].gpuData.color = vec4(0.35f, 0.37f, 0.4f, 0.9f);
    sceneGraph[0].localScale = {14.0f, 0.1f, 14.0f};   // С запасом: игрок появляется на месте камеры и не должен упасть мимо

    // Башня из ящиков
    for (int i = 0; i < 4; i++) {
        int crate = add(("Crate " + std::to_string(i + 1)).c_str(), SDF_BOX, {-2.5f, -0.5f + i * 0.82f, -1.0f}, vec3(0.4f), {0.75f, 0.5f, 0.25f});
        sceneGraph[crate].body.type = BODY_DYNAMIC;
        sceneGraph[crate].body.mass = 2.0f;
    }

    int ball = add("Ball", SDF_SPHERE, {0.0f, 3.0f, -1.0f}, vec3(0.45f), {0.9f, 0.2f, 0.3f});
    sceneGraph[ball].body.type = BODY_DYNAMIC;
    sceneGraph[ball].body.restitution = 0.7f;
    sceneGraph[ball].gpuData.color.w = 0.2f;
    attach(ball, "ball.nls");

    int capsule = add("Barrel", SDF_CYLINDER, {1.5f, 2.0f, -2.5f}, {0.4f, 0.55f, 0.4f}, {0.2f, 0.5f, 0.8f});
    sceneGraph[capsule].body.type = BODY_DYNAMIC;
    sceneGraph[capsule].localRotation = {0.4f, 0.0f, 1.2f};

    int ring = add("Spinner", SDF_TORUS, {3.0f, 0.6f, 1.0f}, vec3(0.9f), {1.0f, 0.77f, 0.34f});
    sceneGraph[ring].gpuData.params.w = 1.0f;       // metallic
    sceneGraph[ring].gpuData.color.w = 0.25f;
    sceneGraph[ring].localRotation = {1.2f, 0.0f, 0.0f};
    attach(ring, "spinner.nls");

    int lamp = add("Lamp", NODE_LIGHT, {0.0f, 3.5f, 1.0f}, vec3(1.0f), {1.0f, 0.85f, 0.6f});
    sceneGraph[lamp].lightRange = 12.0f;
    attach(lamp, "lamp.nls");

    int zone = add("Launch Pad", SDF_BOX, {-0.5f, -0.4f, 2.5f}, {1.0f, 0.5f, 1.0f}, {0.3f, 0.9f, 0.5f});
    sceneGraph[zone].isTrigger = true;
    attach(zone, "launcher.nls");
    for (auto& v : sceneGraph[zone].script.vars) if (v.name == "target") v.node = ball;

    cam = CameraState();
    cam.target = {0.0f, 0.3f, 0.0f};
    cam.radius = 8.0f;
    cam.phi = 1.25f;
    currentLoadedFile = "Untitled";
    showToast("Demo scene loaded: press F5 to play");
}
