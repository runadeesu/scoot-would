#include "game/player/landing.h"

namespace sw {

const char* landingResultName(LandingResult r) {
    switch (r) {
        case LandingResult::Clean: return "CLEAN";
        case LandingResult::Normal: return "LANDED";
        case LandingResult::Sketchy: return "SKETCHY";
        case LandingResult::Bail: return "BAIL";
    }
    return "";
}

LandingInfo LandingSystem::evaluate(const Vec3& n, const Quat& rot, const Vec3& vel, bool tricksComplete, float completion) const {
    LandingInfo li;
    Vec3 up = rot * Vec3(0, 1, 0);
    Vec3 fwd = rot * Vec3(0, 0, -1);
    li.rotationError = angleBetween(up, n) * kRad2Deg;
    Vec3 vp = projectOnPlane(vel, n);
    Vec3 fp = projectOnPlane(fwd, n);
    float heading = 0.0f;
    if (vp.length() > 1.2f && fp.length() > 0.1f) heading = angleBetween(fp, vp) * kRad2Deg;
    li.fakie = heading > 90.0f;
    li.headingError = li.fakie ? 180.0f - heading : heading;
    li.impact = std::max(0.0f, -dot(vel, n));
    li.verticalVelocity = vel.y;
    li.trickIncomplete = !tricksComplete;
    li.flipCompletion = completion;

    float extra = assistLevel == LandingAssist::Normal ? 10.0f : assistLevel == LandingAssist::Low ? 5.0f : 0.0f;
    float rot1 = 12.0f + extra, rot2 = 26.0f + extra, rot3 = 42.0f + extra;
    float hd1 = 14.0f + extra, hd2 = 28.0f + extra, hd3 = 48.0f + extra;
    if (li.trickIncomplete) {
        li.result = LandingResult::Bail;
        li.reason = "trick not finished";
    } else if (li.rotationError > rot3) {
        li.result = LandingResult::Bail;
        li.reason = "bad landing angle";
    } else if (li.headingError > hd3) {
        li.result = LandingResult::Bail;
        li.reason = "landed sideways";
    } else if (li.impact > 12.5f) {
        li.result = LandingResult::Bail;
        li.reason = "impact too high";
    } else if (li.rotationError > rot2 || li.headingError > hd2 || li.impact > 9.5f) {
        li.result = LandingResult::Sketchy;
    } else if (li.rotationError > rot1 || li.headingError > hd1 || li.impact > 7.0f) {
        li.result = LandingResult::Normal;
    } else {
        li.result = LandingResult::Clean;
    }
    if (godMode && li.result == LandingResult::Bail) {
        li.result = LandingResult::Sketchy;
        li.reason += " (god mode)";
    }
    return li;
}

Quat LandingSystem::assist(const Quat& rot, const Vec3& n, float timeToLand, float dt) const {
    if (assistLevel == LandingAssist::Off || timeToLand > 0.3f) return rot;
    Vec3 up = rot * Vec3(0, 1, 0);
    float err = angleBetween(up, n);
    // only small corrections: never a full automatic landing
    float maxErr = (assistLevel == LandingAssist::Normal ? 40.0f : 28.0f) * kDeg2Rad;
    if (err < 1e-3f || err > maxErr) return rot;
    float rate = assistLevel == LandingAssist::Normal ? 2.6f : 1.4f;  // rad/s
    float stepAng = std::min(err, rate * dt);
    Vec3 axis = cross(up, n).normalized();
    return (Quat::angleAxis(stepAng, axis) * rot).normalized();
}

}  // namespace sw
