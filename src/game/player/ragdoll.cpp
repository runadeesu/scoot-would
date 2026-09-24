#include "game/player/ragdoll.h"
#include "core/log.h"
#include "game/player/rider_blueprint.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/Skeleton/Skeleton.h>

namespace sw {

namespace {
inline JPH::Vec3 J(const Vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
inline Vec3 S(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
inline Quat S(JPH::QuatArg q) { return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}; }

struct PartDef {
    const char* name;
    int parent;       // ragdoll part index
    int joint;        // rider joint (body origin)
    int childJoint;   // bone end (-1 = use endOffset)
    Vec3 endOffset;   // when childJoint < 0
    float radius;
    float mass;
    int kind;         // 0 capsule, 1 box, 2 sphere
    int constraint;   // 0 swing twist, 1 knee hinge, 2 elbow hinge
    float cone1, cone2, twist;  // degrees
};

const PartDef kParts[RP_Count] = {
    {"pelvis", -1, RJ_Pelvis, RJ_Spine, {}, 0.13f, 11.0f, 1, 0, 0, 0, 0},
    {"chest", RP_Pelvis, RJ_Spine, RJ_Neck, {}, 0.14f, 22.0f, 0, 0, 30, 30, 20},
    {"head", RP_Chest, RJ_Neck, -1, {0, 0.28f, 0}, 0.11f, 5.0f, 2, 0, 40, 40, 50},
    {"upperarm_l", RP_Chest, RJ_UpperArmL, RJ_LowerArmL, {}, 0.05f, 2.2f, 0, 0, 80, 80, 60},
    {"lowerarm_l", RP_UpperArmL, RJ_LowerArmL, RJ_HandL, {}, 0.045f, 1.4f, 0, 2, 0, 0, 0},
    {"hand_l", RP_LowerArmL, RJ_HandL, -1, {-0.01f, -0.09f, -0.02f}, 0.04f, 0.5f, 1, 0, 40, 40, 30},
    {"upperarm_r", RP_Chest, RJ_UpperArmR, RJ_LowerArmR, {}, 0.05f, 2.2f, 0, 0, 80, 80, 60},
    {"lowerarm_r", RP_UpperArmR, RJ_LowerArmR, RJ_HandR, {}, 0.045f, 1.4f, 0, 2, 0, 0, 0},
    {"hand_r", RP_LowerArmR, RJ_HandR, -1, {0.01f, -0.09f, -0.02f}, 0.04f, 0.5f, 1, 0, 40, 40, 30},
    {"thigh_l", RP_Pelvis, RJ_ThighL, RJ_ShinL, {}, 0.075f, 8.0f, 0, 0, 70, 45, 30},
    {"shin_l", RP_ThighL, RJ_ShinL, RJ_FootL, {}, 0.055f, 4.0f, 0, 1, 0, 0, 0},
    {"foot_l", RP_ShinL, RJ_FootL, -1, {0.0f, -0.05f, -0.17f}, 0.045f, 1.2f, 1, 0, 30, 30, 10},
    {"thigh_r", RP_Pelvis, RJ_ThighR, RJ_ShinR, {}, 0.075f, 8.0f, 0, 0, 70, 45, 30},
    {"shin_r", RP_ThighR, RJ_ShinR, RJ_FootR, {}, 0.055f, 4.0f, 0, 1, 0, 0, 0},
    {"foot_r", RP_ShinR, RJ_FootR, -1, {0.0f, -0.05f, -0.17f}, 0.045f, 1.2f, 1, 0, 30, 30, 10},
};
}  // namespace

int ragdollPartJoint(int part) { return kParts[part].joint; }

struct Ragdoll::Impl {
    JPH::Ref<JPH::RagdollSettings> settings;
    JPH::Ref<JPH::Ragdoll> ragdoll;
    uint32_t group = 1;
};

Ragdoll::Ragdoll() : impl_(std::make_unique<Impl>()) {}
Ragdoll::~Ragdoll() = default;

Quat Ragdoll::partRestRotation(int) const { return Quat::identity(); }

void Ragdoll::build() {
    using namespace JPH;
    const RiderJointDef* rj = riderJoints();
    Ref<Skeleton> skel = new Skeleton;
    for (int i = 0; i < RP_Count; ++i) skel->AddJoint(kParts[i].name, kParts[i].parent);
    Ref<RagdollSettings> rs = new RagdollSettings;
    rs->mSkeleton = skel;
    rs->mParts.resize(RP_Count);
    for (int i = 0; i < RP_Count; ++i) {
        const PartDef& pd = kParts[i];
        Vec3 a = rj[pd.joint].restWorld;
        Vec3 b = pd.childJoint >= 0 ? rj[pd.childJoint].restWorld : a + pd.endOffset;
        Vec3 dir = b - a;
        float len = std::max(dir.length(), 0.02f);
        Vec3 d = dir / len;
        Vec3 mid = (a + b) * 0.5f;
        Ref<Shape> shape;
        switch (pd.kind) {
            case 0: {
                float half = std::max(0.01f, len * 0.5f - pd.radius * 0.6f);
                Quat q = Quat::fromTo(Vec3(0, 1, 0), d);
                shape = new RotatedTranslatedShape(J(mid - a), JPH::Quat(q.x, q.y, q.z, q.w), new CapsuleShape(half, pd.radius));
                break;
            }
            case 1: {
                Vec3 he = i == RP_Pelvis ? Vec3(0.15f, 0.09f, 0.1f) : Vec3(pd.radius, pd.radius * 0.8f, len * 0.5f);
                Quat q = i == RP_Pelvis ? Quat::identity() : Quat::lookRotation(-d, std::fabs(d.y) > 0.9f ? Vec3(0, 0, 1) : Vec3(0, 1, 0));
                if (i == RP_HandL || i == RP_HandR) {
                    he = Vec3(0.03f, len * 0.5f, 0.045f);
                    q = Quat::fromTo(Vec3(0, 1, 0), d);
                }
                Vec3 c = i == RP_Pelvis ? Vec3(0, -0.02f, 0) : mid - a;
                shape = new RotatedTranslatedShape(J(c), JPH::Quat(q.x, q.y, q.z, q.w), new BoxShape(J(he), 0.01f));
                break;
            }
            default: shape = new RotatedTranslatedShape(J(mid - a), JPH::Quat::sIdentity(), new SphereShape(pd.radius)); break;
        }
        RagdollSettings::Part& part = rs->mParts[i];
        part.SetShape(shape);
        part.mPosition = RVec3(J(a));
        part.mRotation = JPH::Quat::sIdentity();
        part.mMotionType = EMotionType::Dynamic;
        part.mObjectLayer = ObjectLayer(PhysLayer::Ragdoll);
        part.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
        part.mMassPropertiesOverride.mMass = pd.mass;
        part.mFriction = 0.7f;
        part.mRestitution = 0.05f;
        part.mLinearDamping = 0.08f;
        part.mAngularDamping = 0.35f;
        part.mMotionQuality = (i == RP_Pelvis || i == RP_Chest || i == RP_Head) ? EMotionQuality::LinearCast : EMotionQuality::Discrete;
        if (pd.parent >= 0) {
            if (pd.constraint == 0) {
                Ref<SwingTwistConstraintSettings> c = new SwingTwistConstraintSettings;
                c->mSpace = EConstraintSpace::WorldSpace;
                c->mPosition1 = c->mPosition2 = RVec3(J(a));
                c->mTwistAxis1 = c->mTwistAxis2 = J(d);
                Vec3 plane = anyPerpendicular(d);
                c->mPlaneAxis1 = c->mPlaneAxis2 = J(plane);
                c->mNormalHalfConeAngle = pd.cone1 * kDeg2Rad;
                c->mPlaneHalfConeAngle = pd.cone2 * kDeg2Rad;
                c->mTwistMinAngle = -pd.twist * kDeg2Rad;
                c->mTwistMaxAngle = pd.twist * kDeg2Rad;
                part.mToParent = c;
            } else {
                Ref<HingeConstraintSettings> h = new HingeConstraintSettings;
                h->mSpace = EConstraintSpace::WorldSpace;
                h->mPoint1 = h->mPoint2 = RVec3(J(a));
                Vec3 axis(1, 0, 0);
                Vec3 normal = (d - axis * dot(d, axis)).normalized();
                h->mHingeAxis1 = h->mHingeAxis2 = J(axis);
                h->mNormalAxis1 = h->mNormalAxis2 = J(normal);
                if (pd.constraint == 1) {  // knee bends backwards
                    h->mLimitsMin = -2.4f;
                    h->mLimitsMax = 0.05f;
                } else {  // elbow bends forwards
                    h->mLimitsMin = -0.05f;
                    h->mLimitsMax = 2.4f;
                }
                part.mToParent = h;
            }
        }
    }
    rs->Stabilize();
    rs->DisableParentChildCollisions();
    rs->CalculateBodyIndexToConstraintIndex();
    rs->CalculateConstraintIndexToBodyIdxPair();
    impl_->settings = rs;
}

void Ragdoll::spawn(const Transform& modelToWorld, const std::vector<Transform>& jointModel, const Vec3& linVel, const Vec3& angVel,
                    const Vec3& com) {
    using namespace JPH;
    despawn();
    if (!impl_->settings) build();
    PhysicsSystem& sys = physics().system();
    impl_->ragdoll = impl_->settings->CreateRagdoll(impl_->group++, 0, &sys);
    Mat44 mats[RP_Count];
    for (int i = 0; i < RP_Count; ++i) {
        Transform jt = jointModel.size() > size_t(kParts[i].joint) ? jointModel[size_t(kParts[i].joint)] : Transform();
        if (jointModel.size() <= size_t(kParts[i].joint)) jt.position = riderJoints()[kParts[i].joint].restWorld;
        Vec3 p = modelToWorld.transformPoint(jt.position);
        Quat r = (modelToWorld.rotation * jt.rotation).normalized();
        mats[i] = Mat44::sRotationTranslation(JPH::Quat(r.x, r.y, r.z, r.w), J(p));
    }
    impl_->ragdoll->SetPose(RVec3::sZero(), mats);
    impl_->ragdoll->AddToPhysicsSystem(EActivation::Activate);
    BodyInterface& bi = sys.GetBodyInterface();
    for (int i = 0; i < RP_Count; ++i) {
        BodyID id = impl_->ragdoll->GetBodyID(i);
        Vec3 p = S(JPH::Vec3(bi.GetPosition(id)));
        Vec3 v = linVel + cross(angVel, p - com);
        bi.SetLinearAndAngularVelocity(id, J(v), J(angVel * 0.6f));
    }
    active_ = true;
    settled_ = 0.0f;
}

void Ragdoll::spawnScooter(const Vec3& pos, const Quat& rot, const Vec3& linVel, const Vec3& angVel) {
    if (scooterBody_ != kNoBody) physics().removeBody(scooterBody_);
    BodyDesc d;
    ShapeDesc deck;
    deck.kind = ShapeKind::Box;
    deck.halfExtents = Vec3(0.06f, 0.035f, 0.3f);
    deck.position = Vec3(0, 0.02f, 0);
    d.shapes.push_back(deck);
    ShapeDesc bars;
    bars.kind = ShapeKind::Box;
    bars.halfExtents = Vec3(0.28f, 0.02f, 0.02f);
    ScooterDims dims;
    bars.position = dims.barCenter();
    d.shapes.push_back(bars);
    ShapeDesc stem;
    stem.kind = ShapeKind::Box;
    stem.halfExtents = Vec3(0.02f, 0.46f, 0.02f);
    stem.position = (dims.frontAxle() + dims.barCenter()) * 0.5f;
    stem.rotation = Quat::fromTo(Vec3(0, 1, 0), dims.steerAxis());
    d.shapes.push_back(stem);
    for (const Vec3& ax : {dims.frontAxle(), dims.rearAxle()}) {
        ShapeDesc w;
        w.kind = ShapeKind::Sphere;
        w.radius = dims.wheelRadius;
        w.position = ax;
        d.shapes.push_back(w);
    }
    d.position = pos;
    d.rotation = rot;
    d.mass = 4.5f;
    d.layer = PhysLayer::Debris;
    d.friction = 0.6f;
    d.restitution = 0.25f;
    d.continuous = true;
    d.angularDamping = 0.2f;
    scooterBody_ = physics().createBody(d);
    physics().setLinearVelocity(scooterBody_, linVel);
    physics().setAngularVelocity(scooterBody_, angVel);
}

void Ragdoll::despawn() {
    if (impl_->ragdoll) {
        impl_->ragdoll->RemoveFromPhysicsSystem();
        impl_->ragdoll = nullptr;
    }
    if (scooterBody_ != kNoBody) physics().removeBody(scooterBody_);
    scooterBody_ = kNoBody;
    active_ = false;
}

Transform Ragdoll::partTransform(int part) const {
    Transform t;
    if (!impl_->ragdoll) return t;
    JPH::BodyID id = impl_->ragdoll->GetBodyID(part);
    const JPH::BodyInterface& bi = physics().system().GetBodyInterface();
    t.position = S(JPH::Vec3(bi.GetPosition(id)));
    t.rotation = S(bi.GetRotation(id));
    return t;
}

Transform Ragdoll::scooterTransform() const {
    Transform t;
    if (scooterBody_ == kNoBody) return t;
    t.position = physics().position(scooterBody_);
    t.rotation = physics().rotation(scooterBody_);
    return t;
}

Vec3 Ragdoll::centre() const { return impl_->ragdoll ? partTransform(RP_Pelvis).position : Vec3(0); }

Vec3 Ragdoll::velocity() const {
    if (!impl_->ragdoll) return Vec3(0);
    const JPH::BodyInterface& bi = physics().system().GetBodyInterface();
    return S(bi.GetLinearVelocity(impl_->ragdoll->GetBodyID(RP_Pelvis)));
}

void Ragdoll::update(float dt) {
    if (!active_) return;
    if (velocity().length() < 0.4f)
        settled_ += dt;
    else
        settled_ = 0.0f;
}

}  // namespace sw
