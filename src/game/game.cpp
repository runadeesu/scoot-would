#include "game/game.h"
#include "assets/asset_manager.h"
#include "audio/audio.h"
#include "audio/music.h"
#include "core/filesystem.h"
#include "core/log.h"
#include "core/timer.h"
#include "debug/debug_ui.h"
#include "game/ui/hud.h"
#include "game/ui/menus.h"
#include "input/input.h"
#include "physics/physics_world.h"
#include "render/debug_draw.h"
#include "render/particles.h"
#include "render/renderer.h"
#include "render/shader.h"
#include "save/save_system.h"
#include "ui/ui.h"

namespace sw {

Game::Game(const GameOptions& opts) : opts_(opts), scene_(&renderScene_) {}
Game::~Game() = default;

void Game::loadMapList() {
    maps_.clear();
    auto j = loadJsonFile(fs::resolve("assets/data/maps.json"));
    if (!j || !j->contains("maps")) return;
    for (auto& m : (*j)["maps"]) {
        MapInfo mi;
        mi.id = jget<std::string>(m, "id", "");
        mi.name = jget<std::string>(m, "name", mi.id);
        mi.scene = jget<std::string>(m, "scene", "");
        if (!fs::exists(fs::resolve(mi.scene))) continue;
        if (m.contains("districts"))
            for (auto& d : m["districts"]) {
                DistrictInfo di;
                di.id = jget<std::string>(d, "id", "");
                di.name = jget<std::string>(d, "name", di.id);
                di.spawn = jget<std::string>(d, "spawn", "");
                di.description = jget<std::string>(d, "desc", "");
                di.color = jcolor(d, "color", Vec3(1));
                mi.districts.push_back(di);
            }
        maps_.push_back(mi);
    }
}

const MapInfo* Game::currentMap() const {
    for (auto& m : maps_)
        if (m.scene == mapPath_) return &m;
    return nullptr;
}

std::string Game::currentArea() const {
    // the smallest area zone containing the player
    std::string best;
    float bestVol = 1e30f;
    Vec3 p = player_.position();
    const_cast<Scene&>(scene_).forEach([&](Entity& e) {
        if (!e.zone || e.zone->kind != "area") return;
        Vec3 lp = p - e.worldPosition();
        Vec3 he = e.zone->halfExtents;
        if (std::fabs(lp.x) <= he.x && std::fabs(lp.z) <= he.z && lp.y > -5.0f && lp.y < he.y * 2.0f + 30.0f) {
            float vol = he.x * he.z;
            if (vol < bestVol) {
                bestVol = vol;
                best = e.zone->area;
            }
        }
    });
    return best;
}

bool Game::init() {
    auto defaults = loadJsonFile(fs::resolve("config/input.json"));
    auto user = loadJsonFile(fs::userPath("input.json"));
    if (defaults) input().loadBindings(*defaults, user ? &*user : nullptr);
    saves().init();
    bool scripted = !opts_.autotest.empty() || !opts_.screenshot.empty();
    audio().init(!scripted);
    music().init();
    ui::context().init();
    menus_ = std::make_unique<Menus>(*this);
    hud_ = std::make_unique<Hud>(*this);
    debug_ = std::make_unique<DebugUI>(*this);
    debug_->init();
    modes_.load();
    loadMapList();
    player_.init();
    applySettings(false);
    if (!opts_.autotest.empty()) {
        autotest_ = std::make_unique<Autotest>();
        if (!autotest_->load(fs::resolve(opts_.autotest))) {
            LOG_ERROR("autotest: cannot load %s", opts_.autotest.c_str());
            testExit_ = 2;
            quit_ = true;
            return true;
        }
        opts_.map = autotest_->map();
    }
    if (opts_.debugView) {
        RenderSettings rs = renderer().settings();
        rs.debugView = opts_.debugView;
        renderer().applySettings(rs);
    }
    std::string map = opts_.map;
    std::string spawn = opts_.spawn;
    if (!opts_.challenge.empty()) {
        if (const ChallengeDef* d = modes_.find(opts_.challenge)) {
            map = d->map;
            spawn = d->spawn;
        }
    }
    if (map.empty()) {
        map = saves().data().lastMap;
        if (spawn.empty()) spawn = saves().data().lastSpawn;
    }
    if (!fs::exists(fs::resolve(map))) map = fs::exists(fs::resolve("assets/scenes/city.json")) ? "assets/scenes/city.json" : "assets/scenes/testpark.json";
    if (!loadMap(map)) return false;
    if (!spawn.empty()) spawnAt(spawn);
    applyCustomization();

    bool direct = autotest_ || opts_.skipMenu || (!opts_.map.empty() && opts_.menuScreen.empty()) || !opts_.challenge.empty();
    if (!opts_.challenge.empty()) startChallenge(opts_.challenge);
    else if (direct) startFreeRide(false);
    else {
        state_ = AppState::Menu;
        menus_->open(Menus::Screen::Main, false);
        if (opts_.menuScreen == "rider") menus_->open(Menus::Screen::Rider);
        else if (opts_.menuScreen.rfind("scooter", 0) == 0) {
            // "scooter" or "scooter:<category>" (screenshots of every shop page)
            if (opts_.menuScreen.size() > 8) menus_->setShopCategory(std::atoi(opts_.menuScreen.c_str() + 8));
            menus_->open(Menus::Screen::Scooter);
        }
        else if (opts_.menuScreen == "settings") menus_->open(Menus::Screen::Settings);
        else if (opts_.menuScreen == "map") menus_->open(Menus::Screen::Map);
        else if (opts_.menuScreen == "play") menus_->open(Menus::Screen::Play);
        else if (opts_.menuScreen == "challenges") {
            menus_->setChallengeType(ModeType::Line);
            menus_->open(Menus::Screen::Challenges);
        }
    }
    if (opts_.editor) openEditor(true);
    music().setContext(state_ == AppState::Menu);
    return true;
}

void Game::shutdown() {
    saves().data().custom = visual_.customization();
    saves().markDirty();
    saves().flush();
    if (debug_) debug_->shutdown();
    audio_.stop();
    music().shutdown();
    modes_.stop(&renderScene_);
    visual_.destroy();
    player_.despawn();
    scene_.clear();
    renderScene_.clear();
    fontAtlas().shutdown();
    audio().shutdown();
}

void Game::applySettings(bool video) {
    const Settings& s = saves().settings();
    bool scripted = !opts_.autotest.empty() || !opts_.screenshot.empty();
    // renderer
    RenderSettings rs = renderer().settings();
    rs.shadowQuality = s.graphics.shadowQuality;
    rs.ssao = s.graphics.ssao;
    rs.bloom = s.graphics.bloom;
    rs.fxaa = s.graphics.fxaa;
    rs.motionBlur = s.graphics.motionBlur;
    rs.renderScale = s.graphics.renderScale;
    rs.anisotropy = float(s.graphics.anisotropy);
    rs.drawDistance = s.graphics.drawDistance;
    rs.lodBias = s.graphics.lodBias;
    rs.sharpen = s.graphics.sharpen;
    rs.particles = s.graphics.particles;
    rs.brightness = s.graphics.brightness;
    renderer().applySettings(rs);
    assets().setMaxTextureSize(256 << s.graphics.textureQuality);
    camera_.baseFov = s.graphics.fov;
    // window / presentation (skipped for scripted runs so tests stay reproducible)
    if (video && !scripted) {
        engine().window().setMode(WindowMode(s.graphics.windowMode), s.graphics.width, s.graphics.height);
        gpu().setPresentMode(s.graphics.vsync);
        engine().setFpsLimit(s.graphics.fpsLimit);
    } else if (!scripted) {
        engine().setFpsLimit(s.graphics.fpsLimit);
    }
    // gameplay
    PlayerSettings ps = player_.settings;
    ps.assist = LandingAssist(s.gameplay.landingAssist);
    ps.balanceScale = s.gameplay.balanceDifficulty;
    player_.applySettings(ps);
    camera_.sensitivity = s.gameplay.cameraSensitivity;
    camera_.invertY = s.gameplay.invertY;
    camera_.shakeSetting = s.gameplay.cameraShake;
    camera_.distanceScale = s.gameplay.cameraDistance;
    visual_.setGoofy(opts_.stance >= 0 ? opts_.stance == 1 : s.gameplay.stance == 1);
    input().vibrationEnabled = s.gameplay.vibration;
    input().invertCameraY = s.gameplay.invertY;
    // audio
    audio().setBusVolume(Bus::Master, s.audio.master);
    audio().setBusVolume(Bus::Music, s.audio.music);
    audio().setBusVolume(Bus::Sfx, s.audio.sfx);
    audio().setBusVolume(Bus::Environment, s.audio.environment);
    audio().setBusVolume(Bus::Ui, s.audio.ui);
    music().setEnabled(s.audio.musicEnabled && !scripted);
}

void Game::applyCustomization() {
    visual_.setRiderVisible(!opts_.hideRider);
    visual_.setBindPose(opts_.bindPose);
    visual_.applyCustomization(saves().data().custom);
}

namespace {
// reads every lighting key present in e (missing keys keep their current value)
void readEnvironment(const Json& e, Environment& env) {
    env.hdri = jget<std::string>(e, "hdri", env.hdri);
    env.rotation = jget<float>(e, "rotation", env.rotation);
    env.rotation += jget<float>(e, "rotationOffset", 0.0f);
    env.sunIntensity = jget<float>(e, "sunIntensity", env.sunIntensity);
    env.sunColor = jvec3(e, "sunColor", env.sunColor);
    env.exposure = jget<float>(e, "exposure", env.exposure);
    env.skyIntensity = jget<float>(e, "skyIntensity", env.skyIntensity);
    env.iblIntensity = jget<float>(e, "iblIntensity", env.iblIntensity);
    env.fogDensity = jget<float>(e, "fogDensity", env.fogDensity);
    env.fogFalloff = jget<float>(e, "fogFalloff", env.fogFalloff);
    env.fogStart = jget<float>(e, "fogStart", env.fogStart);
    env.fogTint = jvec3(e, "fogTint", env.fogTint);
    env.bloomStrength = jget<float>(e, "bloomStrength", env.bloomStrength);
    env.contrast = jget<float>(e, "contrast", env.contrast);
    env.saturation = jget<float>(e, "saturation", env.saturation);
    env.temperature = jget<float>(e, "temperature", env.temperature);
    env.vignette = jget<float>(e, "vignette", env.vignette);
    env.lampsOn = jget<bool>(e, "lamps", env.lampsOn);
    env.autoSun = jget<bool>(e, "autoSun", env.autoSun);
    env.sunDirection = jvec3(e, "sunDirection", env.sunDirection);
    env.urbanReflection = jget<float>(e, "urbanReflection", env.urbanReflection);
}
}  // namespace

void Game::applyEnvironment(const std::string& preset) {
    auto j = loadJsonFile(fs::resolve("assets/data/environments.json"));
    Environment env;
    if (j && j->contains(preset)) {
        env.name = preset;
        readEnvironment((*j)[preset], env);
    } else {
        LOG_WARN("game: unknown environment preset '%s'", preset.c_str());
    }
    // per map adjustments on top of every preset (e.g. "rotationOffset" to put the sun where the
    // map's main line is lit)
    const Json& mapEnv = scene_.environmentJson();
    if (mapEnv.is_object() && mapEnv.contains("adjust")) readEnvironment(mapEnv["adjust"], env);
    renderScene_.environment = env;
    renderScene_.environmentVersion++;
    envPreset_ = preset;
    updateLamps();
    audio_.setEnvironment(preset);
}

void Game::updateLamps() {
    bool on = renderScene_.environment.lampsOn;
    scene_.forEach([&](Entity& e) {
        if (!e.light || e.light->handle == RenderScene::kInvalid) return;
        if (LightProxy* lp = renderScene_.light(e.light->handle)) lp->enabled = !e.light->nightOnly || on;
    });
    MaterialPtr lamp = assets().material("lamp_emissive");
    lamp->emissiveStrength = on ? 40.0f : 0.0f;
    MaterialPtr win = assets().material("window_lit");
    win->emissiveStrength = on ? 3.0f : 0.0f;
}

bool Game::loadMap(const std::string& relPath) {
    Timer t;
    if (inShop_) setShop(false);
    shop_.reset();
    modes_.stop(&renderScene_);
    audio_.stop();
    visual_.destroy();
    player_.despawn();
    scene_.clear();
    renderScene_.clear();
    physics().clearStatic();
    if (!scene_.load(fs::resolve(relPath))) {
        LOG_ERROR("game: failed to load map %s", relPath.c_str());
        return false;
    }
    mapPath_ = relPath;
    std::string preset = jget<std::string>(scene_.environmentJson(), "preset", "day");
    if (!opts_.environment.empty()) preset = opts_.environment;
    applyEnvironment(preset);
    player_.grinds.rebuild(scene_);
    scene_.buildStaticBatches();
    renderScene_.buildGrid(32.0f);
    visual_.create(renderScene_);
    applyCustomization();
    spawnPlayerAtDefault();
    audio_.start(preset);
    if (hud_) hud_->reset();
    LOG_INFO("game: map '%s' ready in %.0f ms (%zu entities)", relPath.c_str(), t.milliseconds(), scene_.entityCount());
    return true;
}

void Game::spawnPlayerAtDefault() {
    Vec3 pos(0, 0.2f, 0);
    Quat rot;
    bool found = false;
    scene_.forEach([&](Entity& e) {
        if (!e.spawn) return;
        if (!found || e.spawn->isDefault) {
            pos = e.worldPosition();
            rot = Quat::fromMat(e.world);
            found = true;
        }
    });
    if (autotest_) {
        pos = autotest_->spawnPos();
        rot = Quat::angleAxis(autotest_->spawnYaw() * kDeg2Rad, Vec3(0, 1, 0));
    }
    player_.setSpawn(pos, rot);
    player_.spawn(pos, rot);
    if (autotest_ && autotest_->spawnVelocity().lengthSq() > 0.0f) player_.scooter.setVelocity(autotest_->spawnVelocity());
    CameraTarget ct;
    ct.position = pos + Vec3(0, 1.1f, 0);
    ct.forward = rot * Vec3(0, 0, -1);
    camera_.reset(ct);
}

bool Game::spawnAt(const std::string& label) {
    bool found = false;
    scene_.forEach([&](Entity& e) {
        if (found || !e.spawn || e.spawn->label != label) return;
        Vec3 pos = e.worldPosition();
        Quat rot = Quat::fromMat(e.world);
        player_.setSpawn(pos, rot);
        player_.spawn(pos, rot);
        CameraTarget ct;
        ct.position = pos + Vec3(0, 1.1f, 0);
        ct.forward = rot * Vec3(0, 0, -1);
        camera_.reset(ct);
        found = true;
    });
    if (!found) LOG_WARN("game: no spawn labelled '%s'", label.c_str());
    return found;
}

// --- flow ---------------------------------------------------------------------------------------

void Game::startFreeRide(bool respawn) {
    modes_.stop(&renderScene_);
    modes_.startFreeRide();
    state_ = AppState::Playing;
    menus_->close();
    player_.combo.reset();
    if (respawn) {
        player_.respawn(false);
        CameraTarget ct;
        ct.position = player_.position() + Vec3(0, 1.1f, 0);
        ct.forward = player_.scooter.forward();
        camera_.reset(ct);
    }
    music().setContext(false);
    engine().setTimeScale(timeScale);
}

bool Game::startChallenge(const std::string& id) {
    const ChallengeDef* d = modes_.find(id);
    if (!d) {
        LOG_WARN("game: unknown challenge '%s'", id.c_str());
        return false;
    }
    if (d->map != mapPath_ && !loadMap(d->map)) return false;
    if (!d->spawn.empty()) spawnAt(d->spawn);
    player_.combo.reset();
    modes_.start(*d, renderScene_);
    state_ = AppState::Playing;
    menus_->close();
    if (hud_) hud_->reset();
    music().setContext(false);
    engine().setTimeScale(timeScale);
    return true;
}

void Game::restartRun() {
    if (modes_.inChallenge()) {
        const ChallengeDef* d = modes_.current();
        if (!d->spawn.empty()) spawnAt(d->spawn);
        else player_.respawn(false);
        player_.combo.reset();
        modes_.restart(renderScene_);
        if (hud_) hud_->reset();
    } else {
        player_.respawn(false);
        player_.combo.reset();
    }
    CameraTarget ct;
    ct.position = player_.position() + Vec3(0, 1.1f, 0);
    ct.forward = player_.scooter.forward();
    camera_.reset(ct);
    state_ = AppState::Playing;
    engine().setTimeScale(timeScale);
}

void Game::goToMenu() {
    modes_.stop(&renderScene_);
    modes_.startFreeRide();
    state_ = AppState::Menu;
    engine().setTimeScale(1.0f);
    player_.respawn(false);
    menus_->close();
    menus_->open(Menus::Screen::Main, false);
    music().setContext(true);
    engine().window().setMouseCaptured(false);
}

void Game::setPaused(bool p) {
    if (p && state_ == AppState::Playing) {
        state_ = AppState::Paused;
        engine().setTimeScale(0.0f);
        menus_->open(Menus::Screen::Pause, false);
        music().setDuck(0.45f);
        audio().play("ui_select", Bus::Ui);
    } else if (!p && state_ == AppState::Paused) {
        state_ = AppState::Playing;
        engine().setTimeScale(timeScale);
        music().setDuck(1.0f);
    }
    input().clearLatches();
}

void Game::openEditor(bool on) {
    if (on) {
        state_ = AppState::Editor;
        scene_.setEditorMode(true);
        engine().setTimeScale(0.0f);
        if (debug_) debug_->setEditor(true);
    } else if (state_ == AppState::Editor) {
        scene_.setEditorMode(false);
        if (debug_) debug_->setEditor(false);
        state_ = AppState::Playing;
        // the level may have changed: rebuild derived data
        player_.grinds.rebuild(scene_);
        scene_.buildStaticBatches();
        renderScene_.buildGrid(32.0f);
        engine().setTimeScale(timeScale);
    }
}

// --- frame ------------------------------------------------------------------------------------------

void Game::onEvent(const SDL_Event& e) {
    if (debug_ && debug_->processEvent(e)) return;
    if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST && state_ == AppState::Playing && !autotest_ && opts_.screenshot.empty()) setPaused(true);
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        switch (e.key.scancode) {
            case SDL_SCANCODE_F3: physicsDebug = !physicsDebug; break;
            case SDL_SCANCODE_F5: shaders().reloadAll(); break;
            case SDL_SCANCODE_F2:
                if (state_ == AppState::Editor) openEditor(false);
                else if (state_ == AppState::Playing) openEditor(true);
                break;
            case SDL_SCANCODE_F11: {
                char name[64];
                snprintf(name, sizeof(name), "screenshots/shot_%lld.png", (long long)SDL_GetTicks());
                renderer().requestScreenshot(fs::userPath(name));
                if (hud_) hud_->toast("Screenshot saved", 1.5f);
                break;
            }
            default: break;
        }
    }
}

PlayerInput Game::gatherInput() {
    Input& in = input();
    PlayerInput pi;
    pi.move = in.moveStick();
    pi.look = in.lookStick();
    pi.jumpDown = in.down(Action::Jump);
    pi.jumpPressed = in.consumePressed(Action::Jump);
    pi.jumpReleased = in.consumeReleased(Action::Jump);
    pi.pushPressed = in.consumePressed(Action::Push);
    pi.brake = in.axis(Axis::Brake);
    pi.spinLeft = in.down(Action::SpinLeft);
    pi.spinRight = in.down(Action::SpinRight);
    pi.grab = in.axis(Axis::Grab);
    pi.revertPressed = in.consumePressed(Action::Revert);
    pi.respawnPressed = in.consumePressed(Action::Respawn);
    pi.checkpointPressed = in.consumePressed(Action::Checkpoint);
    pi.rightDir = in.currentRightDir();
    return pi;
}

void Game::fixedUpdate(float dt) {
    PlayerInput pi;
    if (autotest_) {
        pi = autotest_->input(testTime_, input().flicks(), engine().time(), player_);
    } else if (state_ == AppState::Playing && !(modes_.state() == ModeState::Countdown) && !(debug_ && debug_->wantsKeyboard())) {
        pi = gatherInput();
    } else {
        input().flicks().clear();
        pi.brake = state_ == AppState::Menu ? 1.0f : 0.0f;
    }
    if (modes_.state() == ModeState::Finished) pi = PlayerInput{};
    player_.fixedUpdate(dt, pi, input().flicks(), engine().time());
}

void Game::postPhysics(float dt) {
    player_.postPhysics(dt);
    if (autotest_) {
        autotest_->observe(testTime_, dt, player_);
        testTime_ += dt;
    }
    processEvents();
}

void Game::processEvents() {
    SaveData& sd = saves().data();
    for (const GameEvent& e : player_.events()) {
        audio_.onEvent(e);
        if (hud_) hud_->onEvent(e);
        modes_.onEvent(e, player_);
        switch (e.type) {
            case GameEventType::Land: {
                float impact = e.magnitude;
                camera_.addShake(saturate((impact - 3.0f) / 8.0f) * 0.8f);
                input().rumble(saturate(impact / 10.0f), saturate(impact / 14.0f), impact > 7.0f ? 260 : 140);
                const SurfaceType& st = surfaces().get(e.surface);
                if (st.dustAmount > 0.0f) particles().dust(e.position, e.velocity * 0.2f, st.dustColor, int(4 + impact * 2 * st.dustAmount * 4), 0.2f);
                break;
            }
            case GameEventType::GrindStart: input().rumble(0.2f, 0.35f, 120); break;
            case GameEventType::Bail:
                camera_.addShake(1.0f);
                input().rumble(0.9f, 0.9f, 450);
                sd.bails++;
                break;
            case GameEventType::TrickLanded: sd.tricksLanded++; break;
            case GameEventType::ComboBanked: sd.bestCombo = std::max(sd.bestCombo, e.score); break;
            case GameEventType::Impact: camera_.addShake(saturate(e.magnitude / 10.0f) * 0.5f); break;
            default: break;
        }
    }
    player_.events().clear();
}

static ui::NavInput buildNav(float dt) {
    Input& in = input();
    ui::NavInput n;
    // edge + auto repeat for the directions (d-pad, keys, left stick)
    static float held[4] = {0, 0, 0, 0};
    Vec2 st = in.moveStick();
    bool dirs[4] = {in.down(Action::NavUp) || st.y > 0.6f, in.down(Action::NavDown) || st.y < -0.6f, in.down(Action::NavLeft) || st.x < -0.6f,
                    in.down(Action::NavRight) || st.x > 0.6f};
    bool fire[4];
    for (int i = 0; i < 4; ++i) {
        if (!dirs[i]) {
            held[i] = 0.0f;
            fire[i] = false;
            continue;
        }
        float before = held[i];
        held[i] += dt;
        const float delay = 0.38f, rate = 0.075f;
        if (before == 0.0f) fire[i] = true;
        else if (held[i] >= delay) fire[i] = int((held[i] - delay) / rate) != (before < delay ? -1 : int((before - delay) / rate));
        else fire[i] = false;
    }
    n.up = fire[0];
    n.down = fire[1];
    n.left = fire[2];
    n.right = fire[3];
    n.confirm = in.pressed(Action::Confirm);
    n.back = in.pressed(Action::Back);
    n.tabLeft = in.pressed(Action::TabLeft);
    n.tabRight = in.pressed(Action::TabRight);
    n.extra = in.pressed(Action::MenuExtra);
    n.mouse = in.mousePosition();
    n.mouseMoved = in.mouseDelta().lengthSq() > 0.0f;
    n.click = in.mouseClicked(SDL_BUTTON_LEFT);
    n.mouseDown = in.mouseDown(SDL_BUTTON_LEFT);
    n.wheel = in.mouseWheel();
    return n;
}

void Game::setShop(bool on) {
    if (on == inShop_) return;
    inShop_ = on;
    if (on) {
        shop_.ensureBuilt(renderScene_);
        shop_.setActive(true);
        mapEnv_ = renderScene_.environment;
        renderScene_.environment = shop_.environment();
        visual_.setDisplay(true, shop_.displayTransform());
        shopCamInit_ = false;
    } else {
        shop_.setActive(false);
        renderScene_.environment = mapEnv_;
        visual_.setDisplay(false);
    }
    renderScene_.environmentVersion++;
}

// orbit camera around the displayed scooter: right stick / mouse drag rotate, triggers / wheel zoom;
// the target follows the part category being edited
bool Game::updateShopCamera(float dt) {
    if (!inShop_) return false;
    Input& in = input();
    int cat = menus_->shopCategory();
    // focus point (display scooter body space), distance and pitch per category
    const ScooterDims d;
    struct Focus {
        Vec3 p;
        float dist, pitch;
    };
    Vec3 clampPos = d.frontAxle() + d.steerAxis() * (0.24f / d.steerAxis().y);
    const Focus foci[6] = {{Vec3(0, 0.36f, 0.0f), 2.1f, 0.3f},           {Vec3(0, 0.55f, -0.2f), 1.95f, 0.16f},
                           {clampPos, 0.95f, 0.22f},                     {d.frontAxle() + Vec3(0, 0.12f, 0), 1.15f, 0.16f},
                           {d.frontAxle() + Vec3(0, 0.12f, 0), 1.15f, 0.16f}, {d.barCenter() - Vec3(0, 0.05f, 0), 1.3f, 0.1f}};
    const Focus& f = foci[std::clamp(cat, 0, 5)];
    int w, h;
    engine().window().pixelSize(w, h);
    Vec2 look = in.lookStick();
    bool overPanel = in.mousePosition().x < float(w) * 0.34f;
    if (in.mouseDown(SDL_BUTTON_LEFT) && !overPanel) look += Vec2(in.mouseDelta().x, -in.mouseDelta().y) * 0.12f;
    shopYaw_ -= look.x * 1.8f * dt;
    shopPitch_ = clampf(shopPitch_ - look.y * 1.2f * dt, -0.15f, 1.1f);
    float zoomIn = in.axis(Axis::Grab) - in.axis(Axis::Brake);
    if (!overPanel) zoomIn += in.mouseWheel() * 6.0f;
    shopZoom_ = clampf(shopZoom_ * (1.0f - zoomIn * 1.2f * dt), 0.55f, 1.8f);
    if (!shopCamInit_) {
        shopYaw_ = 0.55f;
        shopPitch_ = f.pitch;
        shopZoom_ = 1.0f;
    }
    static int lastCat = -1;
    if (cat != lastCat) {
        shopPitch_ = f.pitch;
        lastCat = cat;
    }
    Transform disp = shop_.displayTransform();
    Vec3 target = disp.transformPoint(f.p);
    float dist = f.dist * shopZoom_;
    Vec3 dir(std::cos(shopPitch_) * std::sin(shopYaw_), std::sin(shopPitch_), std::cos(shopPitch_) * std::cos(shopYaw_));
    Vec3 eye = target + dir * dist;
    // the options panel covers the left third: shift the subject into the free area
    float aspect = h > 0 ? float(w) / float(h) : 16.0f / 9.0f;
    const float fov = 40.0f * kDeg2Rad;
    Vec3 fwd = (target - eye).normalized();
    Vec3 right = cross(fwd, Vec3(0, 1, 0)).normalized();
    float halfW = std::tan(fov * 0.5f) * aspect * dist;
    Vec3 shift = right * (-0.33f * halfW);
    eye += shift;
    target += shift;
    eye = shop_.clampToRoom(eye);
    if (!shopCamInit_) {
        shopEye_ = eye;
        shopTarget_ = target;
        shopCamInit_ = true;
    }
    shopEye_ = dampv(shopEye_, eye, 6.0f, dt);
    shopTarget_ = dampv(shopTarget_, target, 6.0f, dt);
    menuView_ = RenderView::lookAt(shopEye_, shopTarget_, Vec3(0, 1, 0), fov, aspect, 0.05f);
    audio().setListener(shopEye_, (shopTarget_ - shopEye_).normalized(), Vec3(0, 1, 0), Vec3(0));
    return true;
}

void Game::updateMenuCamera(float dt) {
    menuTime_ += dt;
    if (updateShopCamera(dt)) return;
    Transform body = player_.renderTransform(1.0f);
    Vec3 fwd = player_.scooter.valid() ? player_.scooter.forward() : Vec3(0, 0, -1);
    fwd.y = 0;
    fwd = fwd.lengthSq() > 1e-4f ? fwd.normalized() : Vec3(0, 0, -1);
    Vec3 right = cross(fwd, Vec3(0, 1, 0)).normalized();
    Vec3 base = body.position;
    Vec3 eye;
    float side;  // shift of the rider on screen: + = rider on the right half
    if (menus_->customizing()) {
        // three quarter front view, rider on the left half (options panel on the right)
        eye = base + fwd * 2.4f - right * 1.0f + Vec3(0, 1.3f, 0);
        side = -0.55f;
    } else {
        float a = menuTime_ * 0.08f;
        Vec3 orbit = fwd * std::cos(a) * 4.6f + right * std::sin(a) * 4.6f;
        eye = base + orbit + Vec3(0, 1.6f + 0.2f * std::sin(menuTime_ * 0.3f), 0);
        side = 1.25f;
    }
    Vec3 look = base - eye;
    look.y = 0;
    look = look.lengthSq() > 1e-4f ? look.normalized() : fwd;
    Vec3 camRight = cross(look, Vec3(0, 1, 0)).normalized();
    Vec3 target = base + Vec3(0, 0.9f, 0) - camRight * side;
    int w, h;
    engine().window().pixelSize(w, h);
    float aspect = h > 0 ? float(w) / float(h) : 16.0f / 9.0f;
    static Vec3 se = eye, st = target;
    se = dampv(se, eye, 3.0f, dt);
    st = dampv(st, target, 3.0f, dt);
    menuView_ = RenderView::lookAt(se, st, Vec3(0, 1, 0), 50.0f * kDeg2Rad, aspect, 0.1f);
    audio().setListener(se, (st - se).normalized(), Vec3(0, 1, 0), Vec3(0));
}

void Game::updatePlayCamera(float dt, float alpha) {
    Input& in = input();
    CameraTarget ct;
    Transform body = player_.renderTransform(alpha);
    if (player_.state() == PlayerState::Bailed) {
        ct.position = player_.ragdoll.centre() + Vec3(0, 0.4f, 0);
        ct.grounded = true;
    } else {
        ct.position = body.position + Vec3(0, 1.15f, 0);
        ct.grounded = player_.scooter.grounded() || player_.state() == PlayerState::Grinding;
        ct.forward = player_.scooter.forward();
        ct.fakie = player_.fakie();
        ct.grinding = player_.state() == PlayerState::Grinding;
        ct.ignoreBody = player_.scooter.body();
    }
    ct.velocity = player_.velocity();
    ct.speed = player_.speed();
    bool lookActive = player_.state() != PlayerState::Air;
    Vec2 mouse = engine().window().mouseCaptured() ? in.mouseDelta() * 0.02f : Vec2(0);
    camera_.update(dt, ct, in.lookStick() + mouse, lookActive, in.pressed(Action::CameraReset));
}

void Game::update(float dt, float alpha) {
    ++frame_;
    Input& in = input();
    SaveData& sd = saves().data();
    if (menus_->loadPending()) menus_->performPendingLoad();

    // state transitions from input
    if (state_ == AppState::Playing && in.pressed(Action::Pause) && modes_.state() != ModeState::Finished && !autotest_) setPaused(true);
    if (state_ == AppState::Playing && in.pressed(Action::CameraMode)) camera_.cycleMode();
    if (state_ == AppState::Playing && modes_.state() == ModeState::Finished && modes_.finishedTime() > 1.2f && menus_->screen() != Menus::Screen::Results) {
        menus_->open(Menus::Screen::Results, false);
        audio().play(modes_.result().medal > 0 ? "challenge_complete" : "combo_fail", Bus::Ui);
    }
    // mouse capture while riding
    bool wantCapture = state_ == AppState::Playing && !menus_->active() && !(debug_ && debug_->wantsMouse()) && !autotest_ && opts_.screenshot.empty();
    if (wantCapture != engine().window().mouseCaptured()) engine().window().setMouseCaptured(wantCapture);

    setShop(state_ == AppState::Menu && menus_->screen() == Menus::Screen::Scooter);
    Profiler::begin(ProfileSection::Animation);
    visual_.update(dt, alpha, player_);
    Profiler::end(ProfileSection::Animation);

    Profiler::begin(ProfileSection::Camera);
    if (state_ == AppState::Menu) updateMenuCamera(dt);
    else if (state_ == AppState::Editor && debug_) debug_->updateEditorCamera(dt);
    else if (state_ == AppState::Playing) updatePlayCamera(dt, alpha);
    Profiler::end(ProfileSection::Camera);

    if (state_ == AppState::Playing) {
        modes_.update(dt * engine().timeScale(), player_, renderScene_);
        sd.playTime += double(dt);
        sd.distance += double(player_.speed() * dt);
        sd.topSpeed = std::max(sd.topSpeed, player_.speed());
        if (player_.state() == PlayerState::Air) sd.bestAirTime = std::max(sd.bestAirTime, player_.airTime());
        saves().markDirty();
    }
    saves().autosave(dt, 60.0f);

    // audio
    Profiler::begin(ProfileSection::Audio);
    audio_.update(dt, player_, camera_, state_ != AppState::Playing);
    if (state_ == AppState::Menu) audio().setListener(menuView_.position, menuView_.forward, Vec3(0, 1, 0), Vec3(0));
    music().update(dt);
    audio().update(dt);
    Profiler::end(ProfileSection::Audio);

    particles().update(dt * engine().timeScale());
    debugDraw().clear();
    if (physicsDebug) {
        physics().debugDraw(debugDraw(), player_.position(), 25.0f);
        player_.grinds.debugDraw(player_.position(), 30.0f);
        const WheelContact* ws[2] = {&player_.scooter.frontWheel(), &player_.scooter.rearWheel()};
        for (auto* w : ws)
            if (w->contact) {
                debugDraw().arrow(w->point, w->point + w->normal * 0.5f, Vec4(0.2f, 1, 0.2f, 1));
                debugDraw().cross(w->point, 0.08f, Vec4(1, 1, 0, 1));
            }
    }
    if (debug_) debug_->update(dt);

    // autotest: screenshots + finish
    if (autotest_) {
        if (nextShot_ < autotest_->screenshotTimes.size() && testTime_ >= autotest_->screenshotTimes[nextShot_]) {
            char name[128];
            snprintf(name, sizeof(name), "autotest/%s_%zu.png", autotest_->name().c_str(), nextShot_);
            renderer().requestScreenshot(fs::userPath(name));
            pendingShot_ = true;
            ++nextShot_;
        }
        if (autotest_->finished(testTime_)) {
            bool pass = autotest_->report(player_);
            testExit_ = pass ? 0 : 1;
            quit_ = true;
        }
    }
    if (!opts_.screenshot.empty() && frame_ == opts_.screenshotFrame) renderer().requestScreenshot(fs::resolve(opts_.screenshot));
    if (!opts_.screenshot.empty() && frame_ > opts_.screenshotFrame + 1) quit_ = true;
}

void Game::render(float dt, float alpha) {
    // scripted tests only render the frames that take screenshots (much faster on software GPUs)
    if (autotest_ && !(nextShot_ < autotest_->screenshotTimes.size() && testTime_ + 1.0 / 60.0 >= autotest_->screenshotTimes[nextShot_]) &&
        !pendingShot_)
        return;
    pendingShot_ = false;
    int w, h;
    engine().window().pixelSize(w, h);
    float aspect = h > 0 ? float(w) / float(h) : 16.0f / 9.0f;
    RenderView view = camera_.view(aspect);
    if (state_ == AppState::Menu) view = menuView_;
    if (state_ == AppState::Editor && debug_) view = debug_->editorView(aspect);
    if (opts_.fixedCamera) view = RenderView::lookAt(opts_.cameraPos, opts_.cameraTarget, Vec3(0, 1, 0), 60.0f * kDeg2Rad, aspect, 0.1f);

    // game UI
    ui_.clear();
    ui::Context& ui = ui::context();
    ui::NavInput nav = (debug_ && debug_->wantsKeyboard()) ? ui::NavInput{} : buildNav(dt);
    ui.begin(&ui_, w, h, dt, nav);
    if (state_ == AppState::Playing || state_ == AppState::Paused) {
        if (hudVisible && (!autotest_ || !autotest_->screenshotTimes.empty())) hud_->draw(ui, dt, view);
    }
    if (menus_->active()) menus_->draw(ui, dt);
    ui.end();

    RenderCallbacks cb;
    if (debug_) debug_->callbacks(cb);
    renderer().renderFrame(renderScene_, view, &ui_, cb, dt);
    (void)alpha;
}

}  // namespace sw
