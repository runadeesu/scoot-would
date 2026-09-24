// scoot would - landing evaluation + landing assist
#pragma once

#include "core/math.h"

#include <string>

namespace sw {

enum class LandingResult { Clean = 0, Normal, Sketchy, Bail };
enum class LandingAssist { Off = 0, Low, Normal };

const char* landingResultName(LandingResult r);

struct LandingInfo {
    LandingResult result = LandingResult::Clean;
    float rotationError = 0.0f;   // degrees between the deck up and the ground normal
    float headingError = 0.0f;    // degrees between the deck direction and the travel direction (0 or 180 = ok)
    float impact = 0.0f;          // m/s into the ground
    float verticalVelocity = 0.0f;
    bool fakie = false;
    bool trickIncomplete = false;
    float flipCompletion = 1.0f;
    std::string reason;
};

class LandingSystem {
public:
    LandingInfo evaluate(const Vec3& groundNormal, const Quat& bodyRot, const Vec3& velocity, bool tricksComplete, float completion) const;
    // gently rotate the body towards the landing surface shortly before touchdown; returns the new rotation
    Quat assist(const Quat& bodyRot, const Vec3& predictedNormal, float timeToLand, float dt) const;
    LandingAssist assistLevel = LandingAssist::Low;
    bool godMode = false;
};

}  // namespace sw
