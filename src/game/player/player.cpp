#include "game/player/player.h"
#include "core/filesystem.h"
#include "core/log.h"
#include "game/player/rider_blueprint.h"

namespace sw {

const char* playerStateName(PlayerState s) {
    switch (s) {
        case PlayerState::Riding: return "Riding";
        case PlayerState::Air: return "Air";
        case PlayerState::Grinding: return "Grinding";
        case PlayerState::Manual: return "Manual";
        case PlayerState::Bailed: return "Bailed";
    }
    return "?";
}

bool Player::init() {
    if (!tricks.loadDefinitions(fs::resolve("assets/data/tricks.json"))) LOG_ERROR("player: trick definitions missing");
    ragdoll.build();
    return true;
}

void Player::applySettings(const PlayerSettings& s) {
    settings = s;
    landing.assistLevel = s.assist;
    landing.godMode = s.godMode;
    grinds.godMode = s.godMode;
    manuals.godMode = s.godMode;
}

void Player::emit(GameEventType t, float magnitude, const std::string& text, int score) {
    GameEvent e;
    e.type = t;
    e.position = position();
    e.velocity = velocity();
    e.magnitude = magnitude;
    e.text = text;
    e.score = score;
    e.surface = scooter.valid() ? scooter.groundSurface() : 0;
    events_.push_back(e);
}

void Player::spawn(const Vec3& pos, const Quat& rot) {
    ragdoll.despawn();
    // drop from slightly above so the wheels settle
    Vec3 p = pos + Vec3(0, 0.08f, 0);
    scooter.create(p, rot);
    state_ = PlayerState::Riding;
    prevPos_ = curPos_ = p;
    prevRot_ = curRot_ = rot;
    crouch_ = crouchHold_ = 0.0f;
    jumpLatch_ = false;
    tricks.cancel();
    grind = GrindState{};
    manual = ManualState{};
    safePos_ = p;
    safeRot_ = rot;
    airTimer_ = 0.0f;
    bailTimer_ = 0.0f;
    grinds.cooldown = 0.0f;
}

void Player::despawn() {
    scooter.destroy();
    ragdoll.despawn();
}

Transform Player::renderTransform(float alpha) const {
    Transform t;
    t.position = lerp(prevPos_, curPos_, alpha);
    t.rotation = nlerp(prevRot_, curRot_, alpha);
    return t;
}

void Player::setCheckpoint() {
    if (!scooter.valid() || state_ != PlayerState::Riding || !scooter.grounded()) return;
    checkpointPos_ = scooter.position();
    Vec3 f = projectOnPlane(scooter.forward(), Vec3(0, 1, 0)).normalized();
    checkpointRot_ = Quat::lookRotation(f.lengthSq() > 0.5f ? f : Vec3(0, 0, -1));
    hasCheckpoint_ = true;
    emit(GameEventType::Checkpoint);
}

void Player::respawn(bool atCheckpoint) {
    Vec3 p = safePos_;
    Quat r = safeRot_;
    if (atCheckpoint && hasCheckpoint_) {
        p = checkpointPos_;
        r = checkpointRot_;
    } else if (!atCheckpoint && safePos_.lengthSq() < 1e-6f) {
        p = spawnPos_;
        r = spawnRot_;
    }
    combo.fail();
    spawn(p, r);
    emit(GameEventType::Respawn);
}

void Player::bail(const std::string& reason) {
    if (state_ == PlayerState::Bailed) return;
    if (settings.godMode) {
        LOG_INFO("player: bail prevented (god mode): %s", reason.c_str());
        return;
    }
    LOG_INFO("player: BAIL (%s)", reason.c_str());
    bailReason_ = reason;
    ++bails_;
    Vec3 v = scooter.velocity(), w = scooter.angularVelocity();
    Vec3 pos = scooter.position();
    Quat rot = scooter.rotation();
    // rider joints in the current (approximate) pose: rest pose crouched according to the state
    std::vector<Transform> joints(RJ_Count);
    const RiderJointDef* rj = riderJoints();
    std::vector<Transform> rest(RJ_Count);
    for (int i = 0; i < RJ_Count; ++i) {
        joints[size_t(i)].position = rj[i].restWorld;
        joints[size_t(i)].position.y = joints[size_t(i)].position.y * (1.0f - 0.18f * crouch_) + 0.06f;
    }
    if (!riderPose.empty() && riderPose.size() == size_t(RJ_Count)) joints = riderPose;
    Transform model;
    model.position = pos;
    model.rotation = rot;
    Vec3 com = physics().centerOfMass(scooter.body());
    emit(GameEventType::Bail, v.length(), reason);
    scooter.destroy();
    ragdoll.spawn(model, joints, v * 0.9f, w, com);
    // the scooter flies away on its own
    ragdoll.spawnScooter(pos + rot * Vec3(0, 0.05f, 0), rot, v * 0.8f + Vec3(0, 0.6f, 0), w + rot * Vec3(0.8f, 0, 2.5f));
    state_ = PlayerState::Bailed;
    bailTimer_ = 0.0f;
    combo.fail();
    tricks.cancel();
    grind = GrindState{};
    manual = ManualState{};
    crouch_ = 0.0f;
}

void Player::enterAir(bool popped) {
    state_ = PlayerState::Air;
    airTimer_ = 0.0f;
    airStartY_ = scooter.position().y;
    airPeak_ = 0.0f;
    coyote_ = popped ? 0.0f : 0.12f;
    predictLanding();
    tricks.beginAir(timeToLand_, scooter.fakie());
}

void Player::doPop(float timingBonus) {
    Vec3 dv = scooter.pop(crouch_, timingBonus);
    emit(GameEventType::Pop, dv.length() * (0.5f + crouch_ * 0.5f));
    crouch_ = 0.0f;
    crouchHold_ = 0.0f;
    enterAir(true);
}

void Player::predictLanding() {
    // simple ballistic prediction: step the trajectory and raycast each segment
    timeToLand_ = 10.0f;
    landNormal_ = Vec3(0, 1, 0);
    Vec3 p = scooter.position() + scooter.up() * 0.05f;
    Vec3 v = scooter.velocity();
    const float step = 0.05f;
    for (int i = 0; i < 60; ++i) {
        Vec3 nv = v + Vec3(0, -9.81f, 0) * step;
        Vec3 np = p + (v + nv) * 0.5f * step;
        Vec3 d = np - p;
        float len = d.length();
        RayHit hit;
        if (len > 1e-4f && physics().raycast(p, d / len, len + 0.08f, hit, kWorldMask, scooter.body())) {
            timeToLand_ = float(i) * step + step * saturate(hit.distance / len);
            landNormal_ = hit.normal;
            return;
        }
        p = np;
        v = nv;
    }
}

void Player::finishGrind(GrindExit exit) {
    // score every grind type segment
    int total = 0;
    std::string name;
    for (auto& seg : grind.segments) {
        const GrindTypeInfo& gi = grindInfo(seg.first);
        int pts = int(float(gi.scorePerSecond) * std::max(seg.second, 0.25f));
        total += pts;
        name += (name.empty() ? "" : " to ") + std::string(gi.name);
    }
    if (exit != GrindExit::Bail && !name.empty()) {
        combo.addSegment(name, total);
        lastTrick_ = name;
        lastTrickScore_ = total;
        trickAge_ = 0.0f;
    }
    GameEvent e;
    e.type = GameEventType::GrindEnd;
    e.position = scooter.position();
    e.surface = grind.surface;
    e.text = name;
    events_.push_back(e);
}

void Player::finishManual(ManualExit exit) {
    scooter.setWheelieTarget(0.0f, true, false);
    if (exit != ManualExit::Bail && manual.time > 0.25f) {
        int pts = int((manual.nose ? 320.0f : 260.0f) * manual.time);
        combo.addSegment(manualName(manual), pts);
        lastTrick_ = manualName(manual);
        lastTrickScore_ = pts;
        trickAge_ = 0.0f;
    }
    emit(GameEventType::ManualEnd, manual.time, manualName(manual));
}

void Player::fixedUpdate(float dt, const PlayerInput& in, std::deque<FlickEvent>& flicks, double time) {
    landingAge_ += dt;
    trickAge_ += dt;
    grinds.cooldown = std::max(0.0f, grinds.cooldown - dt);
    if (in.jumpReleased) jumpLatch_ = true;

    if (state_ == PlayerState::Bailed) {
        bailTimer_ += dt;
        ragdoll.update(dt);
        if (ragdoll.active()) {
            curPos_ = prevPos_ = ragdoll.centre();
        }
        bool settled = ragdoll.settledTime() > 1.2f;
        if (bailTimer_ > 3.5f || (bailTimer_ > 1.8f && settled) || (in.respawnPressed && bailTimer_ > 0.4f) ||
            (in.jumpPressed && bailTimer_ > 1.0f))
            respawn(false);
        jumpLatch_ = false;
        return;
    }
    if (!scooter.valid()) return;
    prevPos_ = curPos_;
    prevRot_ = curRot_;

    if (in.respawnPressed) {
        respawn(hasCheckpoint_);
        return;
    }
    if (in.checkpointPressed) setCheckpoint();

    // crouch charge (hold jump). Charge is capped and slowly decays if held too long.
    bool canCrouch = state_ == PlayerState::Riding || state_ == PlayerState::Manual || state_ == PlayerState::Grinding;
    if (in.jumpDown && canCrouch) {
        crouchHold_ += dt;
        float charge = saturate(crouchHold_ / scooter.tuning.crouchTime);
        if (crouchHold_ > 1.4f) charge = std::max(0.7f, 1.0f - (crouchHold_ - 1.4f) * 0.25f);
        crouch_ = charge;
    } else if (!in.jumpDown) {
        crouch_ = std::max(0.0f, crouch_ - dt * 4.0f);
        if (state_ != PlayerState::Air) crouchHold_ = 0.0f;
    }

    ScooterPhysics::Controls c;
    c.steer = in.move.x;
    c.brake = in.brake;

    switch (state_) {
        case PlayerState::Riding: {
            c.pushRequest = in.pushPressed && crouch_ < 0.3f;
            if (c.pushRequest) emit(GameEventType::Push);
            // pop
            if (jumpLatch_ && scooter.grounded()) {
                jumpLatch_ = false;
                // timing bonus: popping on the upward part of a ramp or right at a lip
                float slope = std::acos(clampf(scooter.groundNormal().y, -1.0f, 1.0f)) * kRad2Deg;
                bool goingUp = dot(scooter.velocity(), Vec3(0, 1, 0)) > 0.3f;
                float timing = goingUp ? saturate((slope - 5.0f) / 25.0f) : 0.0f;
                doPop(timing);
                scooter.step(dt, c);
                break;
            }
            jumpLatch_ = false;
            // revert: 180 pivot on the ground (switches between regular and fakie)
            revertTimer_ -= dt;
            if (in.revertPressed && scooter.grounded() && scooter.speed() > 1.0f && revertTimer_ <= 0.0f) {
                Quat r = Quat::angleAxis(kPi, scooter.groundNormal()) * scooter.rotation();
                scooter.setOrientation(r);
                revertTimer_ = 0.5f;
                if (combo.active()) combo.addTrick("Revert", 150, "Revert");
                emit(GameEventType::Revert);
            }
            // manual
            bool landingNow = justLanded_ && landedTimer_ < 0.25f;
            if (manuals.tryEnter(dt, in.move.y, scooter.speed(), scooter.bothWheels() || landingNow, landingNow, manual)) {
                state_ = PlayerState::Manual;
                emit(GameEventType::ManualStart, 0, manualName(manual));
                if (!combo.active()) combo.keepAlive(1.1f);
            }
            scooter.step(dt, c);
            break;
        }
        case PlayerState::Manual: {
            bool jump = jumpLatch_;
            jumpLatch_ = false;
            ManualExit ex = manuals.update(dt, in.move.y, jump, scooter.grounded(), scooter.speed(), manual, settings.balanceScale);
            if (ex == ManualExit::None) {
                scooter.setWheelieTarget(manuals.pitchTarget(manual), !manual.nose, true);
                c.steer *= 0.6f;
                combo.keepAlive(1.1f);
                scooter.step(dt, c);
                break;
            }
            finishManual(ex);
            if (ex == ManualExit::Bail) {
                bail("looped out of the manual");
                return;
            }
            state_ = PlayerState::Riding;
            if (ex == ManualExit::Jump) {
                doPop(0.0f);
            } else if (ex == ManualExit::Airborne) {
                enterAir(false);
            }
            scooter.step(dt, c);
            break;
        }
        case PlayerState::Air: {
            airTimer_ += dt;
            coyote_ = std::max(0.0f, coyote_ - dt);
            airPeak_ = std::max(airPeak_, scooter.position().y - airStartY_);
            // late pop right after rolling off a lip (coyote time)
            if (jumpLatch_ && coyote_ > 0.0f) {
                jumpLatch_ = false;
                scooter.pop(crouch_, 1.0f);
                emit(GameEventType::Pop, 1.0f);
                crouch_ = 0.0f;
                coyote_ = 0.0f;
            }
            jumpLatch_ = false;
            float spinBtn = (in.spinRight ? 1.0f : 0.0f) - (in.spinLeft ? 1.0f : 0.0f);
            c.spin = clampf(in.move.x + spinBtn, -1.0f, 1.0f);
            c.flip = in.move.y;
            c.roll = in.move.x * std::fabs(in.move.y) * 0.8f;
            if (std::fabs(in.move.y) < 0.35f) c.flip = 0.0f;
            predictLanding();
            tricks.airUpdate(dt, flicks, time, in.grab, in.rightDir, scooter.angularVelocity(), scooter.right(), scooter.forward(), timeToLand_);
            if (tricks.startedThisStep) emit(GameEventType::TrickStart, 0, tricks.lastStarted);
            // landing assist (tilt only, never spins / flips for the player)
            if (timeToLand_ < 0.3f) {
                Quat q = landing.assist(scooter.rotation(), landNormal_, timeToLand_, dt);
                if (q.x != scooter.rotation().x || q.w != scooter.rotation().w) scooter.setOrientation(q);
            }
            // grinds: only when the scooter tricks are finished (feet back on the deck)
            if (tricks.completion() >= 0.999f && grinds.tryAttach(scooter, in.move, true, grind)) {
                TrickResult tr;
                bool complete = tricks.land(tr, scooter.fakie());
                if (complete && !tr.name.empty()) {
                    combo.addTrick(tr.name, tr.score, tr.name);
                    lastTrick_ = tr.name;
                    lastTrickScore_ = tr.score;
                    trickAge_ = 0.0f;
                    ++tricksLanded_;
                }
                scooter.setKinematic(true);
                state_ = PlayerState::Grinding;
                GameEvent e;
                e.type = GameEventType::GrindStart;
                e.position = scooter.position();
                e.surface = grind.surface;
                e.text = grindInfo(grind.type).name;
                e.magnitude = grind.speed;
                events_.push_back(e);
                combo.keepAlive(1.2f);
                break;
            }
            scooter.step(dt, c);
            break;
        }
        case PlayerState::Grinding: {
            bool jump = jumpLatch_;
            jumpLatch_ = false;
            GrindExit ex = grinds.update(dt, in.move, jump, grind, scooter, settings.balanceScale);
            combo.keepAlive(1.2f);
            if (ex == GrindExit::None) break;
            finishGrind(ex);
            if (ex == GrindExit::Bail) {
                bail("lost balance on the rail");
                return;
            }
            if (ex == GrindExit::Jump) emit(GameEventType::Pop, 1.0f);
            enterAir(ex == GrindExit::Jump);
            break;
        }
        case PlayerState::Bailed: break;
    }
    if (scooter.valid()) {
        curPos_ = scooter.position();
        curRot_ = scooter.rotation();
    }
    if (scooter.pushing() && !wasPushing_) {
        // push event already emitted on request
    }
    wasPushing_ = scooter.pushing();
    if (justLanded_) landedTimer_ += dt;
    if (landedTimer_ > 0.3f) justLanded_ = false;
}

void Player::handleLanding() {
    TrickResult tr;
    bool complete = tricks.land(tr, scooter.fakie());
    LandingInfo li = landing.evaluate(scooter.groundNormal(), scooter.rotation(), scooter.velocity(), complete, tricks.completion());
    lastLanding_ = li;
    landingAge_ = 0.0f;
    if (li.result == LandingResult::Bail) {
        bail(li.reason);
        return;
    }
    // correct the orientation to the ground: clean landings snap fully, sketchy ones keep some error
    float strength = li.result == LandingResult::Clean ? 1.0f : li.result == LandingResult::Normal ? 0.85f : 0.55f;
    Vec3 n = scooter.groundNormal();
    Vec3 v = scooter.velocity();
    Vec3 vp = projectOnPlane(v, n);
    Vec3 fwd = projectOnPlane(scooter.forward(), n).normalized();
    Vec3 targetFwd = fwd;
    if (vp.length() > 1.0f) targetFwd = (li.fakie ? -vp : vp).normalized();
    Quat target = Quat::lookRotation(targetFwd, n);
    Quat q = slerp(scooter.rotation(), target, strength);
    scooter.setOrientation(q);
    // the legs absorb the impact: remove the velocity into the ground
    float into = dot(v, n);
    if (into < 0.0f) v -= n * into;
    if (li.result == LandingResult::Sketchy) v *= 0.82f;
    // keep the travel direction aligned with the scooter after a clean landing
    if (li.result != LandingResult::Sketchy && vp.length() > 1.0f) {
        float sp = vp.length();
        Vec3 dir = (li.fakie ? -targetFwd : targetFwd);
        v = dir * sp;
    }
    scooter.setVelocity(v);
    scooter.setAngularVelocity(Vec3(0));
    state_ = PlayerState::Riding;
    justLanded_ = true;
    landedTimer_ = 0.0f;
    GameEvent e;
    e.type = GameEventType::Land;
    e.position = scooter.position();
    e.magnitude = li.impact;
    e.landing = int(li.result);
    e.surface = scooter.groundSurface();
    e.text = landingResultName(li.result);
    events_.push_back(e);

    if (!tr.name.empty()) {
        float quality = li.result == LandingResult::Clean ? 1.0f : li.result == LandingResult::Normal ? 0.9f : 0.6f;
        int pts = int(float(tr.score) * quality);
        if (airTimer_ > 1.0f) pts += int((airTimer_ - 1.0f) * 150.0f);
        combo.addTrick(tr.name, pts, tr.name);
        lastTrick_ = tr.name;
        lastTrickScore_ = pts;
        trickAge_ = 0.0f;
        ++tricksLanded_;
        GameEvent te;
        te.type = GameEventType::TrickLanded;
        te.text = tr.name;
        te.score = pts;
        te.landing = int(li.result);
        te.position = scooter.position();
        events_.push_back(te);
    } else if (airTimer_ > 1.1f) {
        int pts = int(airTimer_ * 120.0f);
        combo.addTrick("Big Air", pts, "Big Air");
        lastTrick_ = "Big Air";
        lastTrickScore_ = pts;
        trickAge_ = 0.0f;
    } else if (combo.active()) {
        combo.keepAlive(1.1f);
    }
}

void Player::postPhysics(float dt) {
    auto contacts = physics().takeContacts();
    if (state_ == PlayerState::Bailed || !scooter.valid()) return;
    scooter.sync();
    curPos_ = scooter.position();
    curRot_ = scooter.rotation();
    // collisions of the rider's body: head / body impacts and walls
    for (const ContactEvent& ce : contacts) {
        if (ce.self != scooter.body()) continue;
        if (state_ == PlayerState::Grinding) continue;
        Vec3 local = scooter.rotation().conjugate() * (ce.point - scooter.position());
        bool riderShape = ce.selfSubShapeUser == 2;
        bool head = riderShape && local.y > 1.35f;
        bool wall = std::fabs(ce.normal.y) < 0.45f;
        if (riderShape && ce.approachSpeed > (head ? 2.2f : 3.2f)) {
            bail(head ? "head impact" : (wall ? "wall collision" : "body impact"));
            return;
        }
        if (!riderShape && wall && ce.approachSpeed > 5.5f && ce.added) {
            bail("wall collision");
            return;
        }
        if (ce.added && ce.approachSpeed > 3.0f) emit(GameEventType::Impact, ce.approachSpeed);
    }
    switch (state_) {
        case PlayerState::Air:
            if (scooter.grounded() && airTimer_ > 0.06f) {
                if (airTimer_ < 0.18f && !tricks.startedThisStep && tricks.active().empty() && std::fabs(tricks.spinDegrees()) < 60.0f &&
                    std::fabs(tricks.flipDegrees()) < 60.0f) {
                    // tiny hop: no landing evaluation
                    tricks.cancel();
                    state_ = PlayerState::Riding;
                    if (combo.active()) combo.keepAlive(1.1f);
                } else {
                    handleLanding();
                }
            }
            break;
        case PlayerState::Riding:
            if (scooter.fullyAirborne()) enterAir(false);
            break;
        case PlayerState::Manual:
            break;
        default: break;
    }
    // remember a safe respawn position while riding normally
    if (state_ == PlayerState::Riding && scooter.bothWheels() && scooter.up().y > 0.85f && scooter.speed() < 12.0f) {
        safeTimer_ += dt;
        if (safeTimer_ > 0.6f) {
            safeTimer_ = 0.0f;
            safePos_ = scooter.position();
            Vec3 f = projectOnPlane(scooter.forward(), Vec3(0, 1, 0)).normalized();
            safeRot_ = Quat::lookRotation(f.lengthSq() > 0.5f ? f : Vec3(0, 0, -1));
        }
    }
    // fell out of the world
    if (scooter.position().y < -30.0f) respawn(false);
    combo.update(dt, state_ == PlayerState::Riding && scooter.grounded() && landedTimer_ > 0.05f);
}

}  // namespace sw
