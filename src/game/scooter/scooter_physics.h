// scoot would - scooter physics
//
// One Jolt rigid body represents rider + scooter (mass, centre of mass, inertia, CCD).
// Two sphere-cast wheels provide suspension, rolling and lateral grip; the front wheel
// is steered (bicycle model). Push / brake / rolling resistance act along the contact
// tangent, so slopes, banks and transitions come from real forces instead of scripted
// position changes. In the air the rider rotates the body with angular acceleration.
#pragma once

#include "core/math.h"
#include "physics/physics_world.h"

namespace sw {

struct WheelContact {
    bool contact = false;       // touching (within tolerance)
    bool near = false;          // within snap range
    Vec3 point;                 // contact point on the ground
    Vec3 normal{0, 1, 0};
    Vec3 groundVelocity;
    float compression = 0.0f;   // m, positive = compressed
    float compressionVel = 0.0f;
    float load = 0.0f;          // N
    int surface = 0;
    uint32_t entity = 0;
    Vec3 axleWorld;
};

struct ScooterTuning {
    float mass = 72.0f;
    float wheelRadius = 0.055f;
    float wheelBase = 0.56f;        // axle to axle
    float comHeight = 0.62f;        // above the axles
    float springK = 42000.0f;       // N/m per wheel
    float springDamping = 2600.0f;  // N/(m/s)
    float castLift = 0.3f;
    float snapRange = 0.14f;
    float maxCompression = 0.08f;
    // riding
    float pushImpulse = 2.1f;       // m/s gained per push at low speed
    float maxPushSpeed = 9.5f;      // pushing stops helping around here
    // one push: the foot reaches the ground (pushReach), drives (pushDuration), returns to the tail; a new push
    // can start after pushCooldown (the whole cycle)
    float pushReach = 0.15f;
    float pushDuration = 0.2f;
    float pushCooldown = 0.6f;
    float brakeDecel = 4.5f;        // m/s^2 at full brake
    float airDrag = 0.0045f;        // per (m/s)^2
    float maxSteerLow = 0.52f;      // rad at walking speed
    float maxSteerHigh = 0.06f;     // rad at high speed
    float maxLateralAccel = 8.5f;   // limits the steer angle so turns stay rideable
    float lateralGrip = 1.0f;
    float maxSpeed = 24.0f;
    // air
    float spinAccel = 36.0f;        // rad/s^2
    float spinMax = 8.2f;           // rad/s (~1.3 turns/s)
    float flipAccel = 30.0f;
    float flipMax = 7.2f;
    float rollAccel = 18.0f;
    float rollMax = 4.5f;
    float airAngularDrag = 7.0f;    // when no input (lets the rider stop a rotation)
    // jump
    float popBase = 3.15f;          // m/s
    float popCrouch = 1.75f;        // extra at full crouch
    float crouchTime = 0.32f;       // time to full crouch
};

class ScooterPhysics {
public:
    void create(const Vec3& position, const Quat& rotation);
    void destroy();
    bool valid() const { return body_ != kNoBody; }
    BodyHandle body() const { return body_; }

    // inputs for this physics step
    struct Controls {
        float steer = 0.0f;       // -1..1 (left negative)
        float brake = 0.0f;       // 0..1
        bool pushRequest = false; // edge
        float spin = 0.0f;        // air yaw input -1..1
        float flip = 0.0f;        // air pitch input -1..1 (+ = front flip)
        float roll = 0.0f;        // air roll input -1..1
        bool frozenYaw = false;   // grind / manual take over
    };

    void step(float dt, const Controls& c);
    // apply a pop impulse; returns launch velocity change
    Vec3 pop(float crouch01, float timingBonus);
    void teleport(const Vec3& pos, const Quat& rot, const Vec3& velocity = Vec3(0));
    void setKinematic(bool k);
    bool kinematic() const { return kinematic_; }
    void setVelocity(const Vec3& v);
    void setAngularVelocity(const Vec3& w);
    void setOrientation(const Quat& q);
    void addVelocity(const Vec3& dv);

    // state
    Vec3 position() const { return pos_; }      // body origin (between axles, axle height)
    Quat rotation() const { return rot_; }
    Vec3 velocity() const { return vel_; }
    Vec3 angularVelocity() const { return angVel_; }
    Vec3 forward() const { return rot_ * Vec3(0, 0, -1); }
    Vec3 up() const { return rot_ * Vec3(0, 1, 0); }
    Vec3 right() const { return rot_ * Vec3(1, 0, 0); }
    float speed() const { return vel_.length(); }
    float forwardSpeed() const;  // signed along body forward
    bool grounded() const { return front_.contact || rear_.contact; }
    bool bothWheels() const { return front_.contact && rear_.contact; }
    bool fullyAirborne() const { return airTime_ > 0.05f; }
    float airTime() const { return airTime_; }
    float groundTime() const { return groundTime_; }
    Vec3 groundNormal() const { return groundNormal_; }
    int groundSurface() const { return groundSurface_; }
    const WheelContact& frontWheel() const { return front_; }
    const WheelContact& rearWheel() const { return rear_; }
    float steerAngle() const { return steerAngle_; }
    float lean() const { return lean_; }             // visual lean angle (rad, + = right)
    float pushPhase() const { return pushTimer_ > 0 ? 1.0f - pushTimer_ / tuning.pushCooldown : 0.0f; }
    float pushPlantFraction() const { return tuning.pushReach / tuning.pushCooldown; }
    float pushLiftFraction() const { return (tuning.pushReach + tuning.pushDuration) / tuning.pushCooldown; }
    bool pushing() const { return pushTimer_ > 0.0f; }
    bool fakie() const { return fakie_; }
    Vec3 lastLaunchVelocity() const { return launchVel_; }
    Vec3 localAxle(bool front) const { return Vec3(0, 0, front ? -tuning.wheelBase * 0.5f : tuning.wheelBase * 0.5f); }

    // external constraints for manuals (lift one wheel): pitch target around the rear/front axle
    void setWheelieTarget(float pitchRad, bool onRear, bool active) {
        wheelie_ = active;
        wheeliePitch_ = pitchRad;
        wheelieRear_ = onRear;
    }
    void setGravityScale(float g);
    // refresh cached position / velocity after the physics step
    void sync() {
        if (valid()) readBody();
    }
    void setAirControlScale(float s) { airControlScale_ = s; }

    ScooterTuning tuning;
    bool enableAirControl = true;

private:
    void readBody();
    void castWheel(WheelContact& w, const Vec3& axleLocal, float dt);
    void applySuspension(WheelContact& w, float dt);
    void applyGrip(WheelContact& w, const Vec3& wheelForward, float dt, float brake);
    void keepUpright(float dt, const Controls& c);
    void airControl(float dt, const Controls& c);

    BodyHandle body_ = kNoBody;
    Vec3 pos_, vel_, angVel_;
    Quat rot_;
    WheelContact front_, rear_;
    Vec3 groundNormal_{0, 1, 0};
    int groundSurface_ = 0;
    float airTime_ = 0.0f, groundTime_ = 0.0f;
    float steerAngle_ = 0.0f;
    float lean_ = 0.0f;
    float pushTimer_ = 0.0f;
    float pushActive_ = 0.0f;
    bool pushQueued_ = false;
    bool fakie_ = false;
    bool kinematic_ = false;
    bool wheelie_ = false;
    float wheeliePitch_ = 0.0f;
    bool wheelieRear_ = true;
    float airControlScale_ = 1.0f;
    Vec3 launchVel_;
    Vec3 prevVel_;
};

}  // namespace sw
