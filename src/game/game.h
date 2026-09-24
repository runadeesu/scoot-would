// scoot would - game client: world, player, camera, modes, menus, HUD, audio, save
#pragma once

#include "core/engine.h"
#include "game/autotest.h"
#include "game/camera_controller.h"
#include "game/player/player.h"
#include "game/player/player_visual.h"
#include "render/render_scene.h"
#include "render/ui_draw.h"
#include "scene/scene.h"

#include <memory>
#include <string>

namespace sw {

struct GameOptions {
    std::string map;          // start directly on this map (skips the menu)
    std::string autotest;     // run a scripted test
    std::string screenshot;   // save a screenshot then quit (with frames)
    int screenshotFrame = 0;
    bool skipMenu = false;
    std::string environment;  // override environment preset
    Vec3 cameraPos;           // debug camera placement for screenshots
    Vec3 cameraTarget;
    bool fixedCamera = false;
    bool editor = false;
    int debugView = 0;
};

class Game : public EngineClient {
public:
    explicit Game(const GameOptions& opts);
    ~Game() override;
    bool init() override;
    void shutdown() override;
    void onEvent(const SDL_Event& e) override;
    void fixedUpdate(float dt) override;
    void postPhysics(float dt) override;
    void update(float dt, float alpha) override;
    void render(float dt, float alpha) override;
    bool wantsQuit() const override { return quit_; }

    bool loadMap(const std::string& relPath);
    void applyEnvironment(const std::string& preset);
    void spawnPlayerAtDefault();
    Player& player() { return player_; }
    Scene& scene() { return scene_; }
    RenderScene& renderScene() { return renderScene_; }
    CameraController& camera() { return camera_; }
    int testExitCode() const { return testExit_; }

private:
    PlayerInput gatherInput();
    void processEvents();
    void updateLamps();

    GameOptions opts_;
    RenderScene renderScene_;
    Scene scene_;
    Player player_;
    PlayerVisual visual_;
    CameraController camera_;
    std::unique_ptr<Autotest> autotest_;
    double testTime_ = 0.0;
    size_t nextShot_ = 0;
    int testExit_ = 0;
    bool quit_ = false;
    bool physicsDebug_ = false;
    std::string mapPath_;
    std::string envPreset_ = "day";
    UIDrawList ui_;
    int frame_ = 0;
    bool pendingShot_ = false;
};

}  // namespace sw
