#include "core/engine.h"
#include "assets/asset_manager.h"
#include "core/filesystem.h"
#include "core/jobs.h"
#include "core/log.h"
#include "core/timer.h"
#include "input/input.h"
#include "physics/physics_world.h"
#include "render/gpu.h"
#include "render/renderer.h"
#include "render/shader.h"
#include "scene/scene.h"

#include <SDL3/SDL.h>

#include <thread>

namespace sw {

Engine& engine() {
    static Engine e;
    return e;
}

bool Engine::init(const EngineConfig& cfg) {
    cfg_ = cfg;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    bool dataOk = fs::init();
    Log::init(fs::userPath("logs/scoot_would.log"));
    Log::installCrashHandler();
    LOG_INFO("scoot would %s starting", SCOOT_VERSION);
    // SDL's own warnings / errors (GPU backend, audio, input) go into our log
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_WARN);
    SDL_SetLogOutputFunction(
        [](void*, int category, SDL_LogPriority priority, const char* message) {
            if (priority >= SDL_LOG_PRIORITY_ERROR) LOG_ERROR("SDL[%d]: %s", category, message);
            else if (priority >= SDL_LOG_PRIORITY_WARN) LOG_WARN("SDL[%d]: %s (%s)", category, message, SDL_GetError());
            else LOG_INFO("SDL[%d]: %s", category, message);
        },
        nullptr);
    LOG_INFO("data root: %s", fs::dataRoot().c_str());
    LOG_INFO("user dir : %s", fs::userDir().c_str());
    if (!dataOk) LOG_ERROR("data folder (assets/) not found next to the executable");
    jobs::init();

    if (!window_.create("scoot would", cfg.width, cfg.height, cfg.windowMode)) return false;
    if (!shaders().init()) return false;
    if (!gpu().init(window_.handle(), cfg.gpuDriver, cfg.vsync, cfg.gpuDebug)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "scoot would",
                                 "No supported graphics device was found.\nDirect3D 12 or Vulkan capable GPU and drivers are required.",
                                 window_.handle());
        return false;
    }
#if defined(SCOOT_DEV_FEATURES)
    shaders().setHotReload(true);
#endif
    assets().init();
    if (!renderer().init(window_)) return false;
    if (!physics().init()) return false;
    surfaces().load(fs::resolve("assets/data/surfaces.json"));
    registerBuiltinPrefabs();
    input().init();
    return true;
}

int Engine::run(EngineClient& client) {
    if (!client.init()) {
        LOG_CRITICAL("engine: game initialisation failed");
        return 1;
    }
    Timer frameTimer;
    double last = Timer::now();
    float fpsAccum = 0;
    int fpsFrames = 0;
    while (!quit_ && !client.wantsQuit()) {
        double now = Timer::now();
        float dt = float(now - last);
        last = now;
        if (cfg_.fixedFrameTime > 0.0f) dt = cfg_.fixedFrameTime;
        dt = std::min(dt, 0.1f);
        Profiler::begin(ProfileSection::Frame);

        // OS events + input
        Profiler::begin(ProfileSection::Input);
        input().beginFrame();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) quit_ = true;
            input().processEvent(e);
            client.onEvent(e);
        }
        time_ += double(dt);
        input().update(time_, dt);
        Profiler::end(ProfileSection::Input);

        // fixed timestep physics
        float fixed = 1.0f / cfg_.fixedHz;
        accumulator_ += double(dt * timeScale_);
        int steps = 0;
        while (accumulator_ >= double(fixed) && steps < 12) {
            Profiler::begin(ProfileSection::Gameplay);
            client.fixedUpdate(fixed);
            Profiler::end(ProfileSection::Gameplay);
            Profiler::begin(ProfileSection::Physics);
            physics().step(fixed);
            Profiler::end(ProfileSection::Physics);
            Profiler::begin(ProfileSection::Gameplay);
            client.postPhysics(fixed);
            Profiler::end(ProfileSection::Gameplay);
            accumulator_ -= double(fixed);
            ++steps;
        }
        if (steps == 12) accumulator_ = 0.0;  // spiral of death protection
        float alpha = float(accumulator_ / double(fixed));

        client.update(dt, alpha);
        client.render(dt, alpha);
        shaders().checkHotReload();

        Profiler::end(ProfileSection::Frame);
        Profiler::endFrame();
        ++frame_;
        fpsAccum += dt;
        ++fpsFrames;
        if (fpsAccum >= 0.5f) {
            fps_ = float(fpsFrames) / fpsAccum;
            fpsAccum = 0;
            fpsFrames = 0;
        }
        if (cfg_.maxFrames > 0 && frame_ >= uint64_t(cfg_.maxFrames)) quit_ = true;

        // frame limiter (when vsync is off or the limit is below the refresh rate)
        if (cfg_.fpsLimit > 0 && cfg_.fixedFrameTime <= 0.0f) {
            double target = 1.0 / double(cfg_.fpsLimit);
            double elapsed = Timer::now() - now;
            if (elapsed < target) SDL_DelayPrecise(uint64_t((target - elapsed) * 1e9));
        }
    }
    client.shutdown();
    return 0;
}

void Engine::shutdown() {
    gpu().waitIdle();
    renderer().shutdown();
    input().shutdown();
    physics().shutdown();
    shaders().shutdown();
    assets().shutdown();
    gpu().shutdown();
    window_.destroy();
    jobs::shutdown();
    LOG_INFO("shutdown complete");
    Log::shutdown();
    SDL_Quit();
}

}  // namespace sw
