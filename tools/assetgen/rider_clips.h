// scoot would asset tools - rider animation clips (shared by the rider generators)
//   joint euler angles in degrees (Ry * Rx * Rz) relative to the identity rest pose, pelvis offset
//   in metres from its rest position. Authored for the regular stance (left foot forward); goofy is
//   mirrored at runtime by the rider animator.
#pragma once

#include "core/math.h"
#include "game/player/rider_blueprint.h"

#include <string>
#include <utility>
#include <vector>

namespace sw::tools {

struct PoseDef {
    Vec3 e[RJ_Count];
    Vec3 pelvis;
    PoseDef& set(int j, float x, float y = 0.0f, float z = 0.0f) {
        e[j] = Vec3(x, y, z);
        return *this;
    }
    // mirrored pair (left gets -y, -z)
    PoseDef& pair(int jl, int jr, float x, float y = 0.0f, float z = 0.0f) {
        e[jl] = Vec3(x, -y, -z);
        e[jr] = Vec3(x, y, z);
        return *this;
    }
    PoseDef& add(int j, float x, float y = 0.0f, float z = 0.0f) {
        e[j] += Vec3(x, y, z);
        return *this;
    }
    PoseDef& hips(float x, float y, float z) {
        pelvis = Vec3(x, y, z);
        return *this;
    }
};

struct ClipDef {
    std::string name;
    std::vector<std::pair<float, PoseDef>> keys;
};

PoseDef ridePose();
PoseDef tuckPose();
std::vector<ClipDef> clipDefs();
void smoothClip(ClipDef& cd);  // cubic in-betweens baked into the keys
Quat eulerDeg(const Vec3& e);

}  // namespace sw::tools
