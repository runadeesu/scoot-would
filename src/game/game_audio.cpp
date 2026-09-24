#include "game/game_audio.h"

#include "game/camera_controller.h"
#include "game/player/player.h"
#include "physics/physics_world.h"

namespace sw {

GameAudio::Loop& GameAudio::loop(std::vector<Loop>& set, const std::string& sound, Bus bus, bool spatial) {
    for (auto& l : set)
        if (l.sound == sound) return l;
    Loop l;
    l.sound = sound;
    PlayParams p;
    p.volume = 0.0f;
    p.spatial = spatial;
    p.position = playerPos_;
    p.minDistance = 3.0f;
    p.maxDistance = 60.0f;
    l.voice = audio().loop(sound, bus, p);
    set.push_back(l);
    return set.back();
}

void GameAudio::start(const std::string& environment) {
    stop();
    running_ = true;
    PlayParams p;
    p.volume = 0.0f;
    wind_ = audio().loop("wind", Bus::Environment, p);
    setEnvironment(environment);
}

void GameAudio::stop() {
    for (auto& l : rolls_) audio().stop(l.voice);
    for (auto& l : grinds_) audio().stop(l.voice);
    rolls_.clear();
    grinds_.clear();
    audio().stop(wind_);
    audio().stop(ambience_);
    wind_ = ambience_ = AudioSystem::kNoVoice;
    ambienceName_.clear();
    running_ = false;
}

void GameAudio::setEnvironment(const std::string& env) {
    std::string want = env == "night" ? "ambience_night" : "ambience_day";
    if (want == ambienceName_) return;
    audio().stop(ambience_);
    PlayParams p;
    p.volume = 0.55f;
    ambience_ = audio().loop(want, Bus::Environment, p);
    ambienceName_ = want;
}

void GameAudio::update(float dt, const Player& player, const CameraController& cam, bool paused) {
    if (!running_) return;
    Vec3 camFwd = cam.forward();
    audio().setListener(cam.position(), camFwd, Vec3(0, 1, 0), paused ? Vec3(0) : player.velocity());
    playerPos_ = player.position();
    Vec3 vel = player.velocity();
    float speed = paused ? 0.0f : player.speed();
    PlayerState st = player.state();
    bool rolling = !paused && st != PlayerState::Bailed && st != PlayerState::Grinding && player.scooter.valid() && player.scooter.grounded();
    std::string rollSound;
    if (rolling) rollSound = surfaces().get(player.scooter.groundSurface()).rollSound;
    // rolling: crossfade between surfaces; pitch and volume follow speed
    for (auto& l : rolls_) {
        float target = (l.sound == rollSound) ? saturate(speed / 7.0f) * 0.85f + (speed > 0.3f ? 0.1f : 0.0f) : 0.0f;
        if (st == PlayerState::Manual) target *= 0.6f;  // one wheel
        l.volume = dampf(l.volume, target, 14.0f, dt);
        audio().setVolume(l.voice, l.volume);
        audio().setPitch(l.voice, 0.65f + saturate(speed / 12.0f) * 0.75f);
        audio().setPosition(l.voice, playerPos_, vel);
    }
    if (rolling && !rollSound.empty()) loop(rolls_, rollSound, Bus::Sfx, true);
    // grinding
    std::string grindSound;
    if (!paused && st == PlayerState::Grinding) grindSound = surfaces().get(player.grind.surface).grindSound;
    for (auto& l : grinds_) {
        float target = l.sound == grindSound ? 0.55f + saturate(player.grind.speed / 8.0f) * 0.45f : 0.0f;
        l.volume = dampf(l.volume, target, l.sound == grindSound ? 30.0f : 12.0f, dt);
        audio().setVolume(l.voice, l.volume);
        audio().setPitch(l.voice, 0.8f + saturate(player.grind.speed / 10.0f) * 0.35f);
        audio().setPosition(l.voice, playerPos_, vel);
    }
    if (!grindSound.empty()) loop(grinds_, grindSound, Bus::Sfx, true);
    // wind: rises with speed and in the air
    float windTarget = paused ? 0.0f : saturate((speed - 4.0f) / 12.0f) * 0.8f + (st == PlayerState::Air ? 0.15f : 0.0f);
    windVol_ = dampf(windVol_, windTarget, 4.0f, dt);
    audio().setVolume(wind_, windVol_);
    audio().setPitch(wind_, 0.8f + saturate(speed / 18.0f) * 0.5f);
    audio().setVolume(ambience_, paused ? 0.25f : 0.55f);
}

void GameAudio::onEvent(const GameEvent& e) {
    if (!running_) return;
    PlayParams p;
    p.spatial = true;
    p.position = e.position;
    p.minDistance = 3.0f;
    p.maxDistance = 70.0f;
    switch (e.type) {
        case GameEventType::Push:
            p.volume = 0.7f;
            p.pitch = 0.92f + float(uint32_t(e.position.x * 1000.0f) % 17) / 100.0f;
            audio().play("push", Bus::Sfx, p);
            break;
        case GameEventType::Pop:
            p.volume = 0.6f + saturate(e.magnitude / 5.0f) * 0.4f;
            audio().play("pop", Bus::Sfx, p);
            break;
        case GameEventType::Land: {
            float m = e.magnitude;
            const char* s = m > 8.0f ? "land_hard" : m > 4.0f ? "land_normal" : "land_soft";
            p.volume = 0.5f + saturate(m / 10.0f) * 0.5f;
            p.pitch = e.surface == surfaces().find("wood") ? 0.85f : 1.0f;
            audio().play(s, Bus::Sfx, p);
            break;
        }
        case GameEventType::TrickStart:
            p.volume = 0.5f;
            if (e.text.find("Bar") != std::string::npos) audio().play("barspin", Bus::Sfx, p);
            else audio().play("whip", Bus::Sfx, p);
            break;
        case GameEventType::TrickLanded:
            p.volume = 0.6f;
            audio().play("catch", Bus::Sfx, p);
            audio().play("trick_land", Bus::Ui, PlayParams{0.45f});
            break;
        case GameEventType::GrindStart:
            p.volume = 0.8f;
            audio().play("scooter_hit", Bus::Sfx, p);
            break;
        case GameEventType::Bail:
            p.volume = 1.0f;
            audio().play("bail", Bus::Sfx, p);
            break;
        case GameEventType::Impact:
            p.volume = saturate(e.magnitude / 8.0f);
            audio().play("scooter_hit", Bus::Sfx, p);
            break;
        case GameEventType::Respawn: audio().play("respawn", Bus::Ui, PlayParams{0.5f}); break;
        case GameEventType::Checkpoint: audio().play("checkpoint", Bus::Ui, PlayParams{0.6f}); break;
        case GameEventType::ComboBanked:
            if (e.score >= 1500) audio().play("combo_bank", Bus::Ui, PlayParams{0.55f});
            break;
        case GameEventType::ComboFailed:
            if (e.score > 0) audio().play("combo_fail", Bus::Ui, PlayParams{0.5f});
            break;
        default: break;
    }
}

void GameAudio::ui(const char* name) { audio().play(name, Bus::Ui, PlayParams{0.7f}); }

}  // namespace sw
