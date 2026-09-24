// scoot would - physics world (Jolt Physics wrapper)
#pragma once

#include "core/math.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace JPH {
class PhysicsSystem;
class BodyInterface;
class Shape;
}  // namespace JPH

namespace sw {

struct MeshData;
class DebugDraw;

using BodyHandle = uint32_t;
constexpr BodyHandle kNoBody = 0xffffffffu;

enum class PhysLayer : uint16_t { Static = 0, Dynamic = 1, Player = 2, Ragdoll = 3, Debris = 4, Sensor = 5, Count };

inline uint32_t layerBit(PhysLayer l) { return 1u << uint32_t(l); }
constexpr uint32_t kWorldMask = (1u << 0) | (1u << 1) | (1u << 4);  // what the player / wheels / camera collide with

// surface database (assets/data/surfaces.json)
struct SurfaceType {
    std::string name = "concrete";
    float friction = 0.9f;
    float rollingResistance = 0.012f;
    float grindFriction = 0.08f;
    float popFactor = 1.0f;
    Vec3 dustColor{0.6f, 0.58f, 0.55f};
    float dustAmount = 0.2f;
    std::string rollSound = "roll_concrete";
    std::string grindSound = "grind_concrete";
    std::string landSound = "land_concrete";
};

class SurfaceDB {
public:
    void load(const std::string& absPath);
    int find(const std::string& name) const;
    const SurfaceType& get(int id) const { return types_[size_t(id >= 0 && id < int(types_.size()) ? id : 0)]; }
    size_t count() const { return types_.size(); }

private:
    std::vector<SurfaceType> types_{SurfaceType{}};
};
SurfaceDB& surfaces();

struct RayHit {
    bool hit = false;
    Vec3 point;
    Vec3 normal{0, 1, 0};
    float distance = 0.0f;
    float fraction = 0.0f;
    BodyHandle body = kNoBody;
    uint32_t entity = 0;
    int surface = 0;
};

struct ContactEvent {
    BodyHandle self = kNoBody, other = kNoBody;
    Vec3 point;
    Vec3 normal;        // pointing from other towards self
    float approachSpeed = 0.0f;  // relative velocity into the contact (m/s, positive = impact)
    uint64_t selfSubShapeUser = 0;
    int otherSurface = 0;
    uint32_t otherEntity = 0;
    bool added = true;  // false = persisted
};

enum class ShapeKind { Box, Sphere, Capsule, ConvexHull };

struct ShapeDesc {
    ShapeKind kind = ShapeKind::Box;
    Vec3 halfExtents{0.5f};  // box
    float radius = 0.5f;     // sphere / capsule
    float halfHeight = 0.5f; // capsule cylinder half height (Y axis)
    std::vector<Vec3> points;  // convex hull
    Vec3 position;             // local offset
    Quat rotation;
    uint64_t userData = 0;     // retrievable from contacts (identify sub shapes)
};

enum class MotionKind { Static, Kinematic, Dynamic };

struct BodyDesc {
    std::vector<ShapeDesc> shapes;
    Vec3 position;
    Quat rotation;
    MotionKind motion = MotionKind::Dynamic;
    PhysLayer layer = PhysLayer::Dynamic;
    float mass = 1.0f;              // 0 = computed from density
    bool overrideCom = false;
    Vec3 centerOfMass;              // local, when overrideCom
    Vec3 inertiaScale{1, 1, 1};
    float friction = 0.6f;
    float restitution = 0.1f;
    float linearDamping = 0.02f;
    float angularDamping = 0.05f;
    float gravityFactor = 1.0f;
    bool continuous = false;        // linear cast CCD
    bool sensor = false;
    bool allowSleep = true;
    int surface = 0;
    uint32_t entity = 0;
};

class PhysicsWorld {
public:
    bool init();
    void shutdown();
    void step(float dt);
    void clearStatic();  // remove every static body (level reload)

    BodyHandle createStaticMesh(const MeshData& mesh, const Mat4& world, int surface, uint32_t entity);
    BodyHandle createStaticBox(const Vec3& center, const Vec3& halfExtents, const Quat& rot, int surface, uint32_t entity);
    BodyHandle createBody(const BodyDesc& desc);
    void removeBody(BodyHandle h);
    bool valid(BodyHandle h) const;

    Vec3 position(BodyHandle h) const;
    Quat rotation(BodyHandle h) const;
    Vec3 centerOfMass(BodyHandle h) const;  // world
    Vec3 linearVelocity(BodyHandle h) const;
    Vec3 angularVelocity(BodyHandle h) const;
    Vec3 pointVelocity(BodyHandle h, const Vec3& worldPoint) const;
    float inverseMass(BodyHandle h) const;
    Vec3 applyInverseInertia(BodyHandle h, const Vec3& v) const;  // world space I^-1 * v
    // effective mass for an impulse along dir at a world point
    float effectiveMass(BodyHandle h, const Vec3& point, const Vec3& dir) const;

    void setTransform(BodyHandle h, const Vec3& pos, const Quat& rot, bool activate = true);
    void setLinearVelocity(BodyHandle h, const Vec3& v);
    void setAngularVelocity(BodyHandle h, const Vec3& w);
    void addImpulse(BodyHandle h, const Vec3& impulse);
    void addImpulseAt(BodyHandle h, const Vec3& impulse, const Vec3& point);
    void addAngularImpulse(BodyHandle h, const Vec3& impulse);
    void addForce(BodyHandle h, const Vec3& f);
    void addTorque(BodyHandle h, const Vec3& t);
    void setGravityFactor(BodyHandle h, float g);
    void setMotion(BodyHandle h, MotionKind m);
    void setLayer(BodyHandle h, PhysLayer l);
    void activate(BodyHandle h);
    void setFriction(BodyHandle h, float f);
    int surfaceOf(BodyHandle h) const;
    uint32_t entityOf(BodyHandle h) const;

    bool raycast(const Vec3& origin, const Vec3& dir, float maxDist, RayHit& hit, uint32_t layerMask = kWorldMask,
                 BodyHandle ignore = kNoBody) const;
    bool sphereCast(const Vec3& origin, float radius, const Vec3& dir, float maxDist, RayHit& hit, uint32_t layerMask = kWorldMask,
                    BodyHandle ignore = kNoBody) const;
    int overlapSphere(const Vec3& center, float radius, std::vector<BodyHandle>& out, uint32_t layerMask = kWorldMask) const;

    // contact events for watched bodies
    void watchContacts(BodyHandle h, bool watch);
    std::vector<ContactEvent> takeContacts();

    void debugDraw(DebugDraw& dd, const Vec3& center, float radius);
    Vec3 gravity() const { return {0, -9.81f, 0}; }
    uint32_t bodyCount() const;
    uint32_t activeBodyCount() const;
    double lastStepMs() const { return lastStepMs_; }

    JPH::PhysicsSystem& system();
    JPH::BodyInterface& bodies();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    double lastStepMs_ = 0;
};

PhysicsWorld& physics();

}  // namespace sw
