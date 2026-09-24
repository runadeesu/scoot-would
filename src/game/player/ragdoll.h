// scoot would - bail ragdoll (Jolt Ragdoll with swing twist constraints) + separated scooter body
#pragma once

#include "core/math.h"
#include "physics/physics_world.h"

#include <memory>
#include <vector>

namespace sw {

enum RagdollPart : int {
    RP_Pelvis = 0,
    RP_Chest,
    RP_Head,
    RP_UpperArmL,
    RP_LowerArmL,
    RP_HandL,
    RP_UpperArmR,
    RP_LowerArmR,
    RP_HandR,
    RP_ThighL,
    RP_ShinL,
    RP_FootL,
    RP_ThighR,
    RP_ShinR,
    RP_FootR,
    RP_Count
};

// rider joint driven by each ragdoll part (see rider_blueprint.h)
int ragdollPartJoint(int part);

class Ragdoll {
public:
    Ragdoll();
    ~Ragdoll();
    void build();  // once (rest pose settings)
    // spawn at the given model-to-world transform using the rider's current joint transforms
    // (model space, RJ_Count entries). Linear / angular velocity of the rider are inherited.
    void spawn(const Transform& modelToWorld, const std::vector<Transform>& jointModel, const Vec3& linVel, const Vec3& angVel,
               const Vec3& com);
    void spawnScooter(const Vec3& pos, const Quat& rot, const Vec3& linVel, const Vec3& angVel);
    void despawn();
    bool active() const { return active_; }
    // world transform of each part body (origin at the part's joint)
    Transform partTransform(int part) const;
    Transform scooterTransform() const;
    bool scooterActive() const { return scooterBody_ != kNoBody; }
    Vec3 centre() const;
    Vec3 velocity() const;
    float settledTime() const { return settled_; }
    void update(float dt);
    Quat partRestRotation(int part) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool active_ = false;
    BodyHandle scooterBody_ = kNoBody;
    float settled_ = 0.0f;
};

}  // namespace sw
