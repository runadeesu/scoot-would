#include "game/player/player_visual.h"
#include "assets/asset_manager.h"
#include "core/filesystem.h"
#include "core/log.h"
#include "game/player/player.h"
#include "game/player/ragdoll.h"
#include "game/player/rider_animator.h"
#include "render/mesh_builder.h"

#include <cstdlib>
#include <cstring>

namespace sw {

namespace {
ScooterDims kDims;

std::shared_ptr<GpuMesh> upload(MeshBuilder& b) {
    MeshData md = b.build();
    return createGpuMesh(md, false);
}

SkeletonPtr blueprintSkeleton() {
    auto s = std::make_shared<Skeleton>();
    const RiderJointDef* d = riderJoints();
    for (int i = 0; i < RJ_Count; ++i) {
        Joint j;
        j.name = d[i].name;
        j.parent = d[i].parent;
        Vec3 parentPos = d[i].parent >= 0 ? d[d[i].parent].restWorld : Vec3(0);
        j.bindLocal.position = d[i].restWorld - parentPos;
        j.inverseBind = Mat4::translation(-d[i].restWorld);
        s->byName[j.name] = i;
        s->joints.push_back(j);
    }
    return s;
}
}  // namespace

PlayerVisual::PlayerVisual() {
    for (auto& h : partHandles_) h = RenderScene::kInvalid;
}
PlayerVisual::~PlayerVisual() = default;

void PlayerVisual::buildScooterMeshes() {
    const ScooterDims& d = kDims;
    Vec3 steer = d.steerAxis();
    Vec3 fa = d.frontAxle(), ra = d.rearAxle();
    // deck (+ head tube, dropout)
    {
        MeshBuilder b("scooter_deck");
        b.setMaterial(0);
        float len = d.deckLength, w = d.deckWidth, h = 0.042f;
        float deckY = d.deckTop - h * 0.5f;
        float deckCz = 0.03f;
        if (custom_.deck == 1) w = 0.135f;
        if (custom_.deck == 2) len = 0.53f;
        // main deck body with a bevel via two boxes
        b.box(Vec3(0, deckY, deckCz), Vec3(w, h, len));
        b.box(Vec3(0, deckY - h * 0.5f - 0.004f, deckCz), Vec3(w * 0.8f, 0.008f, len * 0.96f));
        // neck up to the head tube
        Vec3 neckA(0, deckY, deckCz - len * 0.5f + 0.02f);
        Vec3 headBottom = fa + steer * 0.13f;
        b.squareTube({neckA, headBottom + Vec3(0, -0.01f, 0.03f)}, w * 0.28f, 0.022f, true);
        // head tube
        b.tube({fa + steer * 0.11f, fa + steer * 0.25f}, 0.026f, 16, true);
        // rear dropouts + brake fender
        for (float s : {-1.0f, 1.0f}) b.box(Vec3(s * (0.045f), 0.01f, ra.z - 0.02f), Vec3(0.012f, 0.05f, 0.13f));
        b.box(Vec3(0, ra.y + d.wheelRadius + 0.012f, ra.z + 0.01f), Vec3(0.06f, 0.006f, 0.12f));
        parts_[Deck] = upload(b);
    }
    {
        MeshBuilder b("scooter_grip");
        float len = d.deckLength * 0.93f, w = d.deckWidth * 0.94f;
        float y = d.deckTop + 0.0015f;
        b.quad(Vec3(-w * 0.5f, y, 0.03f + len * 0.5f), Vec3(w * 0.5f, y, 0.03f + len * 0.5f), Vec3(w * 0.5f, y, 0.03f - len * 0.5f),
               Vec3(-w * 0.5f, y, 0.03f - len * 0.5f));
        parts_[Grip] = upload(b);
    }
    // fork (moves with the bars)
    {
        MeshBuilder b("scooter_fork");
        Vec3 side(1, 0, 0);
        Vec3 crown = fa + steer * 0.105f;
        b.box(crown, Vec3(0.1f, 0.025f, 0.05f));
        for (float s : {-1.0f, 1.0f}) b.squareTube({crown + side * (s * 0.042f), fa + side * (s * 0.042f)}, 0.008f, 0.016f, true);
        b.tube({fa + steer * 0.25f, fa + steer * 0.31f}, 0.02f, 12, true);  // steerer / compression
        parts_[Fork] = upload(b);
    }
    // bars
    float barH = custom_.bars == 1 ? 0.92f : 0.86f;
    float barW = custom_.bars == 2 ? 0.62f : 0.56f;
    {
        MeshBuilder b("scooter_bars");
        Vec3 bottom = fa + steer * 0.27f;
        Vec3 top = fa + steer * ((d.deckTop + barH) / steer.y);
        b.tube({bottom, top}, 0.0175f, 14, true);
        b.tube({top + Vec3(-barW * 0.5f, 0, 0), top + Vec3(barW * 0.5f, 0, 0)}, 0.0165f, 14, true);
        // gussets
        for (float s : {-1.0f, 1.0f}) b.tube({top + Vec3(s * 0.12f, 0, 0), top - steer * 0.12f}, 0.008f, 8, false);
        parts_[Bars] = upload(b);
        Vec3 gl = top + Vec3(-barW * 0.5f, 0, 0), gr = top + Vec3(barW * 0.5f, 0, 0);
        MeshBuilder g("scooter_grips");
        g.tube({gl + Vec3(0.005f, 0, 0), gl + Vec3(0.13f, 0, 0)}, 0.0205f, 14, true);
        g.tube({gr - Vec3(0.13f, 0, 0), gr - Vec3(0.005f, 0, 0)}, 0.0205f, 14, true);
        parts_[Grips] = upload(g);
        MeshBuilder c("scooter_clamp");
        c.tube({bottom - steer * 0.01f, bottom + steer * 0.06f}, 0.03f, 16, true);
        for (float s : {-1.0f, 1.0f}) c.box(bottom + steer * 0.025f + Vec3(0, 0, 0.035f) + Vec3(s * 0.012f, 0, 0), Vec3(0.008f, 0.03f, 0.03f));
        parts_[Clamp] = upload(c);
    }
    // wheels: tyre (slot 0) + core (slot 1), axis along X, centred at the origin
    for (int wi = 0; wi < 2; ++wi) {
        MeshBuilder b(wi == 0 ? "wheel_f" : "wheel_r");
        float r = d.wheelRadius, width = 0.024f;
        Mat4 rot = Mat4::rotation(Quat::angleAxis(-kHalfPi, Vec3(0, 0, 1)));  // lathe Y axis -> X
        b.setTransform(rot);
        b.setMaterial(0);
        std::vector<Vec2> tyre;
        int n = 10;
        for (int i = 0; i <= n; ++i) {
            float a = -kHalfPi + kPi * float(i) / float(n);
            tyre.push_back(Vec2(r - 0.012f + std::cos(a) * 0.012f, std::sin(a) * width * 0.5f));
        }
        b.lathe(tyre, 24, true);
        b.setMaterial(1);
        float coreR = r - 0.013f;
        int spokes = custom_.wheels == 1 ? 12 : custom_.wheels == 2 ? 0 : 6;
        b.lathe({{0.015f, -0.013f}, {coreR, -0.011f}}, 24, false, true);
        b.lathe({{coreR, 0.011f}, {0.015f, 0.013f}}, 24, false, true);
        b.cylinder(Vec3(0, -0.02f, 0), 0.012f, 0.04f, 10, true);
        if (spokes == 0) {
            b.lathe({{coreR, -0.011f}, {coreR, 0.011f}}, 24, false, true);
        }
        b.resetTransform();
        parts_[wi == 0 ? WheelF : WheelR] = upload(b);
    }
}

void PlayerVisual::create(RenderScene& rs) {
    destroy();
    rs_ = &rs;
    buildScooterMeshes();
    partMats_[Deck] = {assets().material("scooter_deck")};
    partMats_[Grip] = {assets().material("scooter_griptape")};
    partMats_[Fork] = {assets().material("scooter_fork")};
    partMats_[Bars] = {assets().material("scooter_bars")};
    partMats_[Grips] = {assets().material("scooter_grips")};
    partMats_[Clamp] = {assets().material("scooter_clamp")};
    partMats_[WheelF] = {assets().material("scooter_wheel"), assets().material("scooter_core")};
    partMats_[WheelR] = partMats_[WheelF];
    for (int i = 0; i < PartCount; ++i) {
        RenderObject o;
        o.mesh = parts_[i];
        o.materials = partMats_[i];
        o.isStatic = false;
        o.layer = LayerPlayer;
        o.lodBias = 3.0f;
        partHandles_[i] = rs.add(o);
    }

    // rider: skinned glTF model if available, otherwise the procedural mannequin
    animator_ = std::make_unique<RiderAnimator>();
    if (fs::exists(fs::resolve("assets/models/rider.glb"))) model_ = assets().model("assets/models/rider.glb");
    if (model_ && model_->skeleton && !model_->meshes.empty()) {
        animator_->init(model_->skeleton, model_->clips);
        boneOffset_ = rs.allocateBones(model_->skeleton->size());
        for (auto& mm : model_->meshes) {
            RenderObject o;
            o.mesh = mm.gpu;
            o.materials = mm.materials;
            o.isStatic = false;
            o.layer = LayerPlayer;
            o.boneOffset = boneOffset_;
            o.lodBias = 3.0f;
            riderHandles_.push_back(rs.add(o));
            riderMeshVariant_.push_back(mm.name);
        }
        LOG_INFO("player: rider model with %zu joints, %zu clips", model_->skeleton->size(), model_->clips.size());
    } else {
        model_.reset();
        animator_->init(blueprintSkeleton(), {});
        buildMannequin();
    }
    applyCustomization(custom_);
}

// clothing variants by mesh name (see tools/assetgen/rider_gen.cpp):
//   top_N / pants_N / shoes_N / helmet_N select a variant; bare skin under garments is only
//   present where the garment leaves it visible
bool riderPartVisible(const std::string& n, const Customization& c) {
    auto variant = [&](const char* prefix, int value, bool& match) {
        size_t l = std::strlen(prefix);
        if (n.compare(0, l, prefix) != 0 || n.size() <= l) return false;
        match = std::atoi(n.c_str() + l) == value;
        return true;
    };
    bool m = true;
    if (variant("top_", c.top, m) || variant("pants_", c.pants, m) || variant("shoes_", c.shoes, m) || variant("helmet_", c.helmet, m)) return m;
    if (n == "arms_skin") return c.top != 1;
    if (n == "legs_skin") return c.pants == 1;
    if (n == "hair") return c.helmet != 1;
    return true;
}

void PlayerVisual::buildMannequin() {
    const RiderJointDef* d = riderJoints();
    // one capsule per bone, in the parent joint's rest frame
    struct Seg {
        int joint, child;
        float r;
        const char* mat;
    };
    const Seg segs[] = {{RJ_Pelvis, RJ_Spine, 0.13f, "rider_pants"},  {RJ_Spine, RJ_Chest, 0.14f, "rider_top"},
                        {RJ_Chest, RJ_Neck, 0.15f, "rider_top"},      {RJ_Neck, RJ_Head, 0.05f, "rider_skin"},
                        {RJ_UpperArmL, RJ_LowerArmL, 0.05f, "rider_top"}, {RJ_LowerArmL, RJ_HandL, 0.04f, "rider_skin"},
                        {RJ_UpperArmR, RJ_LowerArmR, 0.05f, "rider_top"}, {RJ_LowerArmR, RJ_HandR, 0.04f, "rider_skin"},
                        {RJ_ThighL, RJ_ShinL, 0.07f, "rider_pants"},  {RJ_ShinL, RJ_FootL, 0.055f, "rider_pants"},
                        {RJ_ThighR, RJ_ShinR, 0.07f, "rider_pants"},  {RJ_ShinR, RJ_FootR, 0.055f, "rider_pants"}};
    for (const Seg& s : segs) {
        MeshBuilder b("mannequin");
        Vec3 a(0), c = d[s.child].restWorld - d[s.joint].restWorld;
        b.capsule(a + c.normalized() * s.r * 0.6f, c - c.normalized() * s.r * 0.6f, s.r, 8, 12);
        RenderObject o;
        o.mesh = upload(b);
        o.materials = {assets().material(s.mat)};
        o.isStatic = false;
        o.layer = LayerPlayer;
        mannequin_.push_back(rs_->add(o));
        mannequinBones_.push_back({s.joint, s.child});
    }
    // head, hands, feet
    auto blob = [&](int joint, Vec3 off, Vec3 size, const char* mat, bool sphere) {
        MeshBuilder b("mannequin");
        if (sphere)
            b.sphere(off, size.x, 10, 14);
        else
            b.box(off, size);
        RenderObject o;
        o.mesh = upload(b);
        o.materials = {assets().material(mat)};
        o.isStatic = false;
        o.layer = LayerPlayer;
        mannequin_.push_back(rs_->add(o));
        mannequinBones_.push_back({joint, -1});
    };
    blob(RJ_Head, Vec3(0, 0.1f, 0), Vec3(0.11f), "rider_skin", true);
    blob(RJ_Head, Vec3(0, 0.15f, 0.005f), Vec3(0.125f), "rider_helmet", true);
    blob(RJ_HandL, Vec3(0, -0.05f, 0), Vec3(0.05f, 0.1f, 0.08f), "rider_skin", false);
    blob(RJ_HandR, Vec3(0, -0.05f, 0), Vec3(0.05f, 0.1f, 0.08f), "rider_skin", false);
    blob(RJ_FootL, Vec3(0, -0.05f, -0.06f), Vec3(0.1f, 0.08f, 0.27f), "rider_shoes", false);
    blob(RJ_FootR, Vec3(0, -0.05f, -0.06f), Vec3(0.1f, 0.08f, 0.27f), "rider_shoes", false);
}

void PlayerVisual::destroy() {
    if (!rs_) return;
    for (auto& h : partHandles_) {
        if (h != RenderScene::kInvalid) rs_->remove(h);
        h = RenderScene::kInvalid;
    }
    for (auto h : riderHandles_) rs_->remove(h);
    for (auto h : mannequin_) rs_->remove(h);
    riderHandles_.clear();
    mannequin_.clear();
    mannequinBones_.clear();
    riderMeshVariant_.clear();
    model_.reset();
    rs_ = nullptr;
}

void PlayerVisual::applyCustomization(const Customization& c) {
    bool rebuild = c.deck != custom_.deck || c.bars != custom_.bars || c.wheels != custom_.wheels;
    custom_ = c;
    if (!rs_) return;
    if (rebuild) {
        buildScooterMeshes();
        for (int i = 0; i < PartCount; ++i) rs_->setMesh(partHandles_[i], parts_[i]);
    }
    auto tint = [&](int part, const Vec3& col) { rs_->setTint(partHandles_[part], Vec4(col, 1.0f)); };
    tint(Deck, c.deckColor);
    tint(Grip, Vec3(1));
    tint(Fork, c.barsColor);
    tint(Bars, c.barsColor);
    tint(Grips, c.gripColor);
    tint(Clamp, c.clampColor);
    tint(WheelF, c.wheelColor);
    tint(WheelR, c.wheelColor);
    // the wheel core colour is carried in the tint alpha channel? No: cores use a second tintable slot -> same tint
    for (size_t i = 0; i < riderHandles_.size(); ++i) {
        RenderObject* o = rs_->get(riderHandles_[i]);
        if (!o) continue;
        bool vis = riderPartVisible(riderMeshVariant_[i], c);
        o->visible = vis && visible_;
    }
    // rider material tints: materials are tintable, the instance tint carries the colour
    if (model_) {
        for (size_t i = 0; i < riderHandles_.size(); ++i) {
            const auto& mm = model_->meshes[i];
            Vec3 col(1);
            std::string mat = mm.materials.empty() ? "" : mm.materials[0]->name;
            if (mat.find("skin") != std::string::npos) col = c.skinColor;
            else if (mat.find("top") != std::string::npos) col = c.topColor;
            else if (mat.find("pants") != std::string::npos) col = c.pantsColor;
            else if (mat.find("shoe") != std::string::npos) col = c.shoesColor;
            else if (mat.find("helmet") != std::string::npos) col = c.helmetColor;
            // alpha 0: only tintable materials take the colour (eyes, soles, straps keep theirs)
            rs_->setTint(riderHandles_[i], Vec4(col, 0.0f));
        }
    }
    for (size_t i = 0; i < mannequin_.size(); ++i) {
        RenderObject* o = rs_->get(mannequin_[i]);
        if (!o || o->materials.empty()) continue;
        const std::string& n = o->materials[0]->name;
        Vec3 col = n.find("skin") != std::string::npos ? c.skinColor : n.find("top") != std::string::npos ? c.topColor
                   : n.find("pants") != std::string::npos ? c.pantsColor : n.find("shoes") != std::string::npos ? c.shoesColor : c.helmetColor;
        rs_->setTint(mannequin_[i], Vec4(col, 1.0f));
        if (n.find("helmet") != std::string::npos) o->visible = c.helmet != 0 && visible_;
    }
}

void PlayerVisual::setVisible(bool v) {
    visible_ = v;
    if (!rs_) return;
    for (auto h : partHandles_) rs_->setVisible(h, v);
    for (auto h : mannequin_) rs_->setVisible(h, v);
    applyCustomization(custom_);
}

Vec3 PlayerVisual::headPosition() const {
    if (jointsModel_.size() > size_t(RJ_Head)) return riderWorld_.transformPoint(jointsModel_[RJ_Head].position);
    return riderWorld_.position + Vec3(0, 1.7f, 0);
}

void PlayerVisual::updateScooterParts(const Transform& body, const Player& player, bool bailed) {
    const ScooterDims& d = kDims;
    Transform root = body;
    if (bailed) root = player.ragdoll.scooterTransform();
    ScooterPose pose;
    if (!bailed && player.state() == PlayerState::Air) pose = player.tricks.pose();
    Vec3 steer = d.steerAxis();
    Vec3 fa = d.frontAxle();
    // whole scooter flips pivot around the deck centre, pushed away from the rider (kickless)
    Mat4 whole = Mat4::identity();
    if (std::fabs(pose.wholePitch) > 1e-4f || std::fabs(pose.wholeYaw) > 1e-4f || pose.scooterAway > 0.0f) {
        Vec3 pivot(0, 0.35f, -0.05f);
        Mat4 r = Mat4::rotation(Quat::angleAxis(pose.wholeYaw, Vec3(0, 1, 0)) * Quat::angleAxis(pose.wholePitch, Vec3(1, 0, 0)));
        whole = Mat4::translation(Vec3(0, -0.25f * pose.scooterAway, -0.15f * pose.scooterAway)) * Mat4::translation(pivot) * r * Mat4::translation(-pivot);
    }
    Mat4 rootM = root.matrix() * whole;
    // deck: whip around the steer axis through the front axle, roll / pitch around its own centre
    Mat4 deckM = rootM * Mat4::translation(fa) * Mat4::rotation(Quat::angleAxis(pose.deckSteer, steer)) * Mat4::translation(-fa);
    Vec3 dc(0, d.deckTop * 0.5f, 0.03f);
    if (std::fabs(pose.deckRoll) > 1e-4f || std::fabs(pose.deckPitch) > 1e-4f) {
        // fingerwhip / bri flip rotate the deck around the head tube line / lateral axis
        Vec3 pivot = (pose.deckRoll != 0.0f) ? fa + steer * 0.2f : dc;
        Vec3 axis = pose.deckRoll != 0.0f ? (dc - pivot).normalized() : Vec3(1, 0, 0);
        float ang = pose.deckRoll != 0.0f ? pose.deckRoll : pose.deckPitch;
        deckM = deckM * Mat4::translation(pivot) * Mat4::rotation(Quat::angleAxis(ang, axis)) * Mat4::translation(-pivot);
    }
    float steerAngle = bailed ? 0.0f : player.scooter.steerAngle();
    barSteer_ = steerAngle;
    Mat4 barsM = rootM * Mat4::translation(fa) * Mat4::rotation(Quat::angleAxis(pose.bars - steerAngle * 0.8f, steer)) * Mat4::translation(-fa);
    Mat4 wheelSpin = Mat4::rotation(Quat::angleAxis(-wheelSpin_, Vec3(1, 0, 0)));
    rs_->setTransform(partHandles_[Deck], deckM);
    rs_->setTransform(partHandles_[Grip], deckM);
    rs_->setTransform(partHandles_[Fork], barsM);
    rs_->setTransform(partHandles_[Bars], barsM);
    rs_->setTransform(partHandles_[Grips], barsM);
    rs_->setTransform(partHandles_[Clamp], barsM);
    rs_->setTransform(partHandles_[WheelF], barsM * Mat4::translation(fa) * wheelSpin);
    rs_->setTransform(partHandles_[WheelR], deckM * Mat4::translation(d.rearAxle()) * wheelSpin);
}

void PlayerVisual::updateRider(const Transform& body, Player& player, float dt, bool bailed) {
    const ScooterDims& d = kDims;
    // rider model origin: on the deck, between the feet
    Transform modelToBody;
    modelToBody.position = Vec3(0, d.deckTop, 0.01f);
    riderWorld_ = body * modelToBody;
    RiderAnimParams ap;
    ap.state = int(player.state());
    ap.crouch = player.crouch();
    ap.lean = bailed ? 0.0f : player.scooter.lean();
    ap.speed = player.speed();
    ap.pushing = !bailed && player.scooter.pushing();
    ap.pushPhase = bailed ? 0.0f : player.scooter.pushPhase();
    const ScooterPose& tp = player.tricks.pose();
    ap.trickPose = tp.riderPose;
    ap.trickWeight = player.state() == PlayerState::Air ? tp.riderPoseWeight : 0.0f;
    ap.manualNose = player.manual.nose;
    ap.grindType = int(player.grind.type);
    ap.airTime = player.airTime();
    ap.landAge = player.lastLandingAge();
    ap.landImpact = player.lastLanding().impact;
    ap.steer = bailed ? 0.0f : player.scooter.steerAngle() / 0.5f;
    RiderRig rig;
    rig.gripL = d.gripL() - modelToBody.position;
    rig.gripR = d.gripR() - modelToBody.position;
    rig.footFront = d.frontFoot() - modelToBody.position;
    rig.footBack = d.backFoot() - modelToBody.position;
    if (player.state() == PlayerState::Air) {
        rig.feetOff = tp.feetOff;
        rig.handsOff = tp.handsOff;
        rig.oneHand = tp.oneHand;
    }
    animator_->update(dt, ap, rig);
    animator_->modelSpace(jointsModel_);
    // publish the pose for the ragdoll (bail starts from the animated pose)
    std::vector<Transform> jointsBp(RJ_Count);
    const RiderJointDef* rj = riderJoints();
    for (int i = 0; i < RJ_Count; ++i) {
        int j = animator_->joint(rj[i].name);
        if (j >= 0 && size_t(j) < jointsModel_.size()) {
            Transform t = jointsModel_[size_t(j)];
            t.position += modelToBody.position;  // blueprint model space = body space offset by the deck
            jointsBp[size_t(i)] = t;
        }
    }
    player.riderPose = jointsBp;

    if (bailed && player.ragdoll.active()) {
        // drive the joints from the ragdoll bodies
        std::vector<Transform> world(jointsModel_.size());
        Transform inv = riderWorld_.inverse();
        const Skeleton& sk = animator_->skeleton();
        std::vector<Transform> ms = jointsModel_;
        for (int part = 0; part < RP_Count; ++part) {
            int bj = ragdollPartJoint(part);
            int j = animator_->joint(rj[bj].name);
            if (j < 0) continue;
            Transform pt = player.ragdoll.partTransform(part);
            // ragdoll parts rest with identity rotation in blueprint space -> rider model space
            ms[size_t(j)].position = pt.position;
            ms[size_t(j)].rotation = pt.rotation;
        }
        // express in the pelvis-anchored model frame: pelvis part defines the rider world transform
        Transform pel = player.ragdoll.partTransform(RP_Pelvis);
        Vec3 restPelvis = rj[RJ_Pelvis].restWorld;
        riderWorld_.rotation = pel.rotation;
        riderWorld_.position = pel.position - pel.rotation * restPelvis;
        inv = riderWorld_.inverse();
        for (size_t j = 0; j < ms.size(); ++j) {
            bool driven = false;
            for (int part = 0; part < RP_Count; ++part)
                if (animator_->joint(rj[ragdollPartJoint(part)].name) == int(j)) driven = true;
            if (driven) {
                ms[j].position = inv.transformPoint(ms[j].position);
                ms[j].rotation = (inv.rotation * ms[j].rotation).normalized();
            } else {
                // follow the parent rigidly (rest offset)
                int p = sk.joints[j].parent;
                if (p >= 0) {
                    ms[j].rotation = ms[size_t(p)].rotation;
                    ms[j].position = ms[size_t(p)].position + ms[size_t(p)].rotation * sk.joints[j].bindLocal.position;
                }
            }
        }
        jointsModel_ = ms;
    }

    if (model_) {
        std::vector<Mat4>& bones = rs_->bones();
        const Skeleton& sk = animator_->skeleton();
        for (size_t j = 0; j < jointsModel_.size() && boneOffset_ >= 0; ++j)
            bones[size_t(boneOffset_) + j] = jointsModel_[j].matrix() * sk.joints[j].inverseBind;
        for (auto h : riderHandles_) rs_->setTransform(h, riderWorld_.matrix());
    } else {
        for (size_t i = 0; i < mannequin_.size(); ++i) {
            int j = mannequinBones_[i].first;
            if (size_t(j) >= jointsModel_.size()) continue;
            Transform t = riderWorld_ * jointsModel_[size_t(j)];
            rs_->setTransform(mannequin_[i], t.matrix());
        }
    }
}

void PlayerVisual::update(float dt, float alpha, Player& player) {
    if (!rs_) return;
    bool bailed = player.state() == PlayerState::Bailed;
    Transform body = player.renderTransform(alpha);
    // visual carve lean around the travel direction
    if (!bailed) {
        float lean = player.scooter.lean();
        body.rotation = (body.rotation * Quat::angleAxis(-lean, Vec3(0, 0, -1))).normalized();
        float fs = player.scooter.forwardSpeed();
        wheelSpin_ += fs * dt / kDims.wheelRadius;
    }
    updateScooterParts(body, player, bailed);
    updateRider(body, player, dt, bailed);
}

}  // namespace sw
