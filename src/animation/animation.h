// scoot would - skeletal animation data: skeleton, poses, clips, blending, two bone IK
#pragma once

#include "core/math.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sw {

struct Joint {
    std::string name;
    int parent = -1;
    Transform bindLocal;  // rest pose local transform
    Mat4 inverseBind;     // model space -> joint space at bind
};

struct Skeleton {
    std::vector<Joint> joints;  // parents always precede children
    std::unordered_map<std::string, int> byName;
    int find(const std::string& name) const {
        auto it = byName.find(name);
        return it == byName.end() ? -1 : it->second;
    }
    size_t size() const { return joints.size(); }
};
using SkeletonPtr = std::shared_ptr<Skeleton>;

struct Pose {
    std::vector<Transform> local;
    void resize(size_t n) { local.resize(n); }
    void setBind(const Skeleton& s) {
        local.resize(s.size());
        for (size_t i = 0; i < s.size(); ++i) local[i] = s.joints[i].bindLocal;
    }
    // compute model space transforms
    void modelSpace(const Skeleton& s, std::vector<Mat4>& out) const;
    void modelSpace(const Skeleton& s, std::vector<Transform>& out) const;
};

enum class AnimPath { Translation, Rotation, Scale };
enum class AnimInterp { Linear, Step };

struct AnimChannel {
    int joint = -1;
    AnimPath path = AnimPath::Rotation;
    AnimInterp interp = AnimInterp::Linear;
    std::vector<float> times;
    std::vector<Vec4> values;  // xyz for T/S, xyzw quaternion for R
};

struct AnimationClip {
    std::string name;
    float duration = 0.0f;
    bool loop = true;
    std::vector<AnimChannel> channels;
    // overwrite channels present in the clip, other joints keep the pose values
    void sample(float time, Pose& pose) const;
};
using AnimationClipPtr = std::shared_ptr<AnimationClip>;

// blend b into a by weight t (a = lerp(a, b, t)), optional per joint mask
void blendPoses(Pose& a, const Pose& b, float t, const std::vector<float>* mask = nullptr);
// additive: a += (b - ref) * t
void addPose(Pose& a, const Pose& b, const Pose& ref, float t);

// analytic two bone IK in model space. Rotates root and mid joints (local pose) so that the
// end joint reaches target. poleHint is a model space position the knee/elbow bends toward.
bool solveTwoBoneIK(const Skeleton& s, Pose& pose, int root, int mid, int end, const Vec3& target, const Vec3& poleHint,
                    float weight = 1.0f);

}  // namespace sw
