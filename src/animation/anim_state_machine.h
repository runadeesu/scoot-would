// scoot would - animation player / state machine with cross fades
#pragma once

#include "animation/animation.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace sw {

class AnimStateMachine {
public:
    void setSkeleton(const Skeleton* s) { skel_ = s; }
    void addState(const std::string& name, AnimationClipPtr clip, bool loop, float speed = 1.0f);
    bool has(const std::string& name) const { return states_.count(name) > 0; }
    // switch to a state; blends from the current pose over fadeTime (no pose pops)
    void play(const std::string& name, float fadeTime, bool restart = false);
    void setSpeed(float s) { speedScale_ = s; }
    void update(float dt);
    // sample into pose (starts from the rest pose)
    void evaluate(Pose& out) const;
    // sample a single state at a normalized time into pose (overlay layers)
    void sampleState(const std::string& name, float normalizedTime, Pose& out) const;
    const std::string& current() const { return current_; }
    float currentTime() const;
    float currentNormalizedTime() const;

private:
    struct State {
        AnimationClipPtr clip;
        bool loop = true;
        float speed = 1.0f;
    };
    struct Track {
        std::string state;
        float time = 0.0f;
        float weight = 0.0f;
        float fadeRate = 0.0f;  // weight change per second (+ in, - out)
    };
    const Skeleton* skel_ = nullptr;
    std::unordered_map<std::string, State> states_;
    std::vector<Track> tracks_;
    std::string current_;
    float speedScale_ = 1.0f;
};

}  // namespace sw
