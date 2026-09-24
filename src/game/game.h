// scoot would - game client: world, player, camera, modes, menus, HUD, audio, save
#pragma once

#include "core/engine.h"
#include "game/autotest.h"
#include "game/camera_controller.h"
#include "game/game_audio.h"
#include "game/modes/mode_manager.h"
#include "game/player/player.h"
#include "game/player/player_visual.h"
#include "render/render_scene.h"
#include "render/ui_draw.h"
#include "scene/scene.h"

#include <memory>
#include <string>

namespace sw {

class Menus;
class Hud;
class DebugUI;

struct GameOptions {
    std::string map;          // start directly on this map (skips the menu)
    std::string spawn;        // spawn label on that map
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
    std::string menuScreen;   // open this menu screen (screenshots of the UI)
    std::string challenge;    // start this challenge directly
    bool hideRider = false;   // render the scooter alone (screenshots / photo mode)
    int stance = -1;          // -1 = from the settings, 0 regular, 1 goofy
};

struct DistrictInfo {
    std::string id, name, spawn, description;
    Vec3 color{1, 1, 1};
};
struct MapInfo {
    std::string id, name, scene;
    std::vector<DistrictInfo> districts;
};

enum class AppState { Menu, Playing, Paused, Editor };

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

    // world
    bool loadMap(const std::string& relPath);
    void applyEnvironment(const std::string& preset);
    void spawnPlayerAtDefault();
    bool spawnAt(const std::string& label);
    const std::string& mapPath() const { return mapPath_; }
    const std::string& environmentPreset() const { return envPreset_; }
    const std::vector<MapInfo>& maps() const { return maps_; }
    const MapInfo* currentMap() const;
    std::string currentArea() const;

    // flow
    AppState state() const { return state_; }
    void startFreeRide(bool respawn = true);
    bool startChallenge(const std::string& id);
    void restartRun();
    void goToMenu();
    void setPaused(bool p);
    void openEditor(bool on);
    void requestQuit() { quit_ = true; }
    // settings (from the save system) -> engine, renderer, audio, player, camera, input
    void applySettings(bool video);
    void applyCustomization();

    Player& player() { return player_; }
    PlayerVisual& visual() { return visual_; }
    Scene& scene() { return scene_; }
    RenderScene& renderScene() { return renderScene_; }
    CameraController& camera() { return camera_; }
    ModeManager& modes() { return modes_; }
    GameAudio& sound() { return audio_; }
    int testExitCode() const { return testExit_; }
    bool physicsDebug = false;
    bool hudVisible = true;
    float timeScale = 1.0f;

private:
    PlayerInput gatherInput();
    void processEvents();
    void updateLamps();
    void updateMenuCamera(float dt);
    void updatePlayCamera(float dt, float alpha);
    void loadMapList();

    GameOptions opts_;
    RenderScene renderScene_;
    Scene scene_;
    Player player_;
    PlayerVisual visual_;
    CameraController camera_;
    ModeManager modes_;
    GameAudio audio_;
    std::unique_ptr<Menus> menus_;
    std::unique_ptr<Hud> hud_;
    std::unique_ptr<DebugUI> debug_;
    std::unique_ptr<Autotest> autotest_;
    std::vector<MapInfo> maps_;
    AppState state_ = AppState::Menu;
    double testTime_ = 0.0;
    size_t nextShot_ = 0;
    int testExit_ = 0;
    bool quit_ = false;
    std::string mapPath_;
    std::string envPreset_ = "day";
    UIDrawList ui_;
    int frame_ = 0;
    bool pendingShot_ = false;
    float menuTime_ = 0.0f;
    RenderView menuView_;
    bool inputFrozen_ = false;
    std::string lastArea_;
};

}  // namespace sw
