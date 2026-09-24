#include "game/manual/manual_system.h"

namespace sw {

const char* manualName(const ManualState& st) { return st.nose ? "Nose Manual" : "Manual"; }

bool ManualSystem::tryEnter(float dt, float stickY, float speed, bool bothWheels, bool landing, ManualState& st) {
    if (st.active) return false;
    bool wantManual = stickY < -0.55f, wantNose = stickY > 0.55f;
    if ((!wantManual && !wantNose) || speed < 1.0f || !bothWheels) {
        st.enterHold = 0.0f;
        return false;
    }
    st.enterHold += dt;
    // on landing the stick can already be held (landing into a manual)
    if (st.enterHold < (landing ? 0.0f : 0.07f)) return false;
    st = ManualState{};
    st.active = true;
    st.nose = wantNose;
    st.balance = landing ? 0.25f * (wantNose ? -1.0f : 1.0f) : 0.0f;
    return true;
}

float ManualSystem::pitchTarget(const ManualState& st) const {
    float base = st.nose ? 11.0f : 15.0f;
    return (base + st.balance * 10.0f) * kDeg2Rad;
}

ManualExit ManualSystem::update(float dt, float stickY, bool jumpPressed, bool grounded, float speed, ManualState& st, float balanceScale) {
    if (!st.active) return ManualExit::None;
    st.time += dt;
    if (jumpPressed) {
        st.active = false;
        return ManualExit::Jump;
    }
    if (!grounded) {
        st.active = false;
        return ManualExit::Airborne;
    }
    // stick released: the rider puts the wheel down cleanly
    if (std::fabs(stickY) < 0.2f) {
        st.active = false;
        return ManualExit::Released;
    }
    // balance around a stick "sweet spot" (60 % deflection); deviation tips the scooter
    float sign = st.nose ? 1.0f : -1.0f;  // manual uses stick down, nose manual stick up
    float deflect = stickY * sign;        // 0..1 in the right direction
    float control = (deflect - 0.62f) * 4.2f;
    noiseT_ += dt;
    float drift = (std::sin(noiseT_ * 2.3f) * 0.6f + std::sin(noiseT_ * 4.1f + 2.0f) * 0.4f) * 0.9f;
    float inst = (st.nose ? 1.25f : 1.0f) * balanceScale * (1.0f + std::min(st.time, 6.0f) * 0.06f);
    st.balanceVel += (st.balance * 1.6f * inst + drift * 0.8f * inst + control) * dt;
    st.balanceVel *= std::exp(-2.2f * dt);
    st.balance += st.balanceVel * dt;
    if (speed < 0.6f) {
        st.active = false;
        return ManualExit::Dropped;
    }
    if (st.balance < -1.0f) {
        st.active = false;
        return ManualExit::Dropped;  // tipped forward onto both wheels (ends, no bail)
    }
    if (st.balance > 1.0f) {
        if (godMode) {
            st.balance = 0.95f;
        } else {
            st.active = false;
            return ManualExit::Bail;  // looped out
        }
    }
    return ManualExit::None;
}

}  // namespace sw
