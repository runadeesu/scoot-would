// scoot would - gameplay audio: rolling loop per surface (pitch / volume by speed), grind loops,
// wind by speed, ambience by environment, and one shots for gameplay events
#pragma once

#include "audio/audio.h"
#include "game/events.h"

#include <string>
#include <vector>

namespace sw {

class Player;
class CameraController;

class GameAudio {
public:
    void start(const std::string& environment);
    void stop();
    void setEnvironment(const std::string& environment);
    void update(float dt, const Player& player, const CameraController& cam, bool paused);
    void onEvent(const GameEvent& e);
    void ui(const char* name);  // menu sounds

private:
    struct Loop {
        std::string sound;
        AudioSystem::Voice voice = AudioSystem::kNoVoice;
        float volume = 0.0f;
    };
    Loop& loop(std::vector<Loop>& set, const std::string& sound, Bus bus, bool spatial);
    std::vector<Loop> rolls_, grinds_;
    AudioSystem::Voice wind_ = AudioSystem::kNoVoice, ambience_ = AudioSystem::kNoVoice;
    std::string ambienceName_;
    float windVol_ = 0.0f;
    Vec3 playerPos_;
    bool running_ = false;
};

}  // namespace sw
