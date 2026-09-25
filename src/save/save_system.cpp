#include "save/save_system.h"

#include "core/filesystem.h"
#include "core/log.h"

namespace sw {

namespace {
SaveSystem g_saves;
}
SaveSystem& saves() { return g_saves; }

int SaveData::medals(int kind) const {
    int n = 0;
    for (auto& [id, r] : challenges) n += r.medal >= kind ? 1 : 0;
    return n;
}

Json customizationToJson(const Customization& c) {
    return {{"top", c.top},
            {"pants", c.pants},
            {"shoes", c.shoes},
            {"helmet", c.helmet},
            {"skinColor", toJson(c.skinColor)},
            {"topColor", toJson(c.topColor)},
            {"pantsColor", toJson(c.pantsColor)},
            {"shoesColor", toJson(c.shoesColor)},
            {"helmetColor", toJson(c.helmetColor)},
            {"deck", c.deck},
            {"bars", c.bars},
            {"wheels", c.wheels},
            {"clamp", c.clamp},
            {"deckColor", toJson(c.deckColor)},
            {"barsColor", toJson(c.barsColor)},
            {"wheelColor", toJson(c.wheelColor)},
            {"coreColor", toJson(c.coreColor)},
            {"gripColor", toJson(c.gripColor)},
            {"clampColor", toJson(c.clampColor)}};
}

Customization customizationFromJson(const Json& j) {
    Customization c;
    c.top = std::clamp(jget<int>(j, "top", c.top), 0, 2);
    c.pants = std::clamp(jget<int>(j, "pants", c.pants), 0, 1);
    c.shoes = std::clamp(jget<int>(j, "shoes", c.shoes), 0, 1);
    c.helmet = std::clamp(jget<int>(j, "helmet", c.helmet), 0, 2);
    c.skinColor = jvec3(j, "skinColor", c.skinColor);
    c.topColor = jvec3(j, "topColor", c.topColor);
    c.pantsColor = jvec3(j, "pantsColor", c.pantsColor);
    c.shoesColor = jvec3(j, "shoesColor", c.shoesColor);
    c.helmetColor = jvec3(j, "helmetColor", c.helmetColor);
    c.deck = std::clamp(jget<int>(j, "deck", c.deck), 0, 2);
    c.bars = std::clamp(jget<int>(j, "bars", c.bars), 0, 2);
    c.wheels = std::clamp(jget<int>(j, "wheels", c.wheels), 0, 2);
    c.clamp = std::clamp(jget<int>(j, "clamp", c.clamp), 0, 1);
    c.deckColor = jvec3(j, "deckColor", c.deckColor);
    c.barsColor = jvec3(j, "barsColor", c.barsColor);
    c.wheelColor = jvec3(j, "wheelColor", c.wheelColor);
    c.coreColor = jvec3(j, "coreColor", c.coreColor);
    c.gripColor = jvec3(j, "gripColor", c.gripColor);
    c.clampColor = jvec3(j, "clampColor", c.clampColor);
    return c;
}

Json SaveSystem::toJson(const Settings& s) {
    const auto& g = s.graphics;
    const auto& p = s.gameplay;
    const auto& a = s.audio;
    Json j;
    j["version"] = Settings::kVersion;
    j["graphics"] = {{"windowMode", g.windowMode},   {"width", g.width},          {"height", g.height},
                     {"vsync", g.vsync},             {"fpsLimit", g.fpsLimit},    {"renderScale", g.renderScale},
                     {"shadowQuality", g.shadowQuality}, {"textureQuality", g.textureQuality}, {"ssao", g.ssao},
                     {"bloom", g.bloom},             {"motionBlur", g.motionBlur}, {"antiAliasing", g.antiAliasing},
                     {"sharpen", g.sharpen},         {"drawDistance", g.drawDistance}, {"lodBias", g.lodBias},
                     {"fov", g.fov},                 {"brightness", g.brightness}, {"anisotropy", g.anisotropy},
                     {"particles", g.particles}};
    j["gameplay"] = {{"stance", p.stance}, {"controlScheme", p.controlScheme}, {"language", p.language}, {"landingAssist", p.landingAssist}, {"cameraSensitivity", p.cameraSensitivity}, {"invertY", p.invertY},
                     {"cameraShake", p.cameraShake},     {"cameraDistance", p.cameraDistance},       {"vibration", p.vibration},
                     {"showHud", p.showHud},             {"showTrickNames", p.showTrickNames},       {"metric", p.metric},
                     {"balanceDifficulty", p.balanceDifficulty}};
    j["audio"] = {{"master", a.master}, {"music", a.music}, {"sfx", a.sfx}, {"environment", a.environment}, {"ui", a.ui}, {"musicEnabled", a.musicEnabled}};
    return j;
}

Settings SaveSystem::settingsFromJson(const Json& j) {
    Settings s;
    int version = jget<int>(j, "version", 1);
    const Json& g = j.contains("graphics") ? j["graphics"] : Json::object();
    const Json& p = j.contains("gameplay") ? j["gameplay"] : Json::object();
    const Json& a = j.contains("audio") ? j["audio"] : Json::object();
    auto& G = s.graphics;
    G.windowMode = std::clamp(jget<int>(g, "windowMode", G.windowMode), 0, 2);
    G.width = std::max(640, jget<int>(g, "width", G.width));
    G.height = std::max(360, jget<int>(g, "height", G.height));
    G.vsync = jget<bool>(g, "vsync", G.vsync);
    G.fpsLimit = std::max(0, jget<int>(g, "fpsLimit", G.fpsLimit));
    G.renderScale = clampf(jget<float>(g, "renderScale", G.renderScale), 0.5f, 2.0f);
    G.shadowQuality = std::clamp(jget<int>(g, "shadowQuality", G.shadowQuality), 0, 4);
    G.textureQuality = std::clamp(jget<int>(g, "textureQuality", G.textureQuality), 0, 3);
    G.ssao = jget<bool>(g, "ssao", G.ssao);
    G.bloom = jget<bool>(g, "bloom", G.bloom);
    G.motionBlur = jget<bool>(g, "motionBlur", G.motionBlur);
    if (g.contains("antiAliasing"))
        G.antiAliasing = std::clamp(jget<int>(g, "antiAliasing", G.antiAliasing), 0, 2);
    else if (g.contains("fxaa"))
        G.antiAliasing = jget<bool>(g, "fxaa", true) ? 2 : 0;  // settings from before TAA
    G.sharpen = jget<bool>(g, "sharpen", G.sharpen);
    G.drawDistance = clampf(jget<float>(g, "drawDistance", G.drawDistance), 150.0f, 1500.0f);
    G.lodBias = clampf(jget<float>(g, "lodBias", G.lodBias), 0.25f, 4.0f);
    G.fov = clampf(jget<float>(g, "fov", G.fov), 55.0f, 100.0f);
    G.brightness = clampf(jget<float>(g, "brightness", G.brightness), 0.5f, 1.8f);
    G.anisotropy = std::clamp(jget<int>(g, "anisotropy", G.anisotropy), 1, 16);
    G.particles = jget<bool>(g, "particles", G.particles);
    auto& P = s.gameplay;
    P.stance = std::clamp(jget<int>(p, "stance", P.stance), 0, 1);
    P.controlScheme = std::clamp(jget<int>(p, "controlScheme", P.controlScheme), 0, 1);
    P.language = std::clamp(jget<int>(p, "language", P.language), -1, 1);
    P.landingAssist = std::clamp(jget<int>(p, "landingAssist", P.landingAssist), 0, 2);
    P.cameraSensitivity = clampf(jget<float>(p, "cameraSensitivity", P.cameraSensitivity), 0.2f, 3.0f);
    P.invertY = jget<bool>(p, "invertY", P.invertY);
    P.cameraShake = std::clamp(jget<int>(p, "cameraShake", P.cameraShake), 0, 2);
    P.cameraDistance = clampf(jget<float>(p, "cameraDistance", P.cameraDistance), 0.7f, 1.5f);
    P.vibration = jget<bool>(p, "vibration", P.vibration);
    P.showHud = jget<bool>(p, "showHud", P.showHud);
    P.showTrickNames = jget<bool>(p, "showTrickNames", P.showTrickNames);
    P.metric = jget<bool>(p, "metric", P.metric);
    P.balanceDifficulty = clampf(jget<float>(p, "balanceDifficulty", P.balanceDifficulty), 0.5f, 1.5f);
    auto& A = s.audio;
    A.master = clampf(jget<float>(a, "master", A.master), 0.0f, 1.0f);
    A.music = clampf(jget<float>(a, "music", A.music), 0.0f, 1.0f);
    A.sfx = clampf(jget<float>(a, "sfx", A.sfx), 0.0f, 1.0f);
    A.environment = clampf(jget<float>(a, "environment", A.environment), 0.0f, 1.0f);
    A.ui = clampf(jget<float>(a, "ui", A.ui), 0.0f, 1.0f);
    A.musicEnabled = jget<bool>(a, "musicEnabled", A.musicEnabled);
    // migrations
    if (version < 2) {
        // v1 stored the landing assist as a bool and the fps limit as -1 for unlimited
        if (p.contains("assist") && p["assist"].is_boolean()) P.landingAssist = p["assist"].get<bool>() ? 2 : 0;
        if (jget<int>(g, "fpsLimit", 0) < 0) G.fpsLimit = 0;
        LOG_INFO("save: migrated settings v%d -> v%d", version, Settings::kVersion);
    }
    return s;
}

Json SaveSystem::toJson(const SaveData& d) {
    Json j;
    j["version"] = SaveData::kVersion;
    j["customization"] = customizationToJson(d.custom);
    Json ch = Json::object();
    for (auto& [id, r] : d.challenges) ch[id] = {{"best", r.best}, {"medal", r.medal}, {"attempts", r.attempts}};
    j["challenges"] = ch;
    j["lastMap"] = d.lastMap;
    j["lastSpawn"] = d.lastSpawn;
    j["stats"] = {{"playTime", d.playTime},       {"distance", d.distance}, {"tricksLanded", d.tricksLanded}, {"bails", d.bails},
                  {"bestCombo", d.bestCombo},     {"bestAirTime", d.bestAirTime}, {"topSpeed", d.topSpeed}};
    return j;
}

SaveData SaveSystem::dataFromJson(const Json& j) {
    SaveData d;
    if (j.contains("customization")) d.custom = customizationFromJson(j["customization"]);
    if (j.contains("challenges") && j["challenges"].is_object())
        for (auto& [id, r] : j["challenges"].items()) {
            ChallengeRecord rec;
            rec.best = jget<int>(r, "best", 0);
            rec.medal = std::clamp(jget<int>(r, "medal", 0), 0, 3);
            rec.attempts = jget<int>(r, "attempts", 0);
            d.challenges[id] = rec;
        }
    d.lastMap = jget<std::string>(j, "lastMap", d.lastMap);
    d.lastSpawn = jget<std::string>(j, "lastSpawn", "");
    if (j.contains("stats")) {
        const Json& s = j["stats"];
        d.playTime = jget<double>(s, "playTime", 0.0);
        d.distance = jget<double>(s, "distance", 0.0);
        d.tricksLanded = jget<int>(s, "tricksLanded", 0);
        d.bails = jget<int>(s, "bails", 0);
        d.bestCombo = jget<int>(s, "bestCombo", 0);
        d.bestAirTime = jget<float>(s, "bestAirTime", 0.0f);
        d.topSpeed = jget<float>(s, "topSpeed", 0.0f);
    }
    return d;
}

void SaveSystem::init() {
    if (auto j = loadJsonFile(fs::userPath("settings.json"))) settings_ = settingsFromJson(*j);
    else saveSettings();
    if (auto j = loadJsonFile(fs::userPath("save.json"))) data_ = dataFromJson(*j);
    else if (auto b = loadJsonFile(fs::userPath("save.json.bak"))) {
        LOG_WARN("save: save.json unreadable, restored the backup");
        data_ = dataFromJson(*b);
    }
    LOG_INFO("save: settings + save loaded (%zu challenge records)", data_.challenges.size());
}

bool SaveSystem::saveSettings() {
    bool ok = saveJsonFile(fs::userPath("settings.json"), toJson(settings_), true);
    if (!ok) LOG_ERROR("save: cannot write settings.json");
    return ok;
}

bool SaveSystem::saveData() {
    bool ok = saveJsonFile(fs::userPath("save.json"), toJson(data_), true);
    if (ok) {
        dirty_ = false;
        sinceSave_ = 0.0f;
    } else {
        LOG_ERROR("save: cannot write save.json");
    }
    return ok;
}

void SaveSystem::autosave(float dt, float interval) {
    timer_ += dt;
    sinceSave_ += dt;
    if (dirty_ && timer_ >= interval) {
        timer_ = 0.0f;
        saveData();
    }
}

bool SaveSystem::flush() {
    if (!dirty_) return true;
    return saveData();
}

}  // namespace sw
