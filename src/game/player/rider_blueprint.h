// scoot would - rider skeleton blueprint (single source of truth for the rider model generator,
// the animation system and the ragdoll). Rest pose: standing, facing -Z, feet at y = 0.
#pragma once

#include "core/math.h"

namespace sw {

enum RiderJoint : int {
    RJ_Root = 0,
    RJ_Pelvis,
    RJ_Spine,
    RJ_Chest,
    RJ_Neck,
    RJ_Head,
    RJ_UpperArmL,
    RJ_LowerArmL,
    RJ_HandL,
    RJ_UpperArmR,
    RJ_LowerArmR,
    RJ_HandR,
    RJ_ThighL,
    RJ_ShinL,
    RJ_FootL,
    RJ_ThighR,
    RJ_ShinR,
    RJ_FootR,
    RJ_Count
};

struct RiderJointDef {
    const char* name;
    int parent;
    Vec3 restWorld;  // rest position (model space)
};

// left side = -X (the rider faces -Z)
inline const RiderJointDef* riderJoints() {
    static const RiderJointDef defs[RJ_Count] = {
        // measured from the MakeHuman based rider (tools/assetgen/human_gen.cpp, 1.77 m)
        {"root", -1, {0.0f, 0.0f, 0.0f}},
        {"pelvis", RJ_Root, {0.0f, 0.959f, 0.0f}},
        {"spine", RJ_Pelvis, {0.0f, 1.053f, -0.034f}},
        {"chest", RJ_Spine, {0.0f, 1.181f, -0.036f}},
        {"neck", RJ_Chest, {0.0f, 1.525f, -0.074f}},
        {"head", RJ_Neck, {0.0f, 1.622f, -0.107f}},
        {"upperarm_l", RJ_Chest, {-0.205f, 1.422f, -0.081f}},
        {"lowerarm_l", RJ_UpperArmL, {-0.259f, 1.169f, -0.108f}},
        {"hand_l", RJ_LowerArmL, {-0.302f, 0.91f, -0.162f}},
        {"upperarm_r", RJ_Chest, {0.205f, 1.422f, -0.081f}},
        {"lowerarm_r", RJ_UpperArmR, {0.259f, 1.169f, -0.108f}},
        {"hand_r", RJ_LowerArmR, {0.302f, 0.91f, -0.162f}},
        {"thigh_l", RJ_Pelvis, {-0.108f, 0.962f, -0.055f}},
        {"shin_l", RJ_ThighL, {-0.108f, 0.521f, -0.082f}},
        {"foot_l", RJ_ShinL, {-0.108f, 0.071f, -0.034f}},
        {"thigh_r", RJ_Pelvis, {0.108f, 0.962f, -0.055f}},
        {"shin_r", RJ_ThighR, {0.108f, 0.521f, -0.082f}},
        {"foot_r", RJ_ShinR, {0.108f, 0.071f, -0.034f}},
    };
    return defs;
}

// scooter geometry shared by the model generator, the rider IK and the physics
struct ScooterDims {
    float wheelRadius = 0.055f;
    float wheelBase = 0.56f;
    float deckLength = 0.5f;
    float deckWidth = 0.12f;
    float deckTop = 0.058f;      // above the axles
    float barHeight = 0.86f;     // grip height above the deck top
    float barWidth = 0.56f;
    float headTubeAngle = 83.0f; // degrees from horizontal
    // front axle (local, body space: origin between axles at axle height, -Z forward)
    Vec3 frontAxle() const { return {0, 0, -wheelBase * 0.5f}; }
    Vec3 rearAxle() const { return {0, 0, wheelBase * 0.5f}; }
    // steer axis passes through the front axle, tilted back
    Vec3 steerAxis() const {
        float a = headTubeAngle * kDeg2Rad;
        return Vec3(0, std::sin(a), std::cos(a)).normalized();
    }
    Vec3 barCenter() const { return frontAxle() + steerAxis() * ((deckTop + barHeight) / steerAxis().y); }
    // hand centres on the grips (the crossbar sweeps back ~1.6 cm and up ~0.6 cm towards the ends)
    Vec3 gripL() const { return barCenter() + Vec3(-barWidth * 0.5f + 0.06f, 0.0055f, 0.016f); }
    Vec3 gripR() const { return barCenter() + Vec3(barWidth * 0.5f - 0.06f, 0.0055f, 0.016f); }
    // feet on the deck: front foot forward, back foot behind (regular stance, left foot front)
    Vec3 frontFoot() const { return {-0.03f, deckTop, -0.1f}; }
    Vec3 backFoot() const { return {0.03f, deckTop, 0.13f}; }
};

}  // namespace sw
