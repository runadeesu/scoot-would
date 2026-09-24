// scoot would - game modes: Free Ride, Trick Challenge, Line Challenge, Best Trick, Time Attack
//
// Challenges are data (assets/data/challenges.json). Each one names its map, a spawn, a time
// limit, three medal targets and, depending on the mode, a trick list or a chain of gates. Gates can
// require the rider to pass them grinding, in a manual or in the air. Results are stored in the save.
#pragma once

#include "core/json.h"
#include "core/math.h"
#include "game/events.h"
#include "render/render_scene.h"

#include <string>
#include <vector>

namespace sw {

class Player;

enum class ModeType { FreeRide = 0, Trick, Line, BestTrick, TimeAttack, Count };
const char* modeName(ModeType t);
const char* modeDescription(ModeType t);

struct Gate {
    Vec3 position;
    float radius = 3.0f;
    float yaw = 0.0f;        // degrees, ring facing
    std::string require;     // "", "air", "grind", "manual"
};

struct ChallengeDef {
    std::string id, name, description;
    std::string map, spawn;
    ModeType type = ModeType::Trick;
    float timeLimit = 90.0f;           // seconds (0 = none)
    int targets[3] = {0, 0, 0};        // bronze / silver / gold (score, or ms for timed modes)
    bool lowerIsBetter = false;        // time attack / line: faster is better
    std::vector<Gate> gates;
    std::vector<std::string> trickList; // trick challenge: land every one of these
};

enum class ModeState { Idle, Countdown, Running, Finished };

struct ModeResult {
    int value = 0;          // score or milliseconds
    int medal = 0;
    bool completed = false; // objective met (all gates / all listed tricks)
    bool newBest = false;
    std::string reason;     // "Time up", "Bailed", ...
};

class ModeManager {
public:
    void load();
    const std::vector<ChallengeDef>& challenges() const { return defs_; }
    const ChallengeDef* find(const std::string& id) const;
    std::vector<const ChallengeDef*> forMap(const std::string& map, ModeType t) const;

    void startFreeRide();
    void start(const ChallengeDef& def, RenderScene& rs);
    void stop(RenderScene* rs);
    // restart the current challenge (player respawn is done by the caller)
    void restart(RenderScene& rs);

    void update(float dt, Player& player, RenderScene& rs);
    void onEvent(const GameEvent& e, Player& player);

    ModeType type() const { return def_ ? def_->type : ModeType::FreeRide; }
    ModeState state() const { return state_; }
    const ChallengeDef* current() const { return def_; }
    bool inChallenge() const { return def_ != nullptr; }
    float countdown() const { return countdown_; }
    float elapsed() const { return elapsed_; }
    float remaining() const { return def_ && def_->timeLimit > 0 ? std::max(0.0f, def_->timeLimit - elapsed_) : 0.0f; }
    int score() const { return score_; }
    int bestCombo() const { return bestCombo_; }
    int nextGate() const { return nextGate_; }
    int gateCount() const { return def_ ? int(def_->gates.size()) : 0; }
    const std::vector<bool>& tricksDone() const { return tricksDone_; }
    const ModeResult& result() const { return result_; }
    float finishedTime() const { return finishedTime_; }
    std::string objectiveText() const;
    std::string gateMessage;   // e.g. "Grind through this gate!"
    float gateMessageTime = 10.0f;
    static int medalFor(const ChallengeDef& d, int value);

private:
    void finish(bool completed, const std::string& reason, Player& player);
    void buildGates(RenderScene& rs);
    void clearGates(RenderScene* rs);
    void refreshGateVisuals(RenderScene& rs);

    std::vector<ChallengeDef> defs_;
    const ChallengeDef* def_ = nullptr;
    ModeState state_ = ModeState::Idle;
    float countdown_ = 0.0f, elapsed_ = 0.0f, finishedTime_ = 0.0f;
    int score_ = 0, bestCombo_ = 0, nextGate_ = 0;
    long long bankedAtStart_ = 0;
    std::vector<bool> tricksDone_;
    std::vector<RenderScene::Handle> gateHandles_;
    ModeResult result_;
    Vec3 lastPos_;
    bool lastPosValid_ = false;
};

}  // namespace sw
