// scoot would - settings + save data
//
// Two versioned JSON files in the user folder:
//   settings.json  graphics / gameplay / audio options (applied immediately when changed)
//   save.json      customization, challenge progress, stats, last map
// Files are written atomically with a .bak copy of the previous version; unknown or missing keys
// fall back to defaults, and older versions are migrated on load.
#pragma once

#include "core/json.h"
#include "core/math.h"
#include "game/player/player_visual.h"

#include <map>
#include <string>

namespace sw {

struct GraphicsSettings {
    int windowMode = 2;          // 0 windowed, 1 fullscreen, 2 borderless
    int width = 1920, height = 1080;
    bool vsync = true;
    int fpsLimit = 0;            // 0 = unlimited, else 30/60/90/120/144/165/240
    float renderScale = 1.0f;
    int shadowQuality = 3;       // 0 off .. 4 ultra
    int textureQuality = 3;      // 0 low (512) .. 3 ultra (2048)
    bool ssao = true;
    bool bloom = true;
    bool motionBlur = false;
    bool fxaa = true;
    bool sharpen = true;
    float drawDistance = 700.0f;
    float lodBias = 1.0f;
    float fov = 72.0f;
    float brightness = 1.0f;
    int anisotropy = 8;
    bool particles = true;
};

struct GameplaySettings {
    int landingAssist = 1;       // 0 off, 1 low, 2 normal
    float cameraSensitivity = 1.0f;
    bool invertY = false;
    int cameraShake = 2;         // 0 off, 1 low, 2 normal
    float cameraDistance = 1.0f;
    bool vibration = true;
    bool showHud = true;
    bool showTrickNames = true;
    bool metric = true;          // km/h vs mph
    float balanceDifficulty = 1.0f;
};

struct AudioSettings {
    float master = 0.9f, music = 0.6f, sfx = 1.0f, environment = 0.8f, ui = 0.8f;
    bool musicEnabled = true;
};

struct Settings {
    static constexpr int kVersion = 2;
    GraphicsSettings graphics;
    GameplaySettings gameplay;
    AudioSettings audio;
};

struct ChallengeRecord {
    int best = 0;       // score, or time in ms for time attacks (lower is better)
    int medal = 0;      // 0 none, 1 bronze, 2 silver, 3 gold
    int attempts = 0;
};

struct SaveData {
    static constexpr int kVersion = 1;
    Customization custom;
    std::map<std::string, ChallengeRecord> challenges;
    std::string lastMap = "assets/scenes/city.json";
    std::string lastSpawn;
    // lifetime stats
    double playTime = 0.0;
    double distance = 0.0;       // metres
    int tricksLanded = 0;
    int bails = 0;
    int bestCombo = 0;
    float bestAirTime = 0.0f;
    float topSpeed = 0.0f;
    int medals(int kind) const;
};

class SaveSystem {
public:
    void init();
    Settings& settings() { return settings_; }
    SaveData& data() { return data_; }
    bool saveSettings();
    bool saveData();
    void markDirty() { dirty_ = true; }
    // call every frame; writes the save at most every `interval` seconds when dirty
    void autosave(float dt, float interval = 30.0f);
    bool flush();  // write pending changes now (quit, menus)
    float lastSaveAge() const { return sinceSave_; }

    static Json toJson(const Settings& s);
    static Settings settingsFromJson(const Json& j);
    static Json toJson(const SaveData& d);
    static SaveData dataFromJson(const Json& j);

private:
    Settings settings_;
    SaveData data_;
    bool dirty_ = false;
    float timer_ = 0.0f, sinceSave_ = 1e9f;
};

SaveSystem& saves();

Json customizationToJson(const Customization& c);
Customization customizationFromJson(const Json& j);

}  // namespace sw
