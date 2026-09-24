#include "physics/physics_world.h"
#include "core/json.h"
#include "core/log.h"
#include "core/timer.h"
#include "render/debug_draw.h"
#include "render/mesh.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Renderer/DebugRendererSimple.h>

#include <algorithm>
#include <unordered_set>

namespace sw {

// ---------------------------------------------------------------------------
SurfaceDB& surfaces() {
    static SurfaceDB db;
    return db;
}

void SurfaceDB::load(const std::string& absPath) {
    auto j = loadJsonFile(absPath);
    if (!j) return;
    types_.clear();
    for (auto& [name, d] : j->items()) {
        if (!name.empty() && name[0] == '_') continue;
        SurfaceType t;
        t.name = name;
        t.friction = jget<float>(d, "friction", 0.9f);
        t.rollingResistance = jget<float>(d, "rollingResistance", 0.012f);
        t.grindFriction = jget<float>(d, "grindFriction", 0.08f);
        t.popFactor = jget<float>(d, "popFactor", 1.0f);
        t.dustColor = jcolor(d, "dustColor", Vec3(0.6f, 0.58f, 0.55f));
        t.dustAmount = jget<float>(d, "dustAmount", 0.2f);
        t.rollSound = jget<std::string>(d, "rollSound", "roll_concrete");
        t.grindSound = jget<std::string>(d, "grindSound", "grind_concrete");
        t.landSound = jget<std::string>(d, "landSound", "land_concrete");
        types_.push_back(t);
    }
    // concrete first so id 0 is a sane default
    std::stable_partition(types_.begin(), types_.end(), [](const SurfaceType& t) { return t.name == "concrete"; });
    if (types_.empty()) types_.push_back(SurfaceType{});
    LOG_INFO("physics: %zu surface types", types_.size());
}

int SurfaceDB::find(const std::string& name) const {
    for (size_t i = 0; i < types_.size(); ++i)
        if (types_[i].name == name) return int(i);
    return 0;
}

// ---------------------------------------------------------------------------
namespace {

using namespace JPH;

inline Vec3 toSw(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
inline JPH::Vec3 toJ(const sw::Vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
inline sw::Quat toSw(JPH::QuatArg q) { return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}; }
inline JPH::Quat toJ(const sw::Quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w).Normalized(); }

namespace BPLayers {
constexpr BroadPhaseLayer NonMoving(0);
constexpr BroadPhaseLayer Moving(1);
constexpr uint NumLayers = 2;
}  // namespace BPLayers

class BPLayerInterface final : public BroadPhaseLayerInterface {
public:
    uint GetNumBroadPhaseLayers() const override { return BPLayers::NumLayers; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override {
        return layer == ObjectLayer(PhysLayer::Static) ? BPLayers::NonMoving : BPLayers::Moving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer layer) const override { return layer == BPLayers::NonMoving ? "static" : "moving"; }
#endif
};

class ObjVsBPFilter final : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer l, BroadPhaseLayer bp) const override {
        if (l == ObjectLayer(PhysLayer::Static)) return bp == BPLayers::Moving;
        return true;
    }
};

class ObjPairFilter final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override {
        auto L = [](ObjectLayer x) { return PhysLayer(x); };
        PhysLayer la = L(a), lb = L(b);
        if (la == PhysLayer::Static && lb == PhysLayer::Static) return false;
        if (la == PhysLayer::Sensor || lb == PhysLayer::Sensor) return la != PhysLayer::Static && lb != PhysLayer::Static;
        if ((la == PhysLayer::Player && lb == PhysLayer::Ragdoll) || (la == PhysLayer::Ragdoll && lb == PhysLayer::Player)) return false;
        return true;
    }
};

// layer mask based filters for queries
class MaskObjectFilter final : public ObjectLayerFilter {
public:
    explicit MaskObjectFilter(uint32_t m) : mask(m) {}
    bool ShouldCollide(ObjectLayer l) const override { return (mask & (1u << uint32_t(l))) != 0; }
    uint32_t mask;
};

class IgnoreBodyFilter final : public BodyFilter {
public:
    explicit IgnoreBodyFilter(BodyID id) : ignore(id) {}
    bool ShouldCollide(const BodyID& id) const override { return id != ignore; }
    bool ShouldCollideLocked(const Body& b) const override { return b.GetID() != ignore && !b.IsSensor(); }
    BodyID ignore;
};

class DebugRendererImpl final : public DebugRendererSimple {
public:
    DebugDraw* dd = nullptr;
    void DrawLine(RVec3Arg a, RVec3Arg b, ColorArg c) override {
        if (dd) dd->line(toSw(JPH::Vec3(a)), toSw(JPH::Vec3(b)), sw::Vec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f));
    }
    void DrawTriangle(RVec3Arg a, RVec3Arg b, RVec3Arg c, ColorArg col, ECastShadow) override {
        DrawLine(a, b, col);
        DrawLine(b, c, col);
        DrawLine(c, a, col);
    }
    void DrawText3D(RVec3Arg, const string_view&, ColorArg, float) override {}
};

}  // namespace

// ---------------------------------------------------------------------------
struct PhysicsWorld::Impl : public ContactListener {
    std::unique_ptr<TempAllocatorImpl> temp;
    std::unique_ptr<JobSystemThreadPool> jobs;
    std::unique_ptr<PhysicsSystem> system;
    BPLayerInterface bpInterface;
    ObjVsBPFilter objVsBp;
    ObjPairFilter pairFilter;
    std::unique_ptr<DebugRendererImpl> debugRenderer;
    std::vector<BodyID> staticBodies;
    std::unordered_set<uint32_t> watched;
    std::mutex contactMutex;
    std::vector<ContactEvent> contacts;

    // --- ContactListener --------------------------------------------------------------
    void record(const Body& b1, const Body& b2, const ContactManifold& m, bool added) {
        bool w1 = watched.count(b1.GetID().GetIndexAndSequenceNumber()) > 0;
        bool w2 = watched.count(b2.GetID().GetIndexAndSequenceNumber()) > 0;
        if (!w1 && !w2) return;
        auto emit = [&](const Body& self, const Body& other, JPH::Vec3 normalTowardSelf, const SubShapeID& selfSub) {
            ContactEvent e;
            e.self = self.GetID().GetIndexAndSequenceNumber();
            e.other = other.GetID().GetIndexAndSequenceNumber();
            RVec3 p = m.GetWorldSpaceContactPointOn1(0);
            e.point = toSw(JPH::Vec3(p));
            e.normal = toSw(normalTowardSelf);
            JPH::Vec3 vSelf = self.GetPointVelocity(p), vOther = other.GetPointVelocity(p);
            e.approachSpeed = -(vSelf - vOther).Dot(normalTowardSelf);
            e.selfSubShapeUser = self.GetShape()->GetSubShapeUserData(selfSub);
            uint64 ud = other.GetUserData();
            e.otherSurface = int(ud & 0xff);
            e.otherEntity = uint32_t(ud >> 8);
            e.added = added;
            std::lock_guard<std::mutex> lock(contactMutex);
            if (contacts.size() < 512) contacts.push_back(e);
        };
        // manifold normal points from body 1 towards body 2
        if (w1) emit(b1, b2, -m.mWorldSpaceNormal, m.mSubShapeID1);
        if (w2) emit(b2, b1, m.mWorldSpaceNormal, m.mSubShapeID2);
    }
    void OnContactAdded(const Body& b1, const Body& b2, const ContactManifold& m, ContactSettings&) override { record(b1, b2, m, true); }
    void OnContactPersisted(const Body& b1, const Body& b2, const ContactManifold& m, ContactSettings&) override { record(b1, b2, m, false); }
};

PhysicsWorld& physics() {
    static PhysicsWorld w;
    return w;
}

static void joltTrace(const char* fmt, ...) {
    va_list list;
    va_start(list, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, list);
    va_end(list);
    LOG_TRACE("jolt: %s", buf);
}

#ifdef JPH_ENABLE_ASSERTS
static bool joltAssert(const char* expr, const char* msg, const char* file, uint line) {
    LOG_ERROR("jolt assert %s:%u: (%s) %s", file, line, expr, msg ? msg : "");
    return false;
}
#endif

bool PhysicsWorld::init() {
    RegisterDefaultAllocator();
    Trace = joltTrace;
    JPH_IF_ENABLE_ASSERTS(AssertFailed = joltAssert;)
    Factory::sInstance = new Factory();
    RegisterTypes();
    impl_ = std::make_unique<Impl>();
    impl_->temp = std::make_unique<TempAllocatorImpl>(32 * 1024 * 1024);
    int threads = std::max(1, std::min(4, int(std::thread::hardware_concurrency()) - 1));
    impl_->jobs = std::make_unique<JobSystemThreadPool>(cMaxPhysicsJobs, cMaxPhysicsBarriers, threads);
    impl_->system = std::make_unique<PhysicsSystem>();
    impl_->system->Init(65536, 0, 65536, 20480, impl_->bpInterface, impl_->objVsBp, impl_->pairFilter);
    impl_->system->SetGravity(JPH::Vec3(0, -9.81f, 0));
    impl_->system->SetContactListener(impl_.get());
    PhysicsSettings ps = impl_->system->GetPhysicsSettings();
    ps.mNumVelocitySteps = 10;
    ps.mNumPositionSteps = 2;
    impl_->system->SetPhysicsSettings(ps);
    impl_->debugRenderer = std::make_unique<DebugRendererImpl>();
    LOG_INFO("physics: Jolt initialised (%d worker threads)", threads);
    return true;
}

void PhysicsWorld::shutdown() {
    if (!impl_) return;
    impl_->system.reset();
    impl_->jobs.reset();
    impl_->temp.reset();
    impl_->debugRenderer.reset();
    impl_.reset();
    UnregisterTypes();
    delete Factory::sInstance;
    Factory::sInstance = nullptr;
}

PhysicsSystem& PhysicsWorld::system() { return *impl_->system; }
BodyInterface& PhysicsWorld::bodies() { return impl_->system->GetBodyInterface(); }

void PhysicsWorld::step(float dt) {
    Timer t;
    impl_->system->Update(dt, 1, impl_->temp.get(), impl_->jobs.get());
    lastStepMs_ = t.milliseconds();
    Profiler::add(ProfileSection::Physics, 0.0);
}

static uint64 packUser(int surface, uint32_t entity) { return (uint64(entity) << 8) | uint64(surface & 0xff); }

BodyHandle PhysicsWorld::createStaticMesh(const MeshData& mesh, const Mat4& world, int surface, uint32_t entity) {
    if (mesh.indices.size() < 3) return kNoBody;
    VertexList verts;
    verts.reserve(mesh.vertices.size());
    for (const sw::Vertex& v : mesh.vertices) {
        sw::Vec3 p = world.transformPoint(v.position);
        verts.push_back(Float3(p.x, p.y, p.z));
    }
    IndexedTriangleList tris;
    tris.reserve(mesh.indices.size() / 3);
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32 a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        if (a == b || b == c || a == c) continue;
        tris.push_back(IndexedTriangle(a, b, c, 0));
    }
    MeshShapeSettings ms(std::move(verts), std::move(tris));
    ms.mActiveEdgeCosThresholdAngle = std::cos(8.0f * kDeg2Rad);
    auto result = ms.Create();
    if (result.HasError()) {
        LOG_WARN("physics: mesh shape error for '%s': %s", mesh.name.c_str(), result.GetError().c_str());
        return kNoBody;
    }
    BodyCreationSettings bcs(result.Get(), RVec3::sZero(), JPH::Quat::sIdentity(), EMotionType::Static, ObjectLayer(PhysLayer::Static));
    bcs.mFriction = surfaces().get(surface).friction;
    bcs.mRestitution = 0.0f;
    bcs.mUserData = packUser(surface, entity);
    BodyID id = bodies().CreateAndAddBody(bcs, EActivation::DontActivate);
    if (id.IsInvalid()) return kNoBody;
    impl_->staticBodies.push_back(id);
    return id.GetIndexAndSequenceNumber();
}

BodyHandle PhysicsWorld::createStaticBox(const sw::Vec3& center, const sw::Vec3& he, const sw::Quat& rot, int surface, uint32_t entity) {
    BodyCreationSettings bcs(new BoxShape(toJ(vmax(he, sw::Vec3(0.01f))), 0.01f), RVec3(toJ(center)), toJ(rot), EMotionType::Static,
                             ObjectLayer(PhysLayer::Static));
    bcs.mFriction = surfaces().get(surface).friction;
    bcs.mUserData = packUser(surface, entity);
    BodyID id = bodies().CreateAndAddBody(bcs, EActivation::DontActivate);
    if (id.IsInvalid()) return kNoBody;
    impl_->staticBodies.push_back(id);
    return id.GetIndexAndSequenceNumber();
}

static Ref<Shape> makeShape(const ShapeDesc& s) {
    Ref<Shape> shape;
    switch (s.kind) {
        case ShapeKind::Box: {
            float cr = std::min({0.05f, s.halfExtents.x * 0.5f, s.halfExtents.y * 0.5f, s.halfExtents.z * 0.5f});
            shape = new BoxShape(toJ(vmax(s.halfExtents, sw::Vec3(0.005f))), cr);
            break;
        }
        case ShapeKind::Sphere: shape = new SphereShape(s.radius); break;
        case ShapeKind::Capsule: shape = new CapsuleShape(s.halfHeight, s.radius); break;
        case ShapeKind::ConvexHull: {
            Array<JPH::Vec3> pts;
            for (auto& p : s.points) pts.push_back(toJ(p));
            ConvexHullShapeSettings hs(pts, 0.01f);
            auto r = hs.Create();
            if (r.HasError()) shape = new SphereShape(0.1f);
            else shape = r.Get();
            break;
        }
    }
    shape->SetUserData(s.userData);
    return shape;
}

BodyHandle PhysicsWorld::createBody(const BodyDesc& d) {
    if (d.shapes.empty()) return kNoBody;
    Ref<Shape> shape;
    if (d.shapes.size() == 1 && d.shapes[0].position == sw::Vec3(0) && d.shapes[0].rotation.w == 1.0f) {
        shape = makeShape(d.shapes[0]);
    } else {
        StaticCompoundShapeSettings cs;
        for (const ShapeDesc& s : d.shapes) cs.AddShape(toJ(s.position), toJ(s.rotation), makeShape(s), uint32(s.userData));
        auto r = cs.Create();
        if (r.HasError()) {
            LOG_ERROR("physics: compound error: %s", r.GetError().c_str());
            return kNoBody;
        }
        shape = r.Get();
    }
    if (d.overrideCom) {
        JPH::Vec3 com = shape->GetCenterOfMass();
        shape = new OffsetCenterOfMassShape(shape, toJ(d.centerOfMass) - com);
    }
    EMotionType mt = d.motion == MotionKind::Static ? EMotionType::Static : d.motion == MotionKind::Kinematic ? EMotionType::Kinematic : EMotionType::Dynamic;
    BodyCreationSettings bcs(shape, RVec3(toJ(d.position)), toJ(d.rotation), mt, ObjectLayer(d.layer));
    bcs.mFriction = d.friction;
    bcs.mRestitution = d.restitution;
    bcs.mLinearDamping = d.linearDamping;
    bcs.mAngularDamping = d.angularDamping;
    bcs.mGravityFactor = d.gravityFactor;
    bcs.mMotionQuality = d.continuous ? EMotionQuality::LinearCast : EMotionQuality::Discrete;
    bcs.mIsSensor = d.sensor;
    bcs.mAllowSleeping = d.allowSleep;
    bcs.mUserData = packUser(d.surface, d.entity);
    bcs.mMaxAngularVelocity = 60.0f;
    if (d.mass > 0.0f && mt == EMotionType::Dynamic) {
        bcs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
        bcs.mMassPropertiesOverride.mMass = d.mass;
        bcs.mInertiaMultiplier = 1.0f;
    }
    BodyID id = bodies().CreateAndAddBody(bcs, mt == EMotionType::Static ? EActivation::DontActivate : EActivation::Activate);
    if (id.IsInvalid()) return kNoBody;
    if (mt == EMotionType::Static) impl_->staticBodies.push_back(id);
    if (d.inertiaScale != sw::Vec3(1, 1, 1) && mt == EMotionType::Dynamic) {
        BodyLockWrite lock(impl_->system->GetBodyLockInterface(), id);
        if (lock.Succeeded()) {
            MotionProperties* mp = lock.GetBody().GetMotionProperties();
            JPH::Vec3 inv = mp->GetInverseInertiaDiagonal();
            JPH::Vec3 scaled(inv.GetX() / d.inertiaScale.x, inv.GetY() / d.inertiaScale.y, inv.GetZ() / d.inertiaScale.z);
            mp->SetInverseInertia(scaled, mp->GetInertiaRotation());
        }
    }
    return id.GetIndexAndSequenceNumber();
}

void PhysicsWorld::removeBody(BodyHandle h) {
    if (h == kNoBody || !impl_) return;
    BodyID id(h);
    if (!bodies().IsAdded(id)) return;
    bodies().RemoveBody(id);
    bodies().DestroyBody(id);
    impl_->watched.erase(h);
    auto& sb = impl_->staticBodies;
    auto it = std::find(sb.begin(), sb.end(), id);
    if (it != sb.end()) sb.erase(it);
}

void PhysicsWorld::clearStatic() {
    for (BodyID id : impl_->staticBodies) {
        if (bodies().IsAdded(id)) {
            bodies().RemoveBody(id);
            bodies().DestroyBody(id);
        }
    }
    impl_->staticBodies.clear();
    impl_->system->OptimizeBroadPhase();
}

bool PhysicsWorld::valid(BodyHandle h) const { return h != kNoBody && impl_ && impl_->system->GetBodyInterface().IsAdded(BodyID(h)); }

sw::Vec3 PhysicsWorld::position(BodyHandle h) const { return toSw(JPH::Vec3(impl_->system->GetBodyInterface().GetPosition(BodyID(h)))); }
sw::Quat PhysicsWorld::rotation(BodyHandle h) const { return toSw(impl_->system->GetBodyInterface().GetRotation(BodyID(h))); }
sw::Vec3 PhysicsWorld::centerOfMass(BodyHandle h) const { return toSw(JPH::Vec3(impl_->system->GetBodyInterface().GetCenterOfMassPosition(BodyID(h)))); }
sw::Vec3 PhysicsWorld::linearVelocity(BodyHandle h) const { return toSw(impl_->system->GetBodyInterface().GetLinearVelocity(BodyID(h))); }
sw::Vec3 PhysicsWorld::angularVelocity(BodyHandle h) const { return toSw(impl_->system->GetBodyInterface().GetAngularVelocity(BodyID(h))); }
sw::Vec3 PhysicsWorld::pointVelocity(BodyHandle h, const sw::Vec3& p) const {
    return toSw(impl_->system->GetBodyInterface().GetPointVelocity(BodyID(h), RVec3(toJ(p))));
}

float PhysicsWorld::inverseMass(BodyHandle h) const {
    BodyLockRead lock(impl_->system->GetBodyLockInterface(), BodyID(h));
    if (!lock.Succeeded() || !lock.GetBody().IsDynamic()) return 0.0f;
    return lock.GetBody().GetMotionProperties()->GetInverseMass();
}

sw::Vec3 PhysicsWorld::applyInverseInertia(BodyHandle h, const sw::Vec3& v) const {
    BodyLockRead lock(impl_->system->GetBodyLockInterface(), BodyID(h));
    if (!lock.Succeeded() || !lock.GetBody().IsDynamic()) return sw::Vec3(0);
    Mat44 inv = lock.GetBody().GetInverseInertia();
    return toSw(inv.Multiply3x3(toJ(v)));
}

float PhysicsWorld::effectiveMass(BodyHandle h, const sw::Vec3& point, const sw::Vec3& dir) const {
    BodyLockRead lock(impl_->system->GetBodyLockInterface(), BodyID(h));
    if (!lock.Succeeded() || !lock.GetBody().IsDynamic()) return 0.0f;
    const Body& b = lock.GetBody();
    JPH::Vec3 r = JPH::Vec3(RVec3(toJ(point)) - b.GetCenterOfMassPosition());
    JPH::Vec3 n = toJ(dir);
    JPH::Vec3 rn = r.Cross(n);
    float k = b.GetMotionProperties()->GetInverseMass() + rn.Dot(b.GetInverseInertia().Multiply3x3(rn));
    return k > 1e-8f ? 1.0f / k : 0.0f;
}

void PhysicsWorld::setTransform(BodyHandle h, const sw::Vec3& p, const sw::Quat& r, bool act) {
    impl_->system->GetBodyInterface().SetPositionAndRotation(BodyID(h), RVec3(toJ(p)), toJ(r), act ? EActivation::Activate : EActivation::DontActivate);
}
void PhysicsWorld::setLinearVelocity(BodyHandle h, const sw::Vec3& v) { impl_->system->GetBodyInterface().SetLinearVelocity(BodyID(h), toJ(v)); }
void PhysicsWorld::setAngularVelocity(BodyHandle h, const sw::Vec3& w) { impl_->system->GetBodyInterface().SetAngularVelocity(BodyID(h), toJ(w)); }
void PhysicsWorld::addImpulse(BodyHandle h, const sw::Vec3& j) { impl_->system->GetBodyInterface().AddImpulse(BodyID(h), toJ(j)); }
void PhysicsWorld::addImpulseAt(BodyHandle h, const sw::Vec3& j, const sw::Vec3& p) {
    impl_->system->GetBodyInterface().AddImpulse(BodyID(h), toJ(j), RVec3(toJ(p)));
}
void PhysicsWorld::addAngularImpulse(BodyHandle h, const sw::Vec3& j) { impl_->system->GetBodyInterface().AddAngularImpulse(BodyID(h), toJ(j)); }
void PhysicsWorld::addForce(BodyHandle h, const sw::Vec3& f) { impl_->system->GetBodyInterface().AddForce(BodyID(h), toJ(f)); }
void PhysicsWorld::addTorque(BodyHandle h, const sw::Vec3& t) { impl_->system->GetBodyInterface().AddTorque(BodyID(h), toJ(t)); }
void PhysicsWorld::setGravityFactor(BodyHandle h, float g) { impl_->system->GetBodyInterface().SetGravityFactor(BodyID(h), g); }
void PhysicsWorld::setMotion(BodyHandle h, MotionKind m) {
    EMotionType mt = m == MotionKind::Static ? EMotionType::Static : m == MotionKind::Kinematic ? EMotionType::Kinematic : EMotionType::Dynamic;
    impl_->system->GetBodyInterface().SetMotionType(BodyID(h), mt, EActivation::Activate);
}
void PhysicsWorld::setLayer(BodyHandle h, PhysLayer l) { impl_->system->GetBodyInterface().SetObjectLayer(BodyID(h), ObjectLayer(l)); }
void PhysicsWorld::activate(BodyHandle h) { impl_->system->GetBodyInterface().ActivateBody(BodyID(h)); }
void PhysicsWorld::setFriction(BodyHandle h, float f) { impl_->system->GetBodyInterface().SetFriction(BodyID(h), f); }
int PhysicsWorld::surfaceOf(BodyHandle h) const { return int(impl_->system->GetBodyInterface().GetUserData(BodyID(h)) & 0xff); }
uint32_t PhysicsWorld::entityOf(BodyHandle h) const { return uint32_t(impl_->system->GetBodyInterface().GetUserData(BodyID(h)) >> 8); }

static void fillHit(PhysicsSystem& sys, const BodyID& id, const SubShapeID& sub, RVec3Arg point, JPH::Vec3Arg dir, RayHit& hit) {
    BodyLockRead lock(sys.GetBodyLockInterface(), id);
    if (!lock.Succeeded()) return;
    const Body& b = lock.GetBody();
    JPH::Vec3 n = b.GetWorldSpaceSurfaceNormal(sub, point);
    if (n.Dot(dir) > 0.0f) n = -n;
    hit.normal = toSw(n);
    uint64 ud = b.GetUserData();
    hit.surface = int(ud & 0xff);
    hit.entity = uint32_t(ud >> 8);
    hit.body = id.GetIndexAndSequenceNumber();
}

bool PhysicsWorld::raycast(const sw::Vec3& origin, const sw::Vec3& dir, float maxDist, RayHit& hit, uint32_t mask, BodyHandle ignore) const {
    hit = RayHit{};
    sw::Vec3 d = dir.normalized() * maxDist;
    RRayCast ray(RVec3(toJ(origin)), toJ(d));
    RayCastResult res;
    MaskObjectFilter of(mask);
    IgnoreBodyFilter bf(ignore == kNoBody ? BodyID() : BodyID(ignore));
    if (!impl_->system->GetNarrowPhaseQuery().CastRay(ray, res, {}, of, bf)) return false;
    hit.hit = true;
    hit.fraction = res.mFraction;
    hit.distance = res.mFraction * maxDist;
    RVec3 p = ray.GetPointOnRay(res.mFraction);
    hit.point = toSw(JPH::Vec3(p));
    fillHit(*impl_->system, res.mBodyID, res.mSubShapeID2, p, toJ(d.normalized()), hit);
    return true;
}

bool PhysicsWorld::sphereCast(const sw::Vec3& origin, float radius, const sw::Vec3& dir, float maxDist, RayHit& hit, uint32_t mask,
                              BodyHandle ignore) const {
    hit = RayHit{};
    SphereShape sphere(radius);
    sphere.SetEmbedded();
    sw::Vec3 d = dir.normalized() * maxDist;
    RShapeCast cast = RShapeCast::sFromWorldTransform(&sphere, JPH::Vec3::sReplicate(1.0f), RMat44::sTranslation(RVec3(toJ(origin))), toJ(d));
    ShapeCastSettings settings;
    settings.mBackFaceModeTriangles = EBackFaceMode::IgnoreBackFaces;
    settings.mBackFaceModeConvex = EBackFaceMode::IgnoreBackFaces;
    settings.mReturnDeepestPoint = true;
    ClosestHitCollisionCollector<CastShapeCollector> collector;
    MaskObjectFilter of(mask);
    IgnoreBodyFilter bf(ignore == kNoBody ? BodyID() : BodyID(ignore));
    impl_->system->GetNarrowPhaseQuery().CastShape(cast, settings, RVec3::sZero(), collector, {}, of, bf);
    if (!collector.HadHit()) return false;
    const ShapeCastResult& r = collector.mHit;
    hit.hit = true;
    hit.fraction = r.mFraction;
    hit.distance = r.mFraction * maxDist;
    hit.point = toSw(JPH::Vec3(r.mContactPointOn2));
    hit.normal = toSw(-r.mPenetrationAxis.Normalized());
    {
        BodyLockRead lock(impl_->system->GetBodyLockInterface(), r.mBodyID2);
        if (lock.Succeeded()) {
            const Body& b = lock.GetBody();
            // prefer the true surface normal (penetration axis is unreliable at edges)
            JPH::Vec3 sn = b.GetWorldSpaceSurfaceNormal(r.mSubShapeID2, r.mContactPointOn2);
            if (sn.Dot(toJ(hit.normal)) > 0.3f) hit.normal = toSw(sn);
            uint64 ud = b.GetUserData();
            hit.surface = int(ud & 0xff);
            hit.entity = uint32_t(ud >> 8);
            hit.body = r.mBodyID2.GetIndexAndSequenceNumber();
        }
    }
    return true;
}

int PhysicsWorld::overlapSphere(const sw::Vec3& center, float radius, std::vector<BodyHandle>& out, uint32_t mask) const {
    out.clear();
    SphereShape sphere(radius);
    sphere.SetEmbedded();
    CollideShapeSettings settings;
    AllHitCollisionCollector<CollideShapeCollector> collector;
    MaskObjectFilter of(mask);
    impl_->system->GetNarrowPhaseQuery().CollideShape(&sphere, JPH::Vec3::sReplicate(1.0f), RMat44::sTranslation(RVec3(toJ(center))), settings,
                                                      RVec3::sZero(), collector, {}, of);
    for (auto& h : collector.mHits) {
        BodyHandle b = h.mBodyID2.GetIndexAndSequenceNumber();
        if (std::find(out.begin(), out.end(), b) == out.end()) out.push_back(b);
    }
    return int(out.size());
}

void PhysicsWorld::watchContacts(BodyHandle h, bool watch) {
    if (watch)
        impl_->watched.insert(h);
    else
        impl_->watched.erase(h);
}

std::vector<ContactEvent> PhysicsWorld::takeContacts() {
    std::lock_guard<std::mutex> lock(impl_->contactMutex);
    std::vector<ContactEvent> out;
    out.swap(impl_->contacts);
    return out;
}

void PhysicsWorld::debugDraw(DebugDraw& dd, const sw::Vec3& center, float radius) {
    impl_->debugRenderer->dd = &dd;
    BodyManager::DrawSettings settings;
    settings.mDrawShape = true;
    settings.mDrawShapeWireframe = true;
    settings.mDrawBoundingBox = false;
    settings.mDrawCenterOfMassTransform = false;
    // draw only bodies near the player (large static meshes are expensive as lines)
    class NearFilter final : public BodyDrawFilter {
    public:
        RVec3 c;
        float r2;
        bool ShouldDraw(const Body& b) const override { return (b.GetCenterOfMassPosition() - c).LengthSq() < r2 || b.GetMotionType() != EMotionType::Static; }
    } filter;
    filter.c = RVec3(toJ(center));
    filter.r2 = radius * radius;
    impl_->system->DrawBodies(settings, impl_->debugRenderer.get(), &filter);
}

uint32_t PhysicsWorld::bodyCount() const { return impl_ ? impl_->system->GetNumBodies() : 0; }
uint32_t PhysicsWorld::activeBodyCount() const { return impl_ ? impl_->system->GetNumActiveBodies(EBodyType::RigidBody) : 0; }

}  // namespace sw
