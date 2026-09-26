#include "animation/anim_state_machine.h"

#include <algorithm>

namespace sw {

void AnimStateMachine::addState(const std::string& name, AnimationClipPtr clip, bool loop, float speed) {
    if (!clip) return;
    State s;
    s.clip = clip;
    s.loop = loop;
    s.speed = speed;
    states_[name] = s;
}

void AnimStateMachine::play(const std::string& name, float fadeTime, bool restart) {
    if (!states_.count(name)) return;
    if (name == current_ && !restart) return;
    float rate = fadeTime > 1e-4f ? 1.0f / fadeTime : 1000.0f;
    for (auto& t : tracks_) t.fadeRate = -rate;
    // reuse a fading track of the same state (keeps its time for looping states)
    for (auto& t : tracks_)
        if (t.state == name && !restart) {
            t.fadeRate = rate;
            current_ = name;
            return;
        }
    Track tr;
    tr.state = name;
    tr.weight = tracks_.empty() ? 1.0f : 0.0f;
    tr.fadeRate = rate;
    tracks_.push_back(tr);
    current_ = name;
}

void AnimStateMachine::update(float dt) {
    for (auto& t : tracks_) {
        const State& s = states_[t.state];
        t.time += dt * s.speed * speedScale_;
        if (!s.loop && s.clip) t.time = std::min(t.time, s.clip->duration);
        t.weight = clampf(t.weight + t.fadeRate * dt, 0.0f, 1.0f);
    }
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [](const Track& t) { return t.weight <= 0.0f && t.fadeRate < 0.0f; }),
                  tracks_.end());
}

void AnimStateMachine::evaluate(Pose& out) const {
    if (!skel_) return;
    out.setBind(*skel_);
    if (tracks_.empty()) return;
    // accumulate: first track full, then blend the rest in order by their normalized weights
    float total = 0.0f;
    Pose tmp;
    bool first = true;
    for (const auto& t : tracks_) {
        if (t.weight <= 0.0f) continue;
        auto it = states_.find(t.state);
        if (it == states_.end() || !it->second.clip) continue;
        tmp.setBind(*skel_);
        it->second.clip->sample(t.time, tmp);
        // crossfades ease in and out (a linear fade starts and stops the motion abruptly)
        float w = t.weight * t.weight * (3.0f - 2.0f * t.weight);
        if (w <= 1e-4f) continue;
        total += w;
        if (first) {
            out = tmp;
            first = false;
        } else {
            blendPoses(out, tmp, w / total);
        }
    }
}

void AnimStateMachine::sampleState(const std::string& name, float nt, Pose& out) const {
    auto it = states_.find(name);
    if (it == states_.end() || !it->second.clip || !skel_) return;
    out.setBind(*skel_);
    it->second.clip->sample(nt * it->second.clip->duration, out);
}

float AnimStateMachine::currentTime() const {
    for (const auto& t : tracks_)
        if (t.state == current_) return t.time;
    return 0.0f;
}

float AnimStateMachine::currentNormalizedTime() const {
    auto it = states_.find(current_);
    if (it == states_.end() || !it->second.clip || it->second.clip->duration <= 0) return 0.0f;
    float d = it->second.clip->duration;
    float t = currentTime();
    return it->second.loop ? std::fmod(t, d) / d : std::min(t / d, 1.0f);
}

}  // namespace sw
