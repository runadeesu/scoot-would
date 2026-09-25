// scoot would - scripted gameplay tests (deterministic fixed frame time)
//
// A test script (tests/autotest/*.json) spawns the player, feeds timed inputs, samples the
// player state and checks expectations (speed, state, landed tricks, grinds, bails).
// Used for physics / gameplay QA: flat, slope, stairs, bank, quarter, rail, jump, landing,
// backflip, 360, grind, manual, bail, high speed.
#pragma once

#include "core/json.h"
#include "game/player/player.h"

#include <string>
#include <vector>

namespace sw {

class Autotest {
public:
    bool load(const std::string& absPath);
    const std::string& map() const { return map_; }
    Vec3 spawnPos() const { return spawn_; }
    float spawnYaw() const { return yaw_; }
    Vec3 spawnVelocity() const { return spawnVel_; }
    // input for the given test time
    PlayerInput input(double t, std::deque<FlickEvent>& flicks, double gameTime, const Player& p);
    void observe(double t, float dt, Player& p);
    bool finished(double t) const { return t >= duration_; }
    bool report(Player& p);  // prints results, returns pass
    std::vector<float> screenshotTimes;
    const std::string& name() const { return name_; }
    std::string cameraMode;  // optional
    bool flowScheme() const { return flow_; }  // "scheme": "flow" drives the Scooter Flow layout

private:
    struct Step {
        float t = 0.0f;
        Json cmd;
    };
    struct Expect {
        float t = -1.0f;  // -1 = at the end
        Json cond;
        bool checked = false;
        bool pass = false;
        std::string detail;
    };
    bool check(const Json& cond, Player& p, std::string& detail);

    std::string name_, map_;
    Vec3 spawn_;
    Vec3 spawnVel_;
    float yaw_ = 0.0f;
    float duration_ = 5.0f;
    std::vector<Step> steps_;
    std::vector<Expect> expects_;
    size_t nextStep_ = 0;
    std::vector<Step> conditional_;
    std::vector<bool> conditionalFired_;
    void apply(const Json& c, std::deque<FlickEvent>& flicks, double gameTime);
    // held state
    Vec2 move_, look_;
    bool jumpHeld_ = false;
    float brake_ = 0.0f, grab_ = 0.0f, trick_ = 0.0f;
    bool flow_ = false;
    bool spinL_ = false, spinR_ = false;
    // pulses for this frame
    bool pushPulse_ = false, jumpPressPulse_ = false, jumpReleasePulse_ = false, revertPulse_ = false, respawnPulse_ = false;
    // observations
    std::vector<std::string> landedTricks_;
    std::vector<std::string> grinds_;
    std::vector<std::string> manuals_;
    std::vector<std::string> landings_;
    int bails_ = 0;
    float maxSpeed_ = 0.0f, maxAir_ = 0.0f, maxHeight_ = -1e9f;
    double lastLog_ = -1.0;
    std::vector<std::string> log_;
};

}  // namespace sw
