// scoot would - grinding: rails from the scene, detection, grind types, balance, exits
#pragma once

#include "core/math.h"
#include "scene/scene.h"

#include <string>
#include <vector>

namespace sw {

class ScooterPhysics;

struct Rail {
    std::vector<Vec3> points;  // world space
    std::vector<float> cumulative;
    float length = 0.0f;
    RailType type = RailType::Round;
    int surface = 0;
    float radius = 0.025f;
    uint32_t entity = 0;
    AABB bounds;
    Vec3 pointAt(float s) const;
    Vec3 tangentAt(float s) const;
    // closest parameter s on the rail to p, returns distance
    float closest(const Vec3& p, float& sOut) const;
};

enum class GrindType { FiftyFifty = 0, Nosegrind, FiveO, Feeble, Smith, Crooked, Boardslide, Count };

struct GrindTypeInfo {
    const char* name;
    int contact;       // 0 deck centre, 1 front wheel, 2 rear wheel
    float yaw, pitch, roll;  // pose relative to the rail frame (degrees)
    float difficulty;  // balance instability multiplier
    int scorePerSecond;
    float friction;    // multiplier
};
const GrindTypeInfo& grindInfo(GrindType t);

struct GrindState {
    bool active = false;
    int rail = -1;
    float s = 0.0f;          // distance along the rail
    float dir = 1.0f;        // travel direction along the rail
    float speed = 0.0f;
    GrindType type = GrindType::FiftyFifty;
    float balance = 0.0f;    // -1..1, |b| > 1 falls off
    float balanceVel = 0.0f;
    float time = 0.0f;
    float side = 1.0f;       // which side of the rail the rider faces (feeble / smith dip side)
    bool fakie = false;      // riding the rail backwards (scooter faces against travel)
    Vec3 entryOffset;        // blended out after attaching (assist, no teleport)
    Quat entryRotOffset;
    float segmentTime = 0.0f;   // time in the current grind type
    std::vector<std::pair<GrindType, float>> segments;  // grind type switches during this grind
    int surface = 0;
};

enum class GrindExit { None, RailEnd, Jump, Side, Bail, TooSlow };

class GrindSystem {
public:
    void rebuild(Scene& scene);
    const std::vector<Rail>& rails() const { return rails_; }

    // look for a rail under the scooter. Returns true and fills the state when attaching.
    bool tryAttach(const ScooterPhysics& sc, const Vec2& stick, bool airborne, GrindState& st);
    // per physics step; drives the scooter body along the rail
    GrindExit update(float dt, const Vec2& stick, bool jumpPressed, GrindState& st, ScooterPhysics& sc, float balanceScale);
    Quat grindRotation(const GrindState& st) const;
    Vec3 contactLocal(GrindType t) const;
    // debug
    void debugDraw(const Vec3& near, float radius) const;
    std::string lastRejectReason;
    float cooldown = 0.0f;
    bool godMode = false;

private:
    std::vector<Rail> rails_;
    float noiseT_ = 0.0f;
    float sideHold_ = 0.0f;
};

}  // namespace sw
