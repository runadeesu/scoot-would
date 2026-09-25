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
    bool feetOff = false, handsOff = false, oneHand = false, frontFootOff = false, backFootOff = false;
    // the scooter as solid shapes (tricks): legs that are off the deck are kept out of them
    bool collide = false;
    Vec3 deckCenter, deckHalf{0.062f, 0.02f, 0.235f};
    Quat deckRot;
    Vec3 stemA, stemB;      // fork crown -> bar centre
    Vec3 wheelF, wheelB;    // axles
    // hands off a spinning bar hover over where the grips are when it is straight; the back hand can hold the deck
    bool handsHover = false, handDeck = false;
    Vec3 hoverL, hoverR, deckHand;
};

struct RiderAnimParams {
    int state = 0;           // PlayerState
    float crouch = 0.0f;
    float lean = 0.0f;
    float speed = 0.0f;
    bool pushing = false;
    float pushPhase = 0.0f;   // 0..1 over one push cycle
    float pushCycle = 0.62f;  // seconds; the foot is on the ground between pushPlant and pushLift (fractions)
    float pushPlant = 0.26f, pushLift = 0.58f;
    std::string trickPose;
    float trickWeight = 0.0f;
    float trickTime = 0.5f;  // position in the trick clip (0..1): kick, tuck, catch
    float spinRate = 0.0f;   // body rotation in the air (rad/s): + turns left
    float flipRate = 0.0f;   // + backwards
    bool manualNose = false;
    int grindType = 0;
    float airTime = 0.0f;
    float landAge = 10.0f;
    float landImpact = 0.0f;
    float steer = 0.0f;
    float lookYaw = 0.0f;    // radians, head look relative to the body
    bool goofy = false;      // right foot forward: clips (authored regular) are mirrored
    bool footDown = false;   // stopped: the back foot comes off the deck and stands on the ground
    bool firstPerson = false;  // first person camera: taller stance, head behind the bars (they sit in view)
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
    // how far the hands that hold the bars fell short of the grips (rider model space): the scooter hangs from
    // the hands, so the caller moves it by this much
    Vec3 gripShortfall() const { return gripShort_; }

private:
    void applyIK(const RiderRig& rig, float dt, bool goofy);
    void keepLegsClear(const RiderRig& rig, int* leg, float sideSign, float plantedW);
    void mirrorPose(Pose& p) const;
    void applyFingers(float dt);
    void applyFace(float dt, float speed);
    Quat gripRotation(int side) const;
    void setModelRotation(int j, const Quat& modelRot);

    SkeletonPtr skel_;
    AnimStateMachine sm_;
    Pose pose_, overlay_;
    int pelvis_ = -1, spine_ = -1, chest_ = -1, neck_ = -1, head_ = -1;
    int armL_[3] = {-1, -1, -1}, armR_[3] = {-1, -1, -1}, legL_[3] = {-1, -1, -1}, legR_[3] = {-1, -1, -1};
    float feetW_ = 1.0f, handLW_ = 1.0f, handRW_ = 1.0f, backFootW_ = 1.0f;
    float hoverW_ = 0.0f, deckHandW_ = 0.0f;
    float crouchS_ = 0.0f, lookS_ = 0.0f, leanS_ = 0.0f;
    float pushW_ = 0.0f, fpW_ = 0.0f, frontOffW_ = 0.0f, backOffW_ = 0.0f;
    float spinLead_ = 0.0f, flipLead_ = 0.0f;
    Vec3 pushFoot_;     // pushing foot sole target (rider model space, regular stance)
    float pushToe_ = 0.0f;  // heel lift at the end of the drive
    float footDownW_ = 0.0f, groundFoot_ = 0.0f;  // 0 = back foot on the tail, 1 = standing on the ground next to the deck
    std::vector<int> mirror_;  // left <-> right joint partner (self for centre joints)
    // hands: rest frame (finger direction, palm normal) + finger joints with their bend axes
    struct Finger {
        int joint[3] = {-1, -1, -1};
        Vec3 axis;       // rest space bend axis (curl towards the palm)
        bool thumb = false;
    };
    Finger fingers_[2][5];
    Vec3 restFingerDir_[2], restPalm_[2];
    bool handRig_ = false;
    float gripCurl_[2] = {1.0f, 1.0f};
    // face
    int eye_[2] = {-1, -1}, lidUp_[2] = {-1, -1}, lidLo_[2] = {-1, -1}, jaw_ = -1, clav_[2] = {-1, -1};
    float time_ = 0.0f, blinkT_ = 0.0f, nextBlink_ = 2.5f, saccadeT_ = 0.0f;
    Vec2 eyeLook_, eyeTarget_;
    uint32_t rng_ = 12345u;
    std::string lastTrickPose_;
    float trickW_ = 0.0f;
    Vec3 gripShort_;
};

}  // namespace sw
