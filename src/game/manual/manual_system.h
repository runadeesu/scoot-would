// scoot would - manuals (rear wheel) and nose manuals (front wheel) with balance
#pragma once

#include "core/math.h"

namespace sw {

struct ManualState {
    bool active = false;
    bool nose = false;
    float balance = 0.0f;     // -1..1: + = too far back (rear falls = bail), - = drops to the wheels
    float balanceVel = 0.0f;
    float time = 0.0f;
    float enterHold = 0.0f;   // stick hold time before entering
};

enum class ManualExit { None, Released, Dropped, Bail, Jump, Airborne };

class ManualSystem {
public:
    // stickY: left stick vertical (+ up). returns true when a manual starts
    bool tryEnter(float dt, float stickY, float speed, bool bothWheels, bool landing, ManualState& st);
    ManualExit update(float dt, float stickY, bool jumpPressed, bool grounded, float speed, ManualState& st, float balanceScale);
    float pitchTarget(const ManualState& st) const;  // radians
    bool godMode = false;

private:
    float noiseT_ = 0.0f;
};

const char* manualName(const ManualState& st);

}  // namespace sw
