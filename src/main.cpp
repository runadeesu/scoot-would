// scoot would - entry point
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "core/engine.h"
#include "core/log.h"
#include "core/filesystem.h"
#include "core/json.h"
#include "audio/music.h"
#include "game/game.h"
#include "save/save_system.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace sw;

namespace {
// --render-song song.json out.wav: the built in synthesiser renders a song offline (trailer soundtrack)
int renderSongToWav(const std::string& songPath, const std::string& outPath) {
    fs::init();
    auto j = loadJsonFile(fs::resolve(songPath));
    if (!j) {
        LOG_ERROR("render-song: cannot read %s", songPath.c_str());
        return 1;
    }
    SoundPtr s = renderSong(*j, songPath);
    if (!s || s->frames.empty()) return 1;
    FILE* f = fopen(outPath.c_str(), "wb");
    if (!f) return 1;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    uint32_t n = uint32_t(s->frames.size()), ch = uint32_t(s->channels), sr = uint32_t(s->sampleRate);
    fwrite("RIFF", 1, 4, f);
    u32(36 + n * 2);
    fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(uint16_t(ch));
    u32(sr);
    u32(sr * ch * 2);
    u16(uint16_t(ch * 2));
    u16(16);
    fwrite("data", 1, 4, f);
    u32(n * 2);
    for (float v : s->frames) {
        int16_t q = int16_t(std::max(-1.0f, std::min(1.0f, v)) * 32767.0f);
        fwrite(&q, 2, 1, f);
    }
    fclose(f);
    LOG_INFO("render-song: %s -> %s (%.1f s)", songPath.c_str(), outPath.c_str(), double(s->duration()));
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    EngineConfig cfg;
    GameOptions opts;
    bool windowFromArgs = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--map") opts.map = next();
        else if (a == "--autotest") opts.autotest = next();
        else if (a == "--trailer") opts.trailer = next();
        else if (a == "--record") opts.record = next();
        else if (a == "--render-song") {
            std::string song = next();
            return renderSongToWav(song, next());
        }
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
        else if (a == "--lang") opts.language = next() == "ja" ? 1 : 0;
        else if (a == "--pad") {
            std::string st = next();
            opts.padPreview = st == "ps" ? 1 : st == "switch" ? 2 : 0;
        }
        else if (a == "--aa") {
            std::string m = next();
            opts.antiAliasing = m == "off" ? 0 : m == "fxaa" ? 1 : 2;
        }
        else if (a == "--view") opts.cameraView = next();
        else if (a == "--bind-pose") opts.bindPose = true;
        else if (a == "--stance") opts.stance = next() == "goofy" ? 1 : 0;
        else if (a == "--help" || a == "-h") {
            SDL_Log("scoot would [--map scene.json] [--spawn label] [--challenge id] [--play] [--editor]\n"
                    "            [--menu main|play|map|rider|scooter[:category]|settings[:tab]|challenges]\n"
                    "            [--windowed|--fullscreen] [--size WxH] [--novsync] [--gpu direct3d12|vulkan] [--gpu-debug]\n"
                    "            [--lang en|ja] [--stance regular|goofy] [--pad xbox|ps|switch] [--no-rider]\n"
                    "            [--aa off|fxaa|taa] [--view third|close|far|first]\n"
                    "            [--env preset] [--autotest test.json] [--screenshot file.png frame] [--camera x,y,z,tx,ty,tz]\n"
                    "            [--debugview n] [--trailer script.json [--record out.mp4]] [--render-song song.json out.wav]");
            return 0;
        }
    }
    // window + presentation from the saved settings (command line wins; scripted runs use defaults)
    if (!windowFromArgs && opts.autotest.empty() && opts.screenshot.empty() && opts.trailer.empty() && fs::init()) {
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
    if (!opts.trailer.empty()) {
        // every frame is one frame of the video; the script sets the resolution unless --size did
        auto tj = fs::init() ? loadJsonFile(fs::resolve(opts.trailer)) : std::nullopt;
        int fps = tj ? jget<int>(*tj, "fps", 30) : 30;
        cfg.fixedFrameTime = 1.0f / float(fps);
        cfg.vsync = false;
        if (tj && !windowFromArgs) {
            cfg.width = jget<int>(*tj, "width", 1920);
            cfg.height = jget<int>(*tj, "height", 1080);
            cfg.windowMode = WindowMode::Windowed;
        }
    }
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
