// scoot would - engine: owns the platform window and runtime subsystems and runs the main loop
//
// Loop: OS events -> input -> fixed physics steps (120 Hz, accumulator) -> gameplay update
//       -> animation -> camera -> render preparation -> rendering -> audio -> UI -> present
#pragma once

#include "platform/window.h"

#include <string>
#include <vector>

namespace sw {

class EngineClient {
public:
    virtual ~EngineClient() = default;
    virtual bool init() = 0;
    virtual void shutdown() = 0;
    virtual void onEvent(const SDL_Event& e) = 0;
    virtual void fixedUpdate(float dt) = 0;          // before physics step
    virtual void postPhysics(float dt) = 0;          // after physics step
    virtual void update(float dt, float alpha) = 0;  // once per frame, alpha = interpolation factor
    virtual void render(float dt, float alpha) = 0;
    virtual bool wantsQuit() const = 0;
};

struct EngineConfig {
    int width = 1600, height = 900;
    WindowMode windowMode = WindowMode::Windowed;
    bool vsync = true;
    int fpsLimit = 0;  // 0 = unlimited
    std::string gpuDriver;  // "", "direct3d12", "vulkan"
    bool gpuDebug = false;
    float fixedHz = 120.0f;
    // deterministic test mode: every frame advances exactly this many seconds (0 = real time)
    float fixedFrameTime = 0.0f;
    int maxFrames = 0;  // 0 = unlimited
    bool hidden = false;
};

class Engine {
public:
    bool init(const EngineConfig& cfg);
    int run(EngineClient& client);
    void shutdown();
    void requestQuit() { quit_ = true; }

    Window& window() { return window_; }
    EngineConfig& config() { return cfg_; }
    double time() const { return time_; }
    uint64_t frameIndex() const { return frame_; }
    float fps() const { return fps_; }
    float fixedDt() const { return 1.0f / cfg_.fixedHz; }
    void setTimeScale(float s) { timeScale_ = s; }
    float timeScale() const { return timeScale_; }
    void setFpsLimit(int fps) { cfg_.fpsLimit = fps; }

private:
    EngineConfig cfg_;
    Window window_;
    bool quit_ = false;
    double time_ = 0.0;
    uint64_t frame_ = 0;
    float fps_ = 0.0f;
    float timeScale_ = 1.0f;
    double accumulator_ = 0.0;
};

Engine& engine();

}  // namespace sw
