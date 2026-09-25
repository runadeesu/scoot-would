// scoot would - entry point
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "core/engine.h"
#include "core/log.h"
#include "core/filesystem.h"
#include "core/json.h"
#include "game/game.h"
#include "save/save_system.h"

#include <cstring>
#include <string>

using namespace sw;

int main(int argc, char** argv) {
    EngineConfig cfg;
    GameOptions opts;
    bool windowFromArgs = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--map") opts.map = next();
        else if (a == "--autotest") opts.autotest = next();
        else if (a == "--gpu") cfg.gpuDriver = next();
        else if (a == "--gpu-debug") cfg.gpuDebug = true;
        else if (a == "--windowed") {
            cfg.windowMode = WindowMode::Windowed;
            windowFromArgs = true;
        } else if (a == "--fullscreen") {
            cfg.windowMode = WindowMode::Borderless;
            windowFromArgs = true;
        } else if (a == "--size") {
            std::string s = next();
            sscanf(s.c_str(), "%dx%d", &cfg.width, &cfg.height);
            windowFromArgs = true;
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
        else if (a == "--spawn") opts.spawn = next();
        else if (a == "--menu") opts.menuScreen = next();
        else if (a == "--challenge") opts.challenge = next();
        else if (a == "--no-rider") opts.hideRider = true;
        else if (a == "--pad") {
            std::string st = next();
            opts.padPreview = st == "ps" ? 1 : st == "switch" ? 2 : 0;
        }
        else if (a == "--bind-pose") opts.bindPose = true;
        else if (a == "--stance") opts.stance = next() == "goofy" ? 1 : 0;
        else if (a == "--help" || a == "-h") {
            SDL_Log("scoot would [--map scene.json] [--spawn label] [--challenge id] [--play] [--menu screen] [--editor]\n"
                    "            [--windowed|--fullscreen] [--size WxH] [--novsync] [--gpu direct3d12|vulkan] [--gpu-debug]\n"
                    "            [--env preset] [--autotest test.json] [--screenshot file.png frame] [--camera x,y,z,tx,ty,tz]");
            return 0;
        }
    }
    // window + presentation from the saved settings (command line wins; scripted runs use defaults)
    if (!windowFromArgs && opts.autotest.empty() && opts.screenshot.empty() && fs::init()) {
        if (auto j = loadJsonFile(fs::userPath("settings.json"))) {
            Settings s = SaveSystem::settingsFromJson(*j);
            cfg.windowMode = WindowMode(s.graphics.windowMode);
            cfg.width = s.graphics.width;
            cfg.height = s.graphics.height;
            cfg.vsync = s.graphics.vsync;
            cfg.fpsLimit = s.graphics.fpsLimit;
        } else {
            cfg.windowMode = WindowMode::Borderless;  // first run: desktop resolution
        }
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
