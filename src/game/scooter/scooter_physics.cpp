#include "game/scooter/scooter_physics.h"
#include "core/log.h"

namespace sw {

namespace {
constexpr uint64_t kShapeDeck = 1;
constexpr uint64_t kShapeRider = 2;
}  // namespace

void ScooterPhysics::create(const Vec3& position, const Quat& rotation) {
    destroy();
    BodyDesc d;
    ShapeDesc deck;
    deck.kind = ShapeKind::Box;
    deck.halfExtents = Vec3(0.07f, 0.028f, 0.25f);
    deck.position = Vec3(0, 0.03f, 0);
    deck.userData = kShapeDeck;
    d.shapes.push_back(deck);
    ShapeDesc rider;
    rider.kind = ShapeKind::Capsule;
    rider.radius = 0.19f;
    rider.halfHeight = 0.42f;
    rider.position = Vec3(0, 1.1f, 0.02f);
    rider.userData = kShapeRider;
    d.shapes.push_back(rider);
    d.position = position;
    d.rotation = rotation;
    d.motion = MotionKind::Dynamic;
    d.layer = PhysLayer::Player;
    d.mass = tuning.mass;
    d.overrideCom = true;
    d.centerOfMass = Vec3(0, tuning.comHeight, 0);
    d.friction = 0.25f;
    d.restitution = 0.0f;
    d.linearDamping = 0.0f;
    d.angularDamping = 0.0f;
    d.continuous = true;
    d.allowSleep = false;
    body_ = physics().createBody(d);
    physics().watchContacts(body_, true);
    pos_ = position;
    rot_ = rotation;
    vel_ = angVel_ = Vec3(0);
    front_ = rear_ = WheelContact{};
    airTime_ = 0.0f;
    groundTime_ = 0.0f;
    kinematic_ = false;
    wheelie_ = false;
    fakie_ = false;
    pushTimer_ = pushActive_ = 0.0f;
    pushQueued_ = false;
}

void ScooterPhysics::destroy() {
    if (body_ != kNoBody) {
        physics().watchContacts(body_, false);
        physics().removeBody(body_);
    }
    body_ = kNoBody;
}

void ScooterPhysics::readBody() {
    pos_ = physics().position(body_);
    rot_ = physics().rotation(body_).normalized();
    vel_ = physics().linearVelocity(body_);
    angVel_ = physics().angularVelocity(body_);
}

float ScooterPhysics::forwardSpeed() const { return dot(vel_, forward()); }

void ScooterPhysics::teleport(const Vec3& pos, const Quat& rot, const Vec3& velocity) {
    if (!valid()) return;
    physics().setTransform(body_, pos, rot, true);
    physics().setLinearVelocity(body_, velocity);
    physics().setAngularVelocity(body_, Vec3(0));
    pos_ = pos;
    rot_ = rot;
    vel_ = velocity;
    angVel_ = Vec3(0);
    front_ = rear_ = WheelContact{};
    airTime_ = 0.0f;
    fakie_ = false;
    steerAngle_ = 0.0f;
    lean_ = 0.0f;
}

void ScooterPhysics::setKinematic(bool k) {
    kinematic_ = k;
    setGravityScale(k ? 0.0f : 1.0f);
}

void ScooterPhysics::setGravityScale(float g) {
    if (valid()) physics().setGravityFactor(body_, g);
}

void ScooterPhysics::setVelocity(const Vec3& v) {
    vel_ = v;
    physics().setLinearVelocity(body_, v);
}

void ScooterPhysics::addVelocity(const Vec3& dv) { setVelocity(physics().linearVelocity(body_) + dv); }

void ScooterPhysics::setAngularVelocity(const Vec3& w) {
    angVel_ = w;
    physics().setAngularVelocity(body_, w);
}

void ScooterPhysics::setOrientation(const Quat& q) {
    rot_ = q.normalized();
    physics().setTransform(body_, physics().position(body_), rot_, true);
}

void ScooterPhysics::castWheel(WheelContact& w, const Vec3& axleLocal, float dt) {
    Vec3 upW = up();
    Vec3 axle = pos_ + rot_ * axleLocal;
    w.axleWorld = axle;
    float prevComp = w.compression;
    bool prevContact = w.contact;
    RayHit hit;
    Vec3 start = axle + upW * tuning.castLift;
    float maxDist = tuning.castLift + tuning.snapRange;
    bool ok = physics().sphereCast(start, tuning.wheelRadius * 0.92f, -upW, maxDist, hit, kWorldMask, body_);
    // a hit at the very start means the cast began inside geometry: ignore it
    if (!ok || hit.fraction <= 1e-4f || dot(hit.normal, upW) < 0.35f) {
        w.contact = false;
        w.near = false;
        w.compression = -tuning.snapRange;
        w.compressionVel = 0.0f;
        w.load = 0.0f;
        return;
    }
    float comp = tuning.castLift - hit.distance + (tuning.wheelRadius - tuning.wheelRadius * 0.92f);
    w.compression = comp;
    w.compressionVel = prevContact ? (comp - prevComp) / dt : 0.0f;
    w.contact = comp > -0.012f;
    w.near = true;
    w.point = hit.point;
    w.normal = hit.normal;
    w.surface = hit.surface;
    w.entity = hit.entity;
    w.groundVelocity = Vec3(0);
}

void ScooterPhysics::applySuspension(WheelContact& w, float dt) {
    if (!w.contact) {
        w.load = 0.0f;
        return;
    }
    float comp = std::min(w.compression, tuning.maxCompression * 1.5f);
    float f = tuning.springK * std::max(comp, 0.0f) + tuning.springDamping * w.compressionVel;
    // when barely touching the damper must not pull the body down
    f = std::max(f, 0.0f);
    f = std::min(f, tuning.mass * 9.81f * 12.0f);
    w.load = f;
    physics().addImpulseAt(body_, w.normal * (f * dt), w.axleWorld);
    // hard stop: bottomed out, remove the velocity into the ground at the axle
    if (w.compression > tuning.maxCompression) {
        Vec3 pv = physics().pointVelocity(body_, w.axleWorld);
        float into = dot(pv, w.normal);
        if (into < 0.0f) {
            float m = physics().effectiveMass(body_, w.axleWorld, w.normal);
            physics().addImpulseAt(body_, w.normal * (-into * m * 0.8f), w.axleWorld);
        }
    }
}

void ScooterPhysics::applyGrip(WheelContact& w, const Vec3& wheelForward, float dt, float) {
    if (!w.contact) return;
    Vec3 n = w.normal;
    Vec3 fwd = projectOnPlane(wheelForward, n).normalized();
    if (fwd.lengthSq() < 0.5f) return;
    Vec3 side = cross(n, fwd).normalized();
    Vec3 p = w.point + n * tuning.wheelRadius;
    Vec3 v = physics().pointVelocity(body_, p) - w.groundVelocity;
    float vLat = dot(v, side);
    float mEff = physics().effectiveMass(body_, p, side);
    const SurfaceType& st = surfaces().get(w.surface);
    float maxImp = st.friction * tuning.lateralGrip * (w.load + tuning.mass * 2.0f) * dt;
    float j = clampf(-vLat * mEff, -maxImp, maxImp);
    physics().addImpulseAt(body_, side * j, p);
}

void ScooterPhysics::keepUpright(float dt, const Controls& c) {
    Vec3 desiredUp = groundNormal_;
    if (wheelie_) desiredUp = Quat::angleAxis(wheelieRear_ ? wheeliePitch_ : -wheeliePitch_, right()) * groundNormal_;
    Vec3 curUp = up();
    Vec3 axis = cross(curUp, desiredUp);
    float s = axis.length();
    float ang = std::atan2(s, dot(curUp, desiredUp));
    Vec3 wAlign = s > 1e-5f ? axis / s * (ang * 16.0f) : Vec3(0);
    Vec3 w = physics().angularVelocity(body_);
    float yawRate = dot(w, desiredUp);
    // in place turning at very low speed (a rider can lift and swing the scooter)
    float spd = speed();
    if (spd < 1.5f && std::fabs(c.steer) > 0.1f) {
        float t = 1.0f - spd / 1.5f;
        yawRate = lerpf(yawRate, -c.steer * 1.6f, t * damp(6.0f, dt));
    }
    Vec3 target = desiredUp * yawRate + wAlign;
    w = lerp(w, target, damp(30.0f, dt));
    physics().setAngularVelocity(body_, w);
}

void ScooterPhysics::airControl(float dt, const Controls& c) {
    Vec3 w = physics().angularVelocity(body_);
    float scale = airControlScale_;
    auto channel = [&](const Vec3& axis, float input, float target, float accel) {
        float cur = dot(w, axis);
        float next;
        if (std::fabs(input) > 0.12f && enableAirControl) {
            next = moveTowards(cur, target, accel * scale * dt);
        } else {
            next = cur * std::exp(-tuning.airAngularDrag * dt);
        }
        w += axis * (next - cur);
    };
    channel(Vec3(0, 1, 0), c.spin, -c.spin * tuning.spinMax, tuning.spinAccel);
    if (std::fabs(c.flip) < 0.12f) {
        // no flip input: the rider lets the nose follow the trajectory (like pulling the bars
        // through the arc of a jump). Only for forward travel with real horizontal speed, so
        // vert airs (straight up / down) and fakie airs are not rotated.
        Vec3 v = vel_;
        Vec3 R = right();
        Vec3 f = forward();
        Vec3 vp = projectOnPlane(v, R);
        float hs = Vec2(v.x, v.z).length();
        // moving forwards (not fakie) judged on the horizontal heading, rider right side up
        Vec3 fh(f.x, 0, f.z);
        bool forwardTravel = fh.length() > 0.15f && dot(fh.normalized(), Vec3(v.x, 0, v.z).normalized()) > 0.3f && up().y > 0.0f;
        if (hs > 2.0f && vp.length() > 1.0f && forwardTravel) {
            // desired nose pitch: half of the trajectory angle, limited so landings stay rideable
            Vec3 hdir = Vec3(v.x, 0, v.z).normalized();
            float traj = std::atan2(v.y, hs);
            float pitch = clampf(traj * 0.5f, -35.0f * kDeg2Rad, 35.0f * kDeg2Rad);
            Vec3 desired = (hdir * std::cos(pitch) + Vec3(0, std::sin(pitch), 0)).normalized();
            Vec3 dp = projectOnPlane(desired, R);
            float ang = dp.lengthSq() > 1e-4f ? signedAngle(f, dp.normalized(), R) : 0.0f;
            float target = clampf(ang * 2.2f, -2.5f, 2.5f);
            float cur = dot(w, R);
            float next = target + (cur - target) * std::exp(-tuning.airAngularDrag * dt);
            w += R * (next - cur);
        } else {
            channel(R, 0.0f, 0.0f, tuning.flipAccel);
        }
    } else {
        channel(right(), c.flip, -c.flip * tuning.flipMax, tuning.flipAccel);
    }
    channel(forward(), c.roll, c.roll * tuning.rollMax, tuning.rollAccel);
    physics().setAngularVelocity(body_, w);
}

Vec3 ScooterPhysics::pop(float crouch01, float timingBonus) {
    readBody();
    // pop perpendicular to the riding surface (ramps, banks, transitions) blended with world up
    Vec3 n = groundNormal_;
    Vec3 dir = (n * 0.82f + Vec3(0, 1, 0) * 0.18f).normalized();
    float slope = std::acos(clampf(n.y, -1.0f, 1.0f));
    float surf = surfaces().get(groundSurface_).popFactor;
    float v = (tuning.popBase + tuning.popCrouch * saturate(crouch01)) * surf * (1.0f + timingBonus * 0.12f) + 0.03f * speed();
    v *= lerpf(1.0f, 0.72f, saturate(slope / (60.0f * kDeg2Rad)));
    Vec3 popVec = dir * v;
    Vec3 vel = physics().linearVelocity(body_);
    // a rider never pushes himself backwards off a steep ramp: keep the momentum of travel
    Vec3 hv(vel.x, 0, vel.z);
    if (hv.length() > 0.5f) {
        Vec3 h = hv.normalized();
        float back = dot(popVec, h);
        if (back < 0.0f) popVec -= h * (back * 0.85f);
    }
    float vn = dot(vel, dir);
    if (vn < 0.0f) vel -= dir * vn;
    vel += popVec;
    physics().setLinearVelocity(body_, vel);
    vel_ = vel;
    launchVel_ = vel;
    // the wheels leave the ground this step
    front_.contact = rear_.contact = false;
    airTime_ = 0.001f;
    return popVec;
}

void ScooterPhysics::step(float dt, const Controls& c) {
    if (!valid()) return;
    readBody();
    if (kinematic_) {
        airTime_ = 0.0f;
        groundTime_ += dt;
        return;
    }
    prevVel_ = vel_;
    castWheel(front_, localAxle(true), dt);
    castWheel(rear_, localAxle(false), dt);
    // right after a pop ignore the ground so the wheels can leave it
    if (airTime_ > 0.0f && airTime_ < 0.08f && dot(vel_, groundNormal_) > 0.5f) {
        front_.contact = rear_.contact = false;
    }
    bool g = grounded();
    if (g) {
        groundTime_ += dt;
        if (airTime_ > 0.0f) airTime_ = 0.0f;
        Vec3 n(0);
        float wsum = 0;
        for (const WheelContact* w : {&front_, &rear_})
            if (w->contact) {
                n += w->normal;
                wsum += 1.0f;
            }
        groundNormal_ = (n / wsum).normalized();
        groundSurface_ = rear_.contact ? rear_.surface : front_.surface;
    } else {
        airTime_ += dt;
        groundTime_ = 0.0f;
    }

    // steering (speed sensitive, limited lateral acceleration)
    float fs = forwardSpeed();
    if (fakie_ && fs > 0.6f) fakie_ = false;
    if (!fakie_ && fs < -0.6f && g) fakie_ = true;
    float sa = std::fabs(fs);
    float maxSteer = lerpf(tuning.maxSteerLow, tuning.maxSteerHigh, saturate(sa / 13.0f));
    maxSteer = std::min(maxSteer, std::atan(tuning.wheelBase * tuning.maxLateralAccel / std::max(sa * sa, 0.01f)));
    float target = c.steer * maxSteer * (fakie_ ? -1.0f : 1.0f);
    steerAngle_ = moveTowards(steerAngle_, target, 3.5f * dt);

    if (g) {
        applySuspension(front_, dt);
        applySuspension(rear_, dt);
        readBody();
        Vec3 f = forward();
        Vec3 frontDir = Quat::angleAxis(-steerAngle_, up()) * f;
        for (int it = 0; it < 2; ++it) {
            applyGrip(front_, frontDir, dt, 0.0f);
            applyGrip(rear_, f, dt, c.brake);
        }
        readBody();
        // rolling resistance, air drag and brake along the ground plane
        Vec3 vPlane = projectOnPlane(vel_, groundNormal_);
        float s = vPlane.length();
        if (s > 1e-3f) {
            const SurfaceType& st = surfaces().get(groundSurface_);
            float decel = st.rollingResistance * 9.81f + tuning.airDrag * s * s + tuning.brakeDecel * c.brake;
            float dv = std::min(s, decel * dt);
            physics().addImpulse(body_, vPlane * (-dv / s * tuning.mass));
        }
        // pushing
        if (c.pushRequest) pushQueued_ = true;
        pushTimer_ = std::max(0.0f, pushTimer_ - dt);
        if (pushQueued_ && pushTimer_ <= 0.0f && bothWheels() && !wheelie_) {
            pushTimer_ = tuning.pushCooldown;
            pushQueued_ = false;
        }
        // the foot is on the ground between pushReach and pushReach + pushDuration of the cycle
        float pushT = tuning.pushCooldown - pushTimer_;
        pushActive_ = pushTimer_ > 0.0f && pushT >= tuning.pushReach && pushT < tuning.pushReach + tuning.pushDuration ? 1.0f : 0.0f;
        if (pushActive_ > 0.0f) {
            float gain = std::pow(saturate(1.0f - s / tuning.maxPushSpeed), 0.6f) + 0.04f;
            float dv = tuning.pushImpulse * gain * dt / tuning.pushDuration;
            Vec3 dir = projectOnPlane(f * (fakie_ ? -1.0f : 1.0f), groundNormal_).normalized();
            physics().addImpulse(body_, dir * (dv * tuning.mass));
        }
        keepUpright(dt, c);
        // visual carve lean from the lateral acceleration (v^2 / r)
        float yawRate = dot(physics().angularVelocity(body_), groundNormal_);
        float latAcc = yawRate * s * (fakie_ ? 1.0f : -1.0f);
        float targetLean = clampf(std::atan2(latAcc, 9.81f), -0.6f, 0.6f);
        lean_ = dampf(lean_, targetLean, 8.0f, dt);
    } else {
        pushActive_ = 0.0f;
        pushTimer_ = std::max(0.0f, pushTimer_ - dt);
        if (c.pushRequest) pushQueued_ = false;
        airControl(dt, c);
        lean_ = dampf(lean_, 0.0f, 3.0f, dt);
        Vec3 v = physics().linearVelocity(body_);
        float s = v.length();
        if (s > 1e-3f) physics().addImpulse(body_, v * (-std::min(s, tuning.airDrag * s * s * dt) / s * tuning.mass));
    }
    // clamp extreme speeds
    readBody();
    if (vel_.length() > tuning.maxSpeed) physics().setLinearVelocity(body_, vel_.normalized() * tuning.maxSpeed);
}

}  // namespace sw
