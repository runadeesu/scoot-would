#include "animation/animation.h"

#include <algorithm>

namespace sw {

void Pose::modelSpace(const Skeleton& s, std::vector<Mat4>& out) const {
    out.resize(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        Mat4 m = local[i].matrix();
        int p = s.joints[i].parent;
        out[i] = p >= 0 ? out[size_t(p)] * m : m;
    }
}

void Pose::modelSpace(const Skeleton& s, std::vector<Transform>& out) const {
    out.resize(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        int p = s.joints[i].parent;
        out[i] = p >= 0 ? out[size_t(p)] * local[i] : local[i];
    }
}

static size_t findKey(const std::vector<float>& times, float t) {
    auto it = std::upper_bound(times.begin(), times.end(), t);
    if (it == times.begin()) return 0;
    return size_t(it - times.begin()) - 1;
}

void AnimationClip::sample(float time, Pose& pose) const {
    float t = time;
    if (duration > 0.0f) {
        if (loop) {
            t = std::fmod(time, duration);
            if (t < 0) t += duration;
        } else {
            t = clampf(time, 0.0f, duration);
        }
    }
    for (const AnimChannel& ch : channels) {
        if (ch.joint < 0 || size_t(ch.joint) >= pose.local.size() || ch.times.empty()) continue;
        size_t k = findKey(ch.times, t);
        size_t k1 = std::min(k + 1, ch.times.size() - 1);
        float f = 0.0f;
        if (k1 != k && ch.interp == AnimInterp::Linear) f = saturate((t - ch.times[k]) / (ch.times[k1] - ch.times[k]));
        const Vec4& a = ch.values[k];
        const Vec4& b = ch.values[k1];
        Transform& tr = pose.local[size_t(ch.joint)];
        switch (ch.path) {
            case AnimPath::Translation: tr.position = lerp(a.xyz(), b.xyz(), f); break;
            case AnimPath::Scale: tr.scale = lerp(a.xyz(), b.xyz(), f); break;
            case AnimPath::Rotation: tr.rotation = nlerp(Quat(a.x, a.y, a.z, a.w), Quat(b.x, b.y, b.z, b.w), f); break;
        }
    }
}

void blendPoses(Pose& a, const Pose& b, float t, const std::vector<float>* mask) {
    size_t n = std::min(a.local.size(), b.local.size());
    for (size_t i = 0; i < n; ++i) {
        float w = t * (mask && i < mask->size() ? (*mask)[i] : 1.0f);
        if (w <= 0.0f) continue;
        a.local[i].position = lerp(a.local[i].position, b.local[i].position, w);
        a.local[i].rotation = nlerp(a.local[i].rotation, b.local[i].rotation, w);
        a.local[i].scale = lerp(a.local[i].scale, b.local[i].scale, w);
    }
}

void addPose(Pose& a, const Pose& b, const Pose& ref, float t) {
    size_t n = std::min({a.local.size(), b.local.size(), ref.local.size()});
    for (size_t i = 0; i < n; ++i) {
        Quat delta = b.local[i].rotation * ref.local[i].rotation.conjugate();
        Quat scaled = nlerp(Quat::identity(), delta, t);
        a.local[i].rotation = (scaled * a.local[i].rotation).normalized();
        a.local[i].position += (b.local[i].position - ref.local[i].position) * t;
    }
}

bool solveTwoBoneIK(const Skeleton& s, Pose& pose, int root, int mid, int end, const Vec3& target, const Vec3& poleHint,
                    float weight) {
    if (root < 0 || mid < 0 || end < 0 || weight <= 0.0f) return false;
    std::vector<Transform> ms;
    pose.modelSpace(s, ms);
    Vec3 a = ms[size_t(root)].position, b = ms[size_t(mid)].position, c = ms[size_t(end)].position;
    float lab = distance(a, b), lcb = distance(b, c);
    Vec3 tgt = lerp(c, target, weight);
    float lat = clampf(distance(a, tgt), 0.01f, (lab + lcb) * 0.9995f);

    // current angles
    auto safeAcos = [](float v) { return std::acos(clampf(v, -1.0f, 1.0f)); };
    float acab0 = safeAcos(dot((c - a).normalized(), (b - a).normalized()));
    float bab0 = safeAcos(dot((a - b).normalized(), (c - b).normalized()));
    float acat0 = safeAcos(dot((c - a).normalized(), (tgt - a).normalized()));
    // desired angles (law of cosines)
    float acab1 = safeAcos((lcb * lcb - lab * lab - lat * lat) / (-2.0f * lab * lat));
    float bab1 = safeAcos((lat * lat - lab * lab - lcb * lcb) / (-2.0f * lab * lcb));

    // bend axis from pole hint
    Vec3 axis0 = cross(c - a, poleHint - a).normalized();
    if (axis0.lengthSq() < 1e-6f) axis0 = cross(c - a, b - a).normalized();
    if (axis0.lengthSq() < 1e-6f) axis0 = Vec3(1, 0, 0);
    Vec3 axis1 = cross(c - a, tgt - a).normalized();
    if (axis1.lengthSq() < 1e-6f) axis1 = axis0;

    Quat rootWorld = ms[size_t(root)].rotation;
    Quat midWorld = ms[size_t(mid)].rotation;
    // rotations are applied in model space then converted to local
    Quat r0 = Quat::angleAxis(acab1 - acab0, axis0);
    Quat r1 = Quat::angleAxis(bab1 - bab0, axis0);
    Quat r2 = Quat::angleAxis(acat0, axis1);

    Quat newRootWorld = (r2 * r0 * rootWorld).normalized();
    Quat newMidWorld = (r2 * r0 * r1 * midWorld).normalized();
    // convert to local
    int rp = s.joints[size_t(root)].parent;
    Quat parentRot = rp >= 0 ? ms[size_t(rp)].rotation : Quat::identity();
    pose.local[size_t(root)].rotation = (parentRot.conjugate() * newRootWorld).normalized();
    pose.local[size_t(mid)].rotation = (newRootWorld.conjugate() * newMidWorld).normalized();
    return true;
}

}  // namespace sw
