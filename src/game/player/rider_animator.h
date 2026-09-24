// scoot would - rider animation: state machine clips + procedural layers + two bone IK
#pragma once

#include "animation/anim_state_machine.h"
#include "animation/animation.h"

#include <string>
#include <vector>

namespace sw {

// IK targets in rider model space (rider stands at the origin, facing -Z)
struct RiderRig {
    Vec3 gripL, gripR;
    Vec3 footFront, footBack;  // sole contact points on the deck
    bool feetOff = false, handsOff = false, oneHand = false;
};

struct RiderAnimParams {
    int state = 0;           // PlayerState
    float crouch = 0.0f;
    float lean = 0.0f;
    float speed = 0.0f;
    bool pushing = false;
    float pushPhase = 0.0f;
    std::string trickPose;
    float trickWeight = 0.0f;
    bool manualNose = false;
    int grindType = 0;
    float airTime = 0.0f;
    float landAge = 10.0f;
    float landImpact = 0.0f;
    float steer = 0.0f;
    float lookYaw = 0.0f;    // radians, head look relative to the body
    bool goofy = false;      // right foot forward: clips (authored regular) are mirrored
};

class RiderAnimator {
public:
    // skeleton + clips from the rider glTF (clips may be empty: procedural only)
    void init(SkeletonPtr skel, const std::vector<AnimationClipPtr>& clips);
    void update(float dt, const RiderAnimParams& p, const RiderRig& rig);
    const Pose& pose() const { return pose_; }
    const Skeleton& skeleton() const { return *skel_; }
    SkeletonPtr skeletonPtr() const { return skel_; }
    void modelSpace(std::vector<Transform>& out) const { pose_.modelSpace(*skel_, out); }
    void skinMatrices(std::vector<Mat4>& out) const;
    int joint(const std::string& name) const { return skel_ ? skel_->find(name) : -1; }
    const std::string& currentState() const { return sm_.current(); }

private:
    void applyIK(const RiderRig& rig, float dt, bool goofy);
    void mirrorPose(Pose& p) const;
    void setModelRotation(int j, const Quat& modelRot);

    SkeletonPtr skel_;
    AnimStateMachine sm_;
    Pose pose_, overlay_;
    int pelvis_ = -1, spine_ = -1, chest_ = -1, neck_ = -1, head_ = -1;
    int armL_[3] = {-1, -1, -1}, armR_[3] = {-1, -1, -1}, legL_[3] = {-1, -1, -1}, legR_[3] = {-1, -1, -1};
    float feetW_ = 1.0f, handLW_ = 1.0f, handRW_ = 1.0f, backFootW_ = 1.0f;
    float crouchS_ = 0.0f, lookS_ = 0.0f, leanS_ = 0.0f;
    std::vector<int> mirror_;  // left <-> right joint partner (self for centre joints)
    std::string lastTrickPose_;
    float trickW_ = 0.0f;
};

}  // namespace sw
