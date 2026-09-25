// scoot would - player: scooter physics + tricks + grinds + manuals + landing + combo + bail
#pragma once

#include "core/math.h"
#include "game/combo/combo_manager.h"
#include "game/events.h"
#include "game/grind/grind_system.h"
#include "game/manual/manual_system.h"
#include "game/player/landing.h"
#include "game/player/ragdoll.h"
#include "game/scooter/scooter_physics.h"
#include "game/tricks/trick_system.h"
#include "input/input.h"

#include <deque>
#include <string>
#include <vector>

namespace sw {

enum class PlayerState { Riding = 0, Air, Grinding, Manual, Bailed };
const char* playerStateName(PlayerState s);

struct PlayerInput {
    Vec2 move;              // left stick (x steer / spin, y manual / flip)
    Vec2 look;              // right stick
    bool jumpDown = false;
    bool jumpPressed = false;
    bool jumpReleased = false;
    bool pushPressed = false;
    float brake = 0.0f;
    bool spinLeft = false, spinRight = false;
    float grab = 0.0f;
    bool revertPressed = false;
    bool respawnPressed = false;
    bool checkpointPressed = false;
    StickDir rightDir = StickDir::None;
    // Scooter Flow layout: the bare right stick rotates the rider in the air (spin x, flip y)
    bool flow = false;
    Vec2 rotate;
    float trickMod = 0.0f;  // RT held (Scooter Flow layout)
};

struct PlayerSettings {
    float balanceScale = 1.0f;
    bool godMode = false;
    LandingAssist assist = LandingAssist::Low;
};

class Player {
public:
    bool init();
    void spawn(const Vec3& pos, const Quat& rot);
    void despawn();
    void fixedUpdate(float dt, const PlayerInput& in, std::deque<FlickEvent>& flicks, double time);
    void postPhysics(float dt);
    void bail(const std::string& reason);
    void respawn(bool atCheckpoint);
    void setCheckpoint();
    void applySettings(const PlayerSettings& s);

    PlayerState state() const { return state_; }
    bool spawned() const { return scooter.valid() || state_ == PlayerState::Bailed; }
    // interpolated body transform for rendering
    Transform renderTransform(float alpha) const;
    Vec3 position() const { return scooter.valid() ? scooter.position() : ragdoll.centre(); }
    Vec3 velocity() const { return scooter.valid() ? scooter.velocity() : ragdoll.velocity(); }
    float speed() const { return velocity().length(); }
    float crouch() const { return crouch_; }
    float bailTime() const { return bailTimer_; }
    const std::string& bailReason() const { return bailReason_; }
    const LandingInfo& lastLanding() const { return lastLanding_; }
    float lastLandingAge() const { return landingAge_; }
    const std::string& lastTrick() const { return lastTrick_; }
    int lastTrickScore() const { return lastTrickScore_; }
    float lastTrickAge() const { return trickAge_; }
    float airTime() const { return airTimer_; }
    float maxAirHeight() const { return airPeak_; }
    bool fakie() const { return scooter.valid() && scooter.fakie(); }
    std::vector<GameEvent>& events() { return events_; }
    Vec3 checkpointPosition() const { return checkpointPos_; }
    bool hasCheckpoint() const { return hasCheckpoint_; }
    Vec3 spawnPosition() const { return spawnPos_; }
    Quat spawnRotation() const { return spawnRot_; }
    void setSpawn(const Vec3& p, const Quat& r) { spawnPos_ = p; spawnRot_ = r; }
    float timeToLand() const { return timeToLand_; }
    Vec3 predictedLandingNormal() const { return landNormal_; }
    int totalTricksLanded() const { return tricksLanded_; }
    int bails() const { return bails_; }

    ScooterPhysics scooter;
    TrickSystem tricks;
    GrindSystem grinds;
    ManualSystem manuals;
    ComboManager combo;
    LandingSystem landing;
    Ragdoll ragdoll;
    GrindState grind;
    ManualState manual;
    PlayerSettings settings;
    std::vector<Transform> riderPose;  // current animated joints (model space), written by the rider animation

private:
    void enterAir(bool popped);
    void handleLanding();
    void doPop(float timingBonus);
    void predictLanding();
    void finishGrind(GrindExit exit);
    void finishManual(ManualExit exit);
    void emit(GameEventType t, float magnitude = 0.0f, const std::string& text = "", int score = 0);

    PlayerState state_ = PlayerState::Riding;
    int seenBanks_ = 0, seenFails_ = 0;
    Vec3 prevPos_, curPos_;
    Quat prevRot_, curRot_;
    float crouch_ = 0.0f;
    float crouchHold_ = 0.0f;
    bool jumpLatch_ = false;
    float coyote_ = 0.0f;
    float airTimer_ = 0.0f;
    bool airRotateArmed_ = false;  // Scooter Flow layout: the pop flick must return before the stick rotates
    Vec3 pumpNormal_{0, 1, 0};
    float airPeak_ = 0.0f;
    float airStartY_ = 0.0f;
    float bailTimer_ = 0.0f;
    std::string bailReason_;
    LandingInfo lastLanding_;
    float landingAge_ = 100.0f;
    std::string lastTrick_;
    int lastTrickScore_ = 0;
    float trickAge_ = 100.0f;
    float timeToLand_ = 10.0f;
    Vec3 landNormal_{0, 1, 0};
    Vec3 safePos_;
    Quat safeRot_;
    float safeTimer_ = 0.0f;
    Vec3 spawnPos_;
    Quat spawnRot_;
    Vec3 checkpointPos_;
    Quat checkpointRot_;
    bool hasCheckpoint_ = false;
    bool justLanded_ = false;
    float landedTimer_ = 0.0f;
    float revertTimer_ = 0.0f;
    std::vector<GameEvent> events_;
    int tricksLanded_ = 0;
    int bails_ = 0;
    bool wasPushing_ = false;
};

}  // namespace sw
