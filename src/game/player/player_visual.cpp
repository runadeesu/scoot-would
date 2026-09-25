#include "game/player/player_visual.h"
#include "assets/asset_manager.h"
#include "core/filesystem.h"
#include "core/log.h"
#include "game/player/player.h"
#include "game/player/ragdoll.h"
#include "game/player/rider_animator.h"
#include "game/player/scooter_model.h"
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
    ScooterModelOptions opt;
    opt.deck = custom_.deck;
    opt.bars = custom_.bars;
    opt.wheels = custom_.wheels;
    opt.clamp = custom_.clamp;
    ScooterMeshSet m = buildScooterModel(kDims, opt);
    barHeight_ = m.barHeight;
    barWidth_ = m.barWidth;
    parts_[Deck] = createGpuMesh(m.deck, false);
    parts_[Grip] = createGpuMesh(m.grip, false);
    parts_[Brake] = createGpuMesh(m.brake, false);
    parts_[Fork] = createGpuMesh(m.fork, false);
    parts_[Bars] = createGpuMesh(m.bars, false);
    parts_[Grips] = createGpuMesh(m.grips, false);
    parts_[Clamp] = createGpuMesh(m.clamp, false);
    parts_[TyreF] = createGpuMesh(m.tyre, false);
    parts_[TyreR] = parts_[TyreF];
    parts_[CoreF] = createGpuMesh(m.core, false);
    parts_[CoreR] = parts_[CoreF];
}

void PlayerVisual::create(RenderScene& rs) {
    destroy();
    rs_ = &rs;
    buildScooterMeshes();
    MaterialPtr hw = assets().material("scooter_hardware");
    partMats_[Deck] = {assets().material("scooter_deck"), hw, assets().material("scooter_headset")};
    partMats_[Grip] = {assets().material("scooter_griptape"), assets().material("scooter_deck")};  // logo cut-out
    partMats_[Brake] = {assets().material("scooter_brake")};
    partMats_[Fork] = {assets().material("scooter_fork"), hw, assets().material("scooter_headset")};
    partMats_[Bars] = {assets().material("scooter_bars"), assets().material("scooter_barend")};
    partMats_[Grips] = {assets().material("scooter_grips")};
    partMats_[Clamp] = {assets().material("scooter_clamp"), hw};
    partMats_[TyreF] = {assets().material("scooter_wheel")};
    partMats_[TyreR] = partMats_[TyreF];
    partMats_[CoreF] = {assets().material("scooter_core"), assets().material("scooter_bearing"), hw};
    partMats_[CoreR] = partMats_[CoreF];
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
    if (n == "hair") return true;  // short crop: shows under the helmet at the back and sides
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
    bool rebuild = c.deck != custom_.deck || c.bars != custom_.bars || c.wheels != custom_.wheels || c.clamp != custom_.clamp;
    custom_ = c;
    if (!rs_) return;
    if (rebuild) {
        buildScooterMeshes();
        for (int i = 0; i < PartCount; ++i) rs_->setMesh(partHandles_[i], parts_[i]);
    }
    // alpha 0: only the tintable (painted / anodised) slot of a part takes the colour, hardware,
    // bearings and headset keep their own finish
    auto tint = [&](int part, const Vec3& col) { rs_->setTint(partHandles_[part], Vec4(col, 0.0f)); };
    tint(Deck, c.deckColor);
    tint(Grip, c.deckColor);  // griptape is not tintable, the logo cut-out shows the deck
    tint(Brake, Vec3(1));
    tint(Fork, c.barsColor);
    tint(Bars, c.barsColor);
    tint(Grips, c.gripColor);
    tint(Clamp, c.clampColor);
    tint(TyreF, c.wheelColor);
    tint(TyreR, c.wheelColor);
    tint(CoreF, c.coreColor);
    tint(CoreR, c.coreColor);
    for (size_t i = 0; i < riderHandles_.size(); ++i) {
        RenderObject* o = rs_->get(riderHandles_[i]);
        if (!o) continue;
        bool vis = riderPartVisible(riderMeshVariant_[i], c);
        o->visible = vis && visible_ && riderVisible_ && !display_;
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
        if (display_ || !riderVisible_) o->visible = false;
    }
}

void PlayerVisual::setDisplay(bool on, const Transform& where) {
    display_ = on;
    displayXf_ = where;
    if (!rs_) return;
    for (auto h : mannequin_) rs_->setVisible(h, on ? false : (riderVisible_ && visible_));
    applyCustomization(custom_);
}

void PlayerVisual::setRiderVisible(bool v) {
    riderVisible_ = v;
    if (!rs_) return;
    for (auto h : mannequin_) rs_->setVisible(h, v && visible_);
    applyCustomization(custom_);
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
    rs_->setTransform(partHandles_[Brake], deckM);
    rs_->setTransform(partHandles_[Fork], barsM);
    rs_->setTransform(partHandles_[Bars], barsM);
    rs_->setTransform(partHandles_[Grips], barsM);
    rs_->setTransform(partHandles_[Clamp], barsM);
    Mat4 wf = barsM * Mat4::translation(fa) * wheelSpin, wr = deckM * Mat4::translation(d.rearAxle()) * wheelSpin;
    rs_->setTransform(partHandles_[TyreF], wf);
    rs_->setTransform(partHandles_[CoreF], wf);
    rs_->setTransform(partHandles_[TyreR], wr);
    rs_->setTransform(partHandles_[CoreR], wr);
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
    ap.goofy = goofy_;
    ap.footDown = !bailed && player.stoppedTime() > 0.45f && !ap.pushing;
    RiderRig rig;
    ScooterDims bars = d;  // hands on the grips of the fitted bars
    bars.barHeight = barHeight_;
    bars.barWidth = barWidth_;
    rig.gripL = bars.gripL() - modelToBody.position;
    rig.gripR = bars.gripR() - modelToBody.position;
    rig.footFront = d.frontFoot() - modelToBody.position;
    rig.footBack = d.backFoot() - modelToBody.position;
    if (player.state() == PlayerState::Air) {
        rig.feetOff = tp.feetOff;
        rig.handsOff = tp.handsOff;
        rig.oneHand = tp.oneHand;
    }
    animator_->update(dt, ap, rig);
    animator_->modelSpace(jointsModel_);
    if (bindPose_) {
        Pose bind;
        bind.setBind(animator_->skeleton());
        bind.modelSpace(animator_->skeleton(), jointsModel_);
    }
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
    if (display_) {
        // product display: straight bars, wheels still, no trick pose
        const ScooterDims& d = kDims;
        Mat4 root = displayXf_.matrix();
        Vec3 fa = d.frontAxle();
        rs_->setTransform(partHandles_[Deck], root);
        rs_->setTransform(partHandles_[Grip], root);
        rs_->setTransform(partHandles_[Brake], root);
        for (int p : {int(Fork), int(Bars), int(Grips), int(Clamp)}) rs_->setTransform(partHandles_[p], root);
        rs_->setTransform(partHandles_[TyreF], root * Mat4::translation(fa));
        rs_->setTransform(partHandles_[CoreF], root * Mat4::translation(fa));
        rs_->setTransform(partHandles_[TyreR], root * Mat4::translation(d.rearAxle()));
        rs_->setTransform(partHandles_[CoreR], root * Mat4::translation(d.rearAxle()));
        (void)dt;
        (void)alpha;
        (void)player;
        return;
    }
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
