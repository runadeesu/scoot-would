#include "game/camera_controller.h"
#include "physics/physics_world.h"

namespace sw {

static float headingOf(const Vec3& d) { return std::atan2(-d.x, -d.z); }  // yaw where 0 = -Z

static Vec3 dirFromYawPitch(float yaw, float pitch) {
    // forward direction the camera looks along
    return Vec3(-std::sin(yaw) * std::cos(pitch), -std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
}

void CameraController::reset(const CameraTarget& t) {
    yaw_ = headingOf(t.forward);
    pitch_ = 0.2f;
    orbitYaw_ = orbitPitch_ = 0.0f;
    smoothTarget_ = t.position;
    Vec3 dir = dirFromYawPitch(yaw_, pitch_);
    pos_ = t.position - dir * dist_ + Vec3(0, 0.4f, 0);
    lookAt_ = t.position;
    fov_ = baseFov;
    shake_ = 0.0f;
}

void CameraController::cycleMode() {
    int m = (int(mode_) + 1) % int(CameraMode::Free);
    mode_ = CameraMode(m);
}

void CameraController::addShake(float amount) {
    if (shakeSetting == 0) return;
    float scale = shakeSetting == 1 ? 0.45f : 1.0f;
    shake_ = std::min(1.0f, shake_ + amount * scale);
}

void CameraController::update(float dt, const CameraTarget& t, const Vec2& look, bool lookActive, bool resetPressed) {
    if (mode_ == CameraMode::Free) return;
    float distBase = mode_ == CameraMode::Close ? 2.5f : mode_ == CameraMode::Far ? 4.8f : 3.4f;
    float heightBase = mode_ == CameraMode::Close ? 0.35f : mode_ == CameraMode::Far ? 0.9f : 0.55f;

    // heading follows the direction of travel (velocity) so fakie riding keeps the camera behind
    Vec3 hv(t.velocity.x, 0, t.velocity.z);
    float hs = hv.length();
    float desiredYaw = yaw_;
    if (t.grounded || t.grinding) {
        if (hs > 1.2f)
            desiredYaw = headingOf(hv);
        else if (!t.fakie)
            desiredYaw = headingOf(t.forward);
        float follow = t.grinding ? 5.0f : lerpf(2.5f, 6.0f, saturate(hs / 10.0f));
        yaw_ += angleDelta(yaw_, desiredYaw) * damp(follow, dt);
    } else if (hs > 3.0f) {
        // in the air follow the horizontal velocity slowly, never the flips / spins
        yaw_ += angleDelta(yaw_, headingOf(hv)) * damp(1.2f, dt);
    }

    // manual orbit with the right stick, springs back after release
    if (lookActive && look.lengthSq() > 0.01f) {
        orbitYaw_ -= look.x * 2.6f * sensitivity * dt;
        orbitPitch_ += look.y * 1.6f * sensitivity * dt * (invertY ? -1.0f : 1.0f);
        orbitPitch_ = clampf(orbitPitch_, -0.35f, 0.9f);
        orbitTimer_ = 1.4f;
    } else {
        orbitTimer_ -= dt;
        if (orbitTimer_ <= 0.0f) {
            orbitYaw_ = dampf(orbitYaw_, 0.0f, 2.5f, dt);
            orbitPitch_ = dampf(orbitPitch_, 0.0f, 2.5f, dt);
        }
    }
    if (resetPressed) {
        orbitYaw_ = orbitPitch_ = 0.0f;
        yaw_ = headingOf(t.forward);
    }

    // target smoothing (lag) and look ahead
    float lag = t.grounded ? lerpf(10.0f, 16.0f, saturate(hs / 12.0f)) : 7.0f;
    smoothTarget_ = dampv(smoothTarget_, t.position, lag, dt);
    // never let the smoothed target lag too far behind at high speed
    Vec3 off = smoothTarget_ - t.position;
    if (off.length() > 0.9f) smoothTarget_ = t.position + off.normalized() * 0.9f;
    Vec3 ahead = hv * 0.06f;
    ahead.y = 0;

    float airLift = t.grounded ? 0.0f : 0.35f;
    airPitch_ = dampf(airPitch_, t.grounded ? 0.0f : 0.12f, 3.0f, dt);
    float pitch = pitch_ + orbitPitch_ + airPitch_;
    float yaw = yaw_ + orbitYaw_;
    float desiredDist = distBase + saturate(hs / 18.0f) * 0.8f + (t.grounded ? 0.0f : 0.6f);
    dist_ = dampf(dist_, desiredDist, 3.0f, dt);

    Vec3 focus = smoothTarget_ + ahead + Vec3(0, heightBase * 0.4f + airLift * 0.5f, 0);
    Vec3 dir = dirFromYawPitch(yaw, pitch);
    Vec3 desired = focus - dir * dist_ + Vec3(0, heightBase, 0);

    // collision: keep the camera in front of walls between the rider and the camera
    RayHit hit;
    Vec3 toCam = desired - focus;
    float len = toCam.length();
    if (len > 0.01f && physics().sphereCast(focus, 0.22f, toCam / len, len, hit, layerBit(PhysLayer::Static) | layerBit(PhysLayer::Dynamic),
                                            t.ignoreBody)) {
        desired = focus + toCam / len * std::max(0.35f, hit.distance - 0.05f);
    }
    // never go below the ground under the camera
    if (physics().raycast(desired + Vec3(0, 1.0f, 0), Vec3(0, -1, 0), 1.3f, hit, layerBit(PhysLayer::Static), t.ignoreBody))
        desired.y = std::max(desired.y, hit.point.y + 0.25f);

    pos_ = dampv(pos_, desired, 14.0f, dt);
    lookAt_ = dampv(lookAt_, focus, 20.0f, dt);

    // speed based field of view
    float targetFov = baseFov + saturate((t.speed - 4.0f) / 12.0f) * 12.0f + (t.grounded ? 0.0f : 3.0f);
    fov_ = dampf(fov_, targetFov, 3.0f, dt);

    shakeTime_ += dt;
    shake_ = std::max(0.0f, shake_ - dt * 2.2f);
}

void CameraController::updateFree(float dt, const Vec3& move, const Vec2& mouse, float speed) {
    freeYaw_ -= mouse.x * 0.003f;
    freePitch_ = clampf(freePitch_ - mouse.y * 0.003f, -1.5f, 1.5f);
    Quat q = Quat::angleAxis(freeYaw_, Vec3(0, 1, 0)) * Quat::angleAxis(freePitch_, Vec3(1, 0, 0));
    Vec3 f = q * Vec3(0, 0, -1), r = q * Vec3(1, 0, 0);
    pos_ += (f * move.z + r * move.x + Vec3(0, move.y, 0)) * speed * dt;
    lookAt_ = pos_ + f;
    fov_ = baseFov;
}

Quat CameraController::rotation() const { return Quat::lookRotation((lookAt_ - pos_).normalized(), Vec3(0, 1, 0)); }

RenderView CameraController::view(float aspect) const {
    Vec3 p = pos_, target = lookAt_;
    if (shake_ > 0.001f) {
        float s = shake_ * shake_;
        float t = shakeTime_ * 31.0f;
        Vec3 n(std::sin(t * 1.3f) + std::sin(t * 2.9f) * 0.5f, std::sin(t * 1.7f + 1.0f) + std::sin(t * 3.3f) * 0.5f, std::sin(t * 1.1f + 2.0f));
        p += n * (0.06f * s);
        target += n * (0.03f * s);
    }
    return RenderView::lookAt(p, target, Vec3(0, 1, 0), fov_ * kDeg2Rad, aspect, 0.08f);
}

}  // namespace sw
