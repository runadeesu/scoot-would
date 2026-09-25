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
    // mirror partners by name: "*_l" <-> "*_r"
    mirror_.resize(skel_->size());
    for (size_t i = 0; i < skel_->size(); ++i) {
        const std::string& n = skel_->joints[i].name;
        mirror_[i] = int(i);
        if (n.size() > 2 && n[n.size() - 2] == '_' && (n.back() == 'l' || n.back() == 'r')) {
            std::string other = n.substr(0, n.size() - 1) + (n.back() == 'l' ? "r" : "l");
            int j = joint(other);
            if (j >= 0) mirror_[i] = j;
        }
    }
    sm_.play(sm_.has("ride") ? "ride" : "idle", 0.0f);

    // hand rig from the rest pose (identity rest orientations: rest space = local space)
    std::vector<Transform> rest;
    pose_.modelSpace(*skel_, rest);
    auto rp = [&](const std::string& n) { int j = joint(n); return j >= 0 ? rest[size_t(j)].position : Vec3(0); };
    handRig_ = joint("middle1_l") >= 0 && joint("index1_r") >= 0;
    const char* fnames[5] = {"thumb", "index", "middle", "ring", "pinky"};
    for (int s = 0; s < 2 && handRig_; ++s) {
        std::string sfx = s == 0 ? "_l" : "_r";
        Vec3 hand = rp("hand" + sfx), idx = rp("index1" + sfx), pky = rp("pinky1" + sfx), mid = rp("middle1" + sfx), mid3 = rp("middle3" + sfx);
        Vec3 fdir = (mid3 - hand).normalized();
        Vec3 palm = cross(idx - pky, fdir).normalized();
        if (dot(palm, Vec3(-hand.x, 0, 0)) < 0.0f) palm = -palm;  // at rest the palms face the thighs
        restFingerDir_[s] = fdir;
        restPalm_[s] = (palm - fdir * dot(palm, fdir)).normalized();
        (void)mid;
        for (int f = 0; f < 5; ++f) {
            Finger& F = fingers_[s][f];
            F.thumb = f == 0;
            for (int k = 0; k < 3; ++k) F.joint[k] = joint(std::string(fnames[f]) + std::to_string(k + 1) + sfx);
            Vec3 a = rp(std::string(fnames[f]) + "1" + sfx), b = rp(std::string(fnames[f]) + "3" + sfx);
            Vec3 d = (b - a).normalized();
            Vec3 axis = cross(d, restPalm_[s]).normalized();
            // sign: a positive rotation must move the finger tip towards the palm
            Vec3 moved = Quat::angleAxis(0.3f, axis) * (b - a);
            if (dot(moved - (b - a), restPalm_[s]) < 0.0f) axis = -axis;
            if (F.thumb) axis = (axis + d * 0.35f).normalized();  // thumb folds across the palm
            F.axis = axis;
        }
    }
    const char* sides[2] = {"_l", "_r"};
    for (int s = 0; s < 2; ++s) {
        eye_[s] = joint(std::string("eye") + sides[s]);
        lidUp_[s] = joint(std::string("lid_upper") + sides[s]);
        lidLo_[s] = joint(std::string("lid_lower") + sides[s]);
        clav_[s] = joint(std::string("clavicle") + sides[s]);
    }
    jaw_ = joint("jaw");
}

// hand orientation for an overhand grip: fingers point forward and a little down over the grip, the
// palm rests on top of it facing down / forward
Quat RiderAnimator::gripRotation(int s) const {
    Vec3 a = restFingerDir_[s], b = restPalm_[s];
    Vec3 A = Vec3(0.0f, -0.42f, -1.0f).normalized();
    Vec3 B = Vec3(0.0f, -1.0f, 0.3f);
    B = (B - A * dot(B, A)).normalized();
    // basis change rest (a, b, a x b) -> target (A, B, A x B)
    Vec3 c = cross(a, b), C = cross(A, B);
    Mat4 R = Mat4::identity(), S = Mat4::identity();
    auto setCols = [](Mat4& m, Vec3 x, Vec3 y, Vec3 z) {
        m.m[0] = x.x; m.m[1] = x.y; m.m[2] = x.z;
        m.m[4] = y.x; m.m[5] = y.y; m.m[6] = y.z;
        m.m[8] = z.x; m.m[9] = z.y; m.m[10] = z.z;
    };
    setCols(R, A, B, C);
    setCols(S, a, b, c);
    Mat4 M = R * S.transposed();
    return Quat::fromMat(M).normalized();
}

void RiderAnimator::applyFingers(float dt) {
    if (!handRig_) return;
    for (int s = 0; s < 2; ++s) {
        float want = s == 0 ? handLW_ : handRW_;  // on the grip: full curl; off: relaxed half curl
        gripCurl_[s] = dampf(gripCurl_[s], want, 14.0f, dt);
        float g = gripCurl_[s];
        for (int f = 0; f < 5; ++f) {
            const Finger& F = fingers_[s][f];
            // degrees per joint: relaxed .. wrapped around a 32 mm grip
            const float relaxed[3] = {F.thumb ? 8.0f : 10.0f, F.thumb ? 8.0f : 14.0f, F.thumb ? 6.0f : 8.0f};
            const float grip[3] = {F.thumb ? 26.0f : 62.0f + float(f) * 3.0f, F.thumb ? 24.0f : 72.0f, F.thumb ? 18.0f : 44.0f};
            for (int k = 0; k < 3; ++k) {
                int j = F.joint[k];
                if (j < 0) continue;
                float ang = lerpf(relaxed[k], grip[k], g) * kDeg2Rad;
                pose_.local[size_t(j)].rotation = (Quat::angleAxis(ang, F.axis) * pose_.local[size_t(j)].rotation).normalized();
            }
        }
    }
}

void RiderAnimator::applyFace(float dt, float speed) {
    time_ += dt;
    auto rnd = [&]() {
        rng_ = rng_ * 1664525u + 1013904223u;
        return float((rng_ >> 8) & 0xFFFF) / 65535.0f;
    };
    // blinks every 2-6 s (quicker when riding fast), 0.14 s close/open
    blinkT_ += dt;
    if (blinkT_ > nextBlink_) {
        blinkT_ = 0.0f;
        nextBlink_ = lerpf(2.0f, 6.0f, rnd()) * (speed > 6.0f ? 0.7f : 1.0f);
    }
    float blink = blinkT_ < 0.16f ? std::sin(blinkT_ / 0.16f * kPi) : 0.0f;
    // eyes: small saccades around the look direction
    saccadeT_ -= dt;
    if (saccadeT_ <= 0.0f) {
        saccadeT_ = lerpf(0.4f, 1.8f, rnd());
        eyeTarget_ = Vec2((rnd() - 0.5f) * 0.22f, (rnd() - 0.5f) * 0.1f - 0.05f);
    }
    eyeLook_ = Vec2(dampf(eyeLook_.x, eyeTarget_.x, 25.0f, dt), dampf(eyeLook_.y, eyeTarget_.y, 25.0f, dt));
    for (int s = 0; s < 2; ++s) {
        if (eye_[s] >= 0)
            pose_.local[size_t(eye_[s])].rotation =
                (Quat::angleAxis(eyeLook_.x, Vec3(0, 1, 0)) * Quat::angleAxis(eyeLook_.y, Vec3(1, 0, 0)) * pose_.local[size_t(eye_[s])].rotation).normalized();
        // upper lid follows the eye's pitch a little and closes on a blink
        if (lidUp_[s] >= 0)
            pose_.local[size_t(lidUp_[s])].rotation =
                (Quat::angleAxis(blink * 0.62f + eyeLook_.y * 0.4f, Vec3(1, 0, 0)) * pose_.local[size_t(lidUp_[s])].rotation).normalized();
        if (lidLo_[s] >= 0)
            pose_.local[size_t(lidLo_[s])].rotation = (Quat::angleAxis(-blink * 0.12f, Vec3(1, 0, 0)) * pose_.local[size_t(lidLo_[s])].rotation).normalized();
        // breathing lifts the shoulders slightly
        if (clav_[s] >= 0) {
            float br = std::sin(time_ * kTwoPi / 3.6f) * 0.012f;
            pose_.local[size_t(clav_[s])].rotation = (Quat::angleAxis(br * (s == 0 ? 1.0f : -1.0f), Vec3(0, 0, 1)) * pose_.local[size_t(clav_[s])].rotation).normalized();
        }
    }
    if (chest_ >= 0) {
        float br = std::sin(time_ * kTwoPi / 3.6f) * 0.01f;
        pose_.local[size_t(chest_)].rotation = (Quat::angleAxis(-br, Vec3(1, 0, 0)) * pose_.local[size_t(chest_)].rotation).normalized();
    }
    if (jaw_ >= 0) pose_.local[size_t(jaw_)].rotation = (Quat::angleAxis(0.02f + 0.01f * std::sin(time_ * 0.7f), Vec3(1, 0, 0)) * pose_.local[size_t(jaw_)].rotation).normalized();
}

// mirror across the rider's sagittal plane (x -> -x): partners swap, rotations about x keep their
// sign, rotations about y / z flip. Rest orientations are identity and the rest pose is symmetric.
void RiderAnimator::mirrorPose(Pose& p) const {
    std::vector<Transform> src = p.local;
    for (size_t i = 0; i < src.size() && i < mirror_.size(); ++i) {
        const Transform& o = src[size_t(mirror_[i])];
        Transform t = o;
        t.position = Vec3(-o.position.x, o.position.y, o.position.z);
        t.rotation = Quat(o.rotation.x, -o.rotation.y, -o.rotation.z, o.rotation.w);
        p.local[i] = t;
    }
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

    // goofy riders: the regular stance clips are mirrored (right foot forward, left foot pushes)
    if (p.goofy) mirrorPose(pose_);

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
    applyFace(dt, p.speed);
    applyFingers(dt);
    applyIK(rig, dt, p.goofy);
}

void RiderAnimator::applyIK(const RiderRig& rig, float, bool goofy) {
    // legs: ankle joint sits ~8.5 cm above the sole
    Vec3 ankleOff(0, 0.085f, 0);
    // front knee points forward and a little out, the back knee drops in towards the front leg
    auto leg = [&](int* ch, const Vec3& sole, float w, float sideSign, bool back) {
        if (ch[0] < 0 || ch[1] < 0 || ch[2] < 0 || w <= 0.001f) return;
        std::vector<Transform> ms;
        pose_.modelSpace(*skel_, ms);
        Vec3 hip = ms[size_t(ch[0])].position;
        Vec3 pole = hip + Vec3(sideSign * (back ? -0.22f : 0.08f), -0.35f, -0.6f);
        solveTwoBoneIK(*skel_, pose_, ch[0], ch[1], ch[2], sole + ankleOff, pole, w);
        // keep the sole flat on the deck: front foot points forward, back foot turned out on the tail
        Quat footRot = Quat::angleAxis(sideSign * (back ? 0.55f : 0.12f), Vec3(0, 1, 0));
        std::vector<Transform> ms2;
        pose_.modelSpace(*skel_, ms2);
        Quat cur = ms2[size_t(ch[2])].rotation;
        setModelRotation(ch[2], nlerp(cur, footRot, w));
    };
    // regular stance: left foot front; goofy: mirrored targets, right foot front
    if (!goofy) {
        leg(legL_, rig.footFront, feetW_, -1.0f, false);
        leg(legR_, rig.footBack, feetW_ * backFootW_, 1.0f, true);
    } else {
        auto m = [](Vec3 v) { return Vec3(-v.x, v.y, v.z); };
        leg(legR_, m(rig.footFront), feetW_, 1.0f, false);
        leg(legL_, m(rig.footBack), feetW_ * backFootW_, -1.0f, true);
    }
    auto arm = [&](int* ch, const Vec3& grip, float w, float sideSign) {
        if (ch[0] < 0 || ch[1] < 0 || ch[2] < 0 || w <= 0.001f) return;
        std::vector<Transform> ms;
        pose_.modelSpace(*skel_, ms);
        Vec3 shoulder = ms[size_t(ch[0])].position;
        Vec3 pole = shoulder + Vec3(sideSign * 0.5f, -0.25f, 0.35f);
        int s = sideSign < 0.0f ? 0 : 1;
        Quat gripRot = Quat::angleAxis(-sideSign * 0.3f, Vec3(0, 0, 1)) * Quat::angleAxis(0.5f, Vec3(1, 0, 0));
        Vec3 wrist = grip + Vec3(0, 0.035f, 0.03f);
        if (handRig_) {
            gripRot = gripRotation(s);
            // palm centre over the grip: wrist sits behind the knuckles and above the bar
            Vec3 fwd = gripRot * restFingerDir_[s], palm = gripRot * restPalm_[s];
            wrist = grip - fwd * 0.062f - palm * 0.03f;
        }
        solveTwoBoneIK(*skel_, pose_, ch[0], ch[1], ch[2], wrist, pole, w);
        std::vector<Transform> ms2;
        pose_.modelSpace(*skel_, ms2);
        Quat cur = ms2[size_t(ch[2])].rotation;
        setModelRotation(ch[2], nlerp(cur, gripRot, w));
    };
    arm(armL_, rig.gripL, handLW_, -1.0f);
    arm(armR_, rig.gripR, handRW_, 1.0f);
}

}  // namespace sw
