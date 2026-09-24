// scoot would - third person camera (follow, orbit, lag, look ahead, collision, speed FOV, air behaviour, shake)
#pragma once

#include "core/math.h"
#include "render/renderer.h"

namespace sw {

struct CameraTarget {
    Vec3 position;       // rider torso
    Vec3 velocity;
    Vec3 forward;        // scooter forward (horizontal)
    Vec3 groundNormal{0, 1, 0};
    bool grounded = true;
    bool fakie = false;
    bool grinding = false;
    float speed = 0.0f;
    uint32_t ignoreBody = 0xffffffffu;
};

enum class CameraMode { Follow = 0, Close, Far, Free, Count };

class CameraController {
public:
    void reset(const CameraTarget& t);
    void update(float dt, const CameraTarget& t, const Vec2& look, bool lookActive, bool resetPressed);
    void updateFree(float dt, const Vec3& move, const Vec2& mouse, float speed);
    RenderView view(float aspect) const;
    void addShake(float amount);
    void setMode(CameraMode m) { mode_ = m; }
    void cycleMode();
    CameraMode mode() const { return mode_; }
    Vec3 position() const { return pos_; }
    Vec3 forward() const { return (lookAt_ - pos_).normalized(); }
    Quat rotation() const;

    float baseFov = 72.0f;     // degrees (settings)
    int shakeSetting = 2;      // 0 off, 1 low, 2 normal
    float sensitivity = 1.0f;
    bool invertY = false;

private:
    CameraMode mode_ = CameraMode::Follow;
    Vec3 pos_{0, 2, 5};
    Vec3 lookAt_{0, 1, 0};
    Vec3 smoothTarget_;
    float yaw_ = 0.0f, pitch_ = 0.18f;
    float orbitYaw_ = 0.0f, orbitPitch_ = 0.0f;
    float orbitTimer_ = 0.0f;
    float fov_ = 72.0f;
    float dist_ = 3.4f;
    float shake_ = 0.0f;
    float shakeTime_ = 0.0f;
    float airPitch_ = 0.0f;
    // free fly
    float freeYaw_ = 0.0f, freePitch_ = 0.0f;
};

}  // namespace sw
