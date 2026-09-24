#include "game/player/rider_animator.h"
#include "game/player/player.h"

namespace sw {

void RiderAnimator::init(SkeletonPtr skel, const std::vector<AnimationClipPtr>& clips) {
    skel_ = skel;
    sm_.setSkeleton(skel_.get());
    for (const auto& c : clips) {
        bool loop = c->name == "idle" || c->name == "ride" || c->name == "air" || c->name == "manual" || c->name == "nose_manual" ||
                    c->name == "grind" || c->name == "boardslide" || c->name == "crouch" || c->name == "push";
        sm_.addState(c->name, c, loop, 1.0f);
    }
    pose_.setBind(*skel_);
    pelvis_ = joint("pelvis");
    spine_ = joint("spine");
    chest_ = joint("chest");
    neck_ = joint("neck");
    head_ = joint("head");
    const char* al[3] = {"upperarm_l", "lowerarm_l", "hand_l"};
    const char* ar[3] = {"upperarm_r", "lowerarm_r", "hand_r"};
    const char* ll[3] = {"thigh_l", "shin_l", "foot_l"};
    const char* lr[3] = {"thigh_r", "shin_r", "foot_r"};
    for (int i = 0; i < 3; ++i) {
        armL_[i] = joint(al[i]);
        armR_[i] = joint(ar[i]);
        legL_[i] = joint(ll[i]);
        legR_[i] = joint(lr[i]);
    }
    sm_.play(sm_.has("ride") ? "ride" : "idle", 0.0f);
}

void RiderAnimator::skinMatrices(std::vector<Mat4>& out) const {
    std::vector<Mat4> ms;
    pose_.modelSpace(*skel_, ms);
    out.resize(ms.size());
    for (size_t i = 0; i < ms.size(); ++i) out[i] = ms[i] * skel_->joints[i].inverseBind;
}

void RiderAnimator::setModelRotation(int j, const Quat& modelRot) {
    if (j < 0) return;
    std::vector<Transform> ms;
    pose_.modelSpace(*skel_, ms);
    int p = skel_->joints[size_t(j)].parent;
    Quat parent = p >= 0 ? ms[size_t(p)].rotation : Quat::identity();
    pose_.local[size_t(j)].rotation = (parent.conjugate() * modelRot).normalized();
}

void RiderAnimator::update(float dt, const RiderAnimParams& p, const RiderRig& rig) {
    PlayerState st = PlayerState(p.state);
    // base state selection
    std::string want = "ride";
    float fade = 0.2f;
    switch (st) {
        case PlayerState::Riding:
            if (p.landAge < 0.3f && p.landImpact > 6.5f) want = "hard_land";
            else if (p.landAge < 0.22f) want = "land";
            else if (p.pushing) want = "push";
            else if (p.speed < 0.25f) want = "idle";
            else want = "ride";
            break;
        case PlayerState::Air: want = "air"; fade = 0.15f; break;
        case PlayerState::Manual: want = p.manualNose ? "nose_manual" : "manual"; break;
        case PlayerState::Grinding: want = p.grindType == 6 ? "boardslide" : "grind"; fade = 0.1f; break;
        case PlayerState::Bailed: want = "bail"; fade = 0.1f; break;
    }
    if (!sm_.has(want)) want = sm_.has("ride") ? "ride" : "idle";
    sm_.play(want, fade, want == "push" && sm_.current() != "push");
    sm_.update(dt);
    sm_.evaluate(pose_);

    // crouch layer (blend towards the crouch clip)
    crouchS_ = dampf(crouchS_, p.crouch, 18.0f, dt);
    float landComp = p.landAge < 0.35f ? std::sin(p.landAge / 0.35f * kPi) * saturate(p.landImpact / 8.0f) * 0.8f : 0.0f;
    float crouchW = std::max(crouchS_, landComp);
    if (st == PlayerState::Air) crouchW = std::max(crouchW, 0.35f);
    if (crouchW > 0.01f && sm_.has("crouch")) {
        sm_.sampleState("crouch", 0.0f, overlay_);
        blendPoses(pose_, overlay_, crouchW);
    } else if (crouchW > 0.01f && pelvis_ >= 0) {
        pose_.local[size_t(pelvis_)].position.y -= 0.26f * crouchW;
    }

    // trick pose overlay (grabs, whips, bar tricks)
    if (!p.trickPose.empty()) lastTrickPose_ = p.trickPose;
    trickW_ = dampf(trickW_, p.trickWeight, 16.0f, dt);
    if (trickW_ > 0.01f && sm_.has(lastTrickPose_)) {
        sm_.sampleState(lastTrickPose_, 0.0f, overlay_);
        blendPoses(pose_, overlay_, trickW_);
    }

    // carving: upper body leans into the turn, spine twists slightly with steering
    leanS_ = dampf(leanS_, p.lean, 8.0f, dt);
    if (spine_ >= 0) {
        Quat& r = pose_.local[size_t(spine_)].rotation;
        r = (Quat::angleAxis(-leanS_ * 0.35f, Vec3(0, 0, -1)) * Quat::angleAxis(-p.steer * 0.12f, Vec3(0, 1, 0)) * r).normalized();
    }
    // head follows the look direction a little
    lookS_ = dampf(lookS_, clampf(p.lookYaw, -0.8f, 0.8f), 5.0f, dt);
    if (neck_ >= 0) pose_.local[size_t(neck_)].rotation = (Quat::angleAxis(lookS_ * 0.35f, Vec3(0, 1, 0)) * pose_.local[size_t(neck_)].rotation).normalized();
    if (head_ >= 0) pose_.local[size_t(head_)].rotation = (Quat::angleAxis(lookS_ * 0.45f, Vec3(0, 1, 0)) * pose_.local[size_t(head_)].rotation).normalized();

    // IK weights: feet / hands leave the scooter during tricks, the back foot during a push
    bool bailed = st == PlayerState::Bailed;
    float feetTarget = (rig.feetOff || bailed) ? 0.0f : 1.0f;
    float handLTarget = (rig.handsOff || bailed) ? 0.0f : 1.0f;
    float handRTarget = (rig.handsOff || rig.oneHand || bailed) ? 0.0f : 1.0f;
    feetW_ = dampf(feetW_, feetTarget, 20.0f, dt);
    handLW_ = dampf(handLW_, handLTarget, 22.0f, dt);
    handRW_ = dampf(handRW_, handRTarget, 22.0f, dt);
    float pushLeg = 0.0f;
    if (p.pushing && sm_.current() == "push") pushLeg = std::sin(saturate(sm_.currentNormalizedTime()) * kPi);
    backFootW_ = dampf(backFootW_, 1.0f - pushLeg, 25.0f, dt);
    applyIK(rig, dt);
}

void RiderAnimator::applyIK(const RiderRig& rig, float) {
    // legs: ankle joint sits ~8.5 cm above the sole
    Vec3 ankleOff(0, 0.085f, 0);
    auto leg = [&](int* ch, const Vec3& sole, float w, float sideSign) {
        if (ch[0] < 0 || ch[1] < 0 || ch[2] < 0 || w <= 0.001f) return;
        std::vector<Transform> ms;
        pose_.modelSpace(*skel_, ms);
        Vec3 hip = ms[size_t(ch[0])].position;
        Vec3 pole = hip + Vec3(sideSign * 0.08f, -0.35f, -0.6f);
        solveTwoBoneIK(*skel_, pose_, ch[0], ch[1], ch[2], sole + ankleOff, pole, w);
        // keep the sole flat on the deck
        Quat footRot = Quat::angleAxis(sideSign * 0.2f, Vec3(0, 1, 0));
        std::vector<Transform> ms2;
        pose_.modelSpace(*skel_, ms2);
        Quat cur = ms2[size_t(ch[2])].rotation;
        setModelRotation(ch[2], nlerp(cur, footRot, w));
    };
    // regular stance: left foot front
    leg(legL_, rig.footFront, feetW_, -1.0f);
    leg(legR_, rig.footBack, feetW_ * backFootW_, 1.0f);
    auto arm = [&](int* ch, const Vec3& grip, float w, float sideSign) {
        if (ch[0] < 0 || ch[1] < 0 || ch[2] < 0 || w <= 0.001f) return;
        std::vector<Transform> ms;
        pose_.modelSpace(*skel_, ms);
        Vec3 shoulder = ms[size_t(ch[0])].position;
        Vec3 pole = shoulder + Vec3(sideSign * 0.5f, -0.25f, 0.35f);
        Vec3 wrist = grip + Vec3(0, 0.035f, 0.03f);
        solveTwoBoneIK(*skel_, pose_, ch[0], ch[1], ch[2], wrist, pole, w);
        std::vector<Transform> ms2;
        pose_.modelSpace(*skel_, ms2);
        Quat cur = ms2[size_t(ch[2])].rotation;
        Quat gripRot = Quat::angleAxis(-sideSign * 0.3f, Vec3(0, 0, 1)) * Quat::angleAxis(0.5f, Vec3(1, 0, 0));
        setModelRotation(ch[2], nlerp(cur, gripRot, w));
    };
    arm(armL_, rig.gripL, handLW_, -1.0f);
    arm(armR_, rig.gripR, handRW_, 1.0f);
}

}  // namespace sw
