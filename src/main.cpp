// scoot would - entry point
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "core/engine.h"
#include "core/log.h"
#include "game/game.h"

#include <cstring>
#include <string>

using namespace sw;

int main(int argc, char** argv) {
    EngineConfig cfg;
    GameOptions opts;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--map") opts.map = next();
        else if (a == "--autotest") opts.autotest = next();
        else if (a == "--gpu") cfg.gpuDriver = next();
        else if (a == "--gpu-debug") cfg.gpuDebug = true;
        else if (a == "--windowed") cfg.windowMode = WindowMode::Windowed;
        else if (a == "--fullscreen") cfg.windowMode = WindowMode::Borderless;
        else if (a == "--size") {
            std::string s = next();
            sscanf(s.c_str(), "%dx%d", &cfg.width, &cfg.height);
        } else if (a == "--frames") cfg.maxFrames = std::atoi(next().c_str());
        else if (a == "--novsync") cfg.vsync = false;
        else if (a == "--env") opts.environment = next();
        else if (a == "--screenshot") {
            opts.screenshot = next();
            opts.screenshotFrame = std::atoi(next().c_str());
        } else if (a == "--camera") {
            std::string s = next();
            float v[6];
            if (sscanf(s.c_str(), "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
                opts.cameraPos = Vec3(v[0], v[1], v[2]);
                opts.cameraTarget = Vec3(v[3], v[4], v[5]);
                opts.fixedCamera = true;
            }
        } else if (a == "--debugview") opts.debugView = std::atoi(next().c_str());
        else if (a == "--editor") opts.editor = true;
        else if (a == "--play") opts.skipMenu = true;
    }
    if (!opts.autotest.empty()) {
        cfg.fixedFrameTime = 1.0f / 120.0f;
        cfg.vsync = false;
    }
    if (!opts.screenshot.empty()) cfg.fixedFrameTime = 1.0f / 60.0f;
    if (!engine().init(cfg)) {
        engine().shutdown();
        return 1;
    }
    Game game(opts);
    int rc = engine().run(game);
    int testRc = game.testExitCode();
    engine().shutdown();
    return rc != 0 ? rc : testRc;
}
