#include "game/game.h"
#include "assets/asset_manager.h"
#include "core/filesystem.h"
#include "core/log.h"
#include "core/timer.h"
#include "input/input.h"
#include "physics/physics_world.h"
#include "render/debug_draw.h"
#include "render/particles.h"
#include "render/renderer.h"
#include "render/shader.h"

namespace sw {

Game::Game(const GameOptions& opts) : opts_(opts), scene_(&renderScene_) {}
Game::~Game() = default;

bool Game::init() {
    auto defaults = loadJsonFile(fs::resolve("config/input.json"));
    auto user = loadJsonFile(fs::userPath("input.json"));
    if (defaults) input().loadBindings(*defaults, user ? &*user : nullptr);
    player_.init();
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
    std::string map = opts_.map.empty() ? "assets/scenes/testpark.json" : opts_.map;
    if (!loadMap(map)) return false;
    return true;
}

void Game::shutdown() {
    visual_.destroy();
    player_.despawn();
    scene_.clear();
    renderScene_.clear();
}

void Game::applyEnvironment(const std::string& preset) {
    auto j = loadJsonFile(fs::resolve("assets/data/environments.json"));
    Environment env;
    if (j && j->contains(preset)) {
        const Json& e = (*j)[preset];
        env.name = preset;
        env.hdri = jget<std::string>(e, "hdri", "");
        env.rotation = jget<float>(e, "rotation", 0.0f);
        env.sunIntensity = jget<float>(e, "sunIntensity", 3.0f);
        env.sunColor = jvec3(e, "sunColor", Vec3(1, 0.95f, 0.88f));
        env.exposure = jget<float>(e, "exposure", 1.0f);
        env.skyIntensity = jget<float>(e, "skyIntensity", 1.6f);
        env.iblIntensity = jget<float>(e, "iblIntensity", 1.25f);
        env.fogDensity = jget<float>(e, "fogDensity", 0.004f);
        env.fogFalloff = jget<float>(e, "fogFalloff", 0.02f);
        env.bloomStrength = jget<float>(e, "bloomStrength", 0.06f);
        env.contrast = jget<float>(e, "contrast", 1.05f);
        env.saturation = jget<float>(e, "saturation", 1.05f);
        env.temperature = jget<float>(e, "temperature", 0.0f);
        env.vignette = jget<float>(e, "vignette", 0.25f);
        env.lampsOn = jget<bool>(e, "lamps", false);
        env.autoSun = jget<bool>(e, "autoSun", true);
        env.sunDirection = jvec3(e, "sunDirection", env.sunDirection);
    } else {
        LOG_WARN("game: unknown environment preset '%s'", preset.c_str());
    }
    renderScene_.environment = env;
    renderScene_.environmentVersion++;
    envPreset_ = preset;
    updateLamps();
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
    physics().system();
    visual_.create(renderScene_);
    spawnPlayerAtDefault();
    LOG_INFO("game: map '%s' ready in %.0f ms", relPath.c_str(), t.milliseconds());
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

void Game::onEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        switch (e.key.scancode) {
            case SDL_SCANCODE_F3: physicsDebug_ = !physicsDebug_; break;
            case SDL_SCANCODE_F5: shaders().reloadAll(); break;
            case SDL_SCANCODE_F11: {
                char name[64];
                snprintf(name, sizeof(name), "screenshots/shot_%lld.png", (long long)SDL_GetTicks());
                renderer().requestScreenshot(fs::userPath(name));
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
    } else {
        pi = gatherInput();
    }
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
    for (const GameEvent& e : player_.events()) {
        switch (e.type) {
            case GameEventType::Land: {
                float impact = e.magnitude;
                camera_.addShake(saturate((impact - 3.0f) / 8.0f) * 0.8f);
                input().rumble(saturate(impact / 10.0f), saturate(impact / 14.0f), impact > 7.0f ? 260 : 140);
                const SurfaceType& st = surfaces().get(e.surface);
                if (st.dustAmount > 0.0f) particles().dust(e.position, e.velocity * 0.2f, st.dustColor, int(4 + impact * 2 * st.dustAmount * 4), 0.2f);
                break;
            }
            case GameEventType::GrindStart:
                input().rumble(0.2f, 0.35f, 120);
                break;
            case GameEventType::Bail:
                camera_.addShake(1.0f);
                input().rumble(0.9f, 0.9f, 450);
                break;
            case GameEventType::Impact: camera_.addShake(saturate(e.magnitude / 10.0f) * 0.5f); break;
            default: break;
        }
    }
    player_.events().clear();
}

void Game::update(float dt, float alpha) {
    ++frame_;
    Input& in = input();
    if (in.pressed(Action::CameraMode)) camera_.cycleMode();
    Profiler::begin(ProfileSection::Animation);
    visual_.update(dt, alpha, player_);
    Profiler::end(ProfileSection::Animation);

    Profiler::begin(ProfileSection::Camera);
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
    camera_.update(dt, ct, in.lookStick() + in.mouseDelta() * 0.02f, lookActive, in.pressed(Action::CameraReset));
    Profiler::end(ProfileSection::Camera);

    particles().update(dt);
    debugDraw().clear();
    if (physicsDebug_) {
        physics().debugDraw(debugDraw(), player_.position(), 25.0f);
        player_.grinds.debugDraw(player_.position(), 30.0f);
        const WheelContact* ws[2] = {&player_.scooter.frontWheel(), &player_.scooter.rearWheel()};
        for (auto* w : ws)
            if (w->contact) {
                debugDraw().arrow(w->point, w->point + w->normal * 0.5f, Vec4(0.2f, 1, 0.2f, 1));
                debugDraw().cross(w->point, 0.08f, Vec4(1, 1, 0, 1));
            }
    }
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
    if (opts_.fixedCamera) view = RenderView::lookAt(opts_.cameraPos, opts_.cameraTarget, Vec3(0, 1, 0), 60.0f * kDeg2Rad, aspect, 0.1f);
    ui_.clear();
    RenderCallbacks cb;
    renderer().renderFrame(renderScene_, view, &ui_, cb, dt);
    (void)alpha;
}

}  // namespace sw
