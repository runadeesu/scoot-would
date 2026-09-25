#include "game/tricks/trick_system.h"
#include "core/log.h"

#include <algorithm>
#include <cmath>

namespace sw {

namespace {

TrickChannel channelFrom(const std::string& s) {
    if (s == "bars") return TrickChannel::Bars;
    if (s == "rider") return TrickChannel::Rider;
    if (s == "whole") return TrickChannel::Whole;
    return TrickChannel::Deck;
}

ScooterAxis axisFrom(const std::string& s) {
    if (s == "steer") return ScooterAxis::Steer;
    if (s == "deck_roll") return ScooterAxis::DeckRoll;
    if (s == "deck_pitch") return ScooterAxis::DeckPitch;
    if (s == "bars") return ScooterAxis::Bars;
    if (s == "whole_pitch") return ScooterAxis::WholePitch;
    if (s == "whole_yaw") return ScooterAxis::WholeYaw;
    return ScooterAxis::None;
}

float easeInOut(float t) {
    t = saturate(t);
    return t * t * (3.0f - 2.0f * t);
}

bool conflicts(TrickChannel a, TrickChannel b) {
    if (a == b) return true;
    // a whole scooter trick uses deck + bars
    if (a == TrickChannel::Whole && (b == TrickChannel::Deck || b == TrickChannel::Bars)) return true;
    if (b == TrickChannel::Whole && (a == TrickChannel::Deck || a == TrickChannel::Bars)) return true;
    return false;
}

}  // namespace

bool TrickSystem::loadDefinitions(const std::string& absPath) {
    auto j = loadJsonFile(absPath);
    if (!j) return false;
    defs_.clear();
    for (const Json& t : (*j)["tricks"]) {
        TrickDefinition d;
        d.id = jget<std::string>(t, "id", "");
        d.name = jget<std::string>(t, "name", d.id);
        d.category = jget<std::string>(t, "category", "whip");
        d.channel = channelFrom(jget<std::string>(t, "channel", "deck"));
        if (t.contains("input")) {
            const Json& in = t["input"];
            d.inputType = jget<std::string>(in, "type", "flick");
            for (auto& s : in.value("dirs", Json::array())) d.dirs.push_back(stickDirFromName(s.get<std::string>()));
            for (auto& s : in.value("flow_dirs", Json::array())) d.flowDirs.push_back(stickDirFromName(s.get<std::string>()));
            if (d.flowDirs.empty()) d.flowDirs = d.dirs;
            d.modifierGrab = jget<std::string>(in, "modifier", "none") == "grab";
        }
        d.minAirtime = jget<float>(t, "airtime", 0.3f);
        d.duration = jget<float>(t, "duration", 0.4f);
        if (t.contains("scooter_rotation")) {
            const Json& r = t["scooter_rotation"];
            d.axis = axisFrom(jget<std::string>(r, "axis", "steer"));
            d.degrees = jget<float>(r, "degrees", 360.0f);
            d.direction = jget<int>(r, "direction", 1);
            d.rewind = jget<bool>(r, "rewind", false);
        } else {
            d.axis = ScooterAxis::None;
        }
        d.animation = jget<std::string>(t, "animation", "");
        d.feetOff = jget<bool>(t, "feet_off", false);
        d.handsOff = jget<bool>(t, "hands_off", false);
        d.oneHand = jget<bool>(t, "one_hand", false);
        d.score = jget<int>(t, "score", 100);
        d.difficulty = jget<int>(t, "difficulty", 1);
        d.cancelWindow = jget<float>(t, "cancel_window", 0.08f);
        d.chainTo = jget<std::string>(t, "chain", "");
        d.hold = jget<bool>(t, "hold", d.inputType == "grab");
        defs_.push_back(d);
    }
    LOG_INFO("tricks: %zu trick definitions", defs_.size());
    return true;
}

const TrickDefinition* TrickSystem::find(const std::string& id) const {
    for (auto& d : defs_)
        if (d.id == id) return &d;
    return nullptr;
}

void TrickSystem::beginAir(float predictedAirtime, bool fakie) {
    inAir_ = true;
    startFakie_ = fakie;
    active_.clear();
    finished_.clear();
    spin_ = flip_ = roll_ = 0.0f;
    airTime_ = 0.0f;
    predicted_ = predictedAirtime;
    pose_ = ScooterPose{};
    lastStarted.clear();
}

void TrickSystem::cancel() {
    inAir_ = false;
    active_.clear();
    finished_.clear();
    pose_ = ScooterPose{};
}

bool TrickSystem::tryStart(const TrickDefinition& d, int direction, double) {
    for (const ActiveTrick& a : active_)
        if (conflicts(a.def->channel, d.channel)) return false;
    ActiveTrick t;
    t.def = &d;
    t.direction = direction;
    active_.push_back(t);
    lastStarted = d.name;
    startedThisStep = true;
    return true;
}

void TrickSystem::airUpdate(float dt, std::deque<FlickEvent>& flicks, double now, float grabAxis, StickDir rightDir, const Vec3& angVel,
                            const Vec3& bodyRight, const Vec3& bodyForward, float timeToLand) {
    (void)timeToLand;
    startedThisStep = false;
    if (!inAir_) return;
    airTime_ += dt;
    spin_ += dot(angVel, Vec3(0, 1, 0)) * dt;
    flip_ += dot(angVel, bodyRight) * dt;
    roll_ += dot(angVel, bodyForward) * dt;

    auto dirsOf = [&](const TrickDefinition& d) -> const std::vector<StickDir>& { return flow_ ? d.flowDirs : d.dirs; };
    // buffered flicks -> trick starts
    for (size_t fi = 0; fi < flicks.size(); ++fi) {
        FlickEvent& f = flicks[fi];
        if (f.consumed || now - f.time > 0.25) continue;
        bool grabMod = f.modifierGrab || grabAxis > 0.3f;
        if (flow_) {
            // Scooter Flow layout: a bare right stick rotates the rider, tricks need RT (scooter) or LT (grabs)
            if (!f.modifierTrick && !f.modifierGrab) {
                f.consumed = true;
                continue;
            }
            grabMod = f.modifierGrab && !f.modifierTrick;
        }
        // 1) sequences (rewind): previous flick + this one
        const TrickDefinition* seqMatch = nullptr;
        for (auto& d : defs_) {
            const auto& dd = dirsOf(d);
            if (d.inputType != "sequence" || dd.size() != 2 || d.modifierGrab != grabMod) continue;
            if (dd[1] != f.dir) continue;
            // the first flick of the sequence started a trick shortly before
            for (auto& a : active_) {
                const auto& ad = dirsOf(*a.def);
                if (a.time < 0.28f && a.def->channel == d.channel && a.def->inputType != "sequence" &&
                    std::find(ad.begin(), ad.end(), dd[0]) != ad.end()) {
                    seqMatch = &d;
                    a.def = &d;  // convert in place (keeps timing)
                    a.progress = a.time / d.duration;
                    f.consumed = true;
                    lastStarted = d.name;
                    startedThisStep = true;
                    break;
                }
            }
            if (seqMatch) break;
        }
        if (f.consumed) continue;
        // 2) chaining: same input while the trick is running upgrades it (double / triple)
        bool chained = false;
        for (auto& a : active_) {
            const auto& ad = dirsOf(*a.def);
            if (a.done || a.def->chainTo.empty() || ad.empty()) continue;
            bool sameInput = std::find(ad.begin(), ad.end(), f.dir) != ad.end() && a.def->modifierGrab == grabMod;
            if (!sameInput) continue;
            const TrickDefinition* up = find(a.def->chainTo);
            if (!up) continue;
            float angleDone = a.progress * a.def->degrees;
            a.def = up;
            a.time = angleDone / up->degrees * up->duration;
            a.progress = a.time / up->duration;
            f.consumed = true;
            chained = true;
            lastStarted = up->name;
            startedThisStep = true;
            break;
        }
        if (chained) continue;
        // 3) single flick tricks
        for (auto& d : defs_) {
            if (d.inputType == "sequence" || d.inputType == "chain") continue;
            if (d.modifierGrab != grabMod) continue;
            const auto& dd = dirsOf(d);
            auto it = std::find(dd.begin(), dd.end(), f.dir);
            if (it == dd.end()) continue;
            // classic: left side inputs spin the trick the other way (the Scooter Flow layout gives each side its
            // own trick, e.g. right = tailwhip, left = heelwhip)
            bool leftSide = f.dir == StickDir::Left || f.dir == StickDir::UpLeft || f.dir == StickDir::DownLeft;
            int dir = (!flow_ && leftSide) ? -d.direction : d.direction;
            if (tryStart(d, dir, now)) f.consumed = true;
            break;
        }
        f.consumed = true;  // unmatched flicks are dropped (no late trigger)
    }

    // holding the grab trigger and pointing the right stick starts a grab even without a flick
    if (grabAxis > 0.5f && rightDir != StickDir::None) {
        bool riderBusy = false;
        for (auto& a : active_)
            if (a.def->channel == TrickChannel::Rider) riderBusy = true;
        if (!riderBusy && grabCooldown_ <= 0.0f) {
            for (auto& d : defs_) {
                if (d.inputType != "grab") continue;
                const auto& dd = dirsOf(d);
                if (std::find(dd.begin(), dd.end(), rightDir) == dd.end()) continue;
                if (tryStart(d, 1, now)) grabCooldown_ = 0.3f;
                break;
            }
        }
    }
    grabCooldown_ -= dt;

    // advance active tricks
    for (auto& a : active_) {
        a.time += dt;
        if (a.def->hold) {
            a.progress = saturate(a.time / std::max(a.def->duration, 0.01f));
            if (grabAxis > 0.25f)
                a.holdTime += dt;
            else
                a.released = true;
            if (a.released && a.progress >= 1.0f) a.done = true;
        } else {
            a.progress = a.time / a.def->duration;
            if (a.progress >= 1.0f) {
                a.progress = 1.0f;
                a.done = true;
            }
        }
    }
    for (auto it = active_.begin(); it != active_.end();) {
        if (it->done) {
            finished_.push_back(*it);
            it = active_.erase(it);
        } else {
            ++it;
        }
    }
    updatePose(dt);
}

void TrickSystem::updatePose(float dt) {
    ScooterPose p;
    float riderW = 0.0f;
    for (const ActiveTrick& a : active_) {
        const TrickDefinition& d = *a.def;
        float e = easeInOut(a.progress);
        float ang = d.degrees * kDeg2Rad * float(a.direction);
        float v = d.rewind ? std::sin(a.progress * kPi) * kPi * float(a.direction) : ang * e;
        switch (d.axis) {
            case ScooterAxis::Steer: p.deckSteer += v; break;
            case ScooterAxis::DeckRoll: p.deckRoll += v; break;
            case ScooterAxis::DeckPitch: p.deckPitch += v; break;
            case ScooterAxis::Bars: p.bars += v; break;
            case ScooterAxis::WholePitch: p.wholePitch += v; break;
            case ScooterAxis::WholeYaw: p.wholeYaw += v; break;
            case ScooterAxis::None: break;
        }
        bool mid = a.progress > 0.02f && a.progress < 0.97f;
        if (d.hold) mid = !a.released || a.progress < 1.0f;
        if (mid) {
            p.feetOff |= d.feetOff;
            p.handsOff |= d.handsOff;
            p.oneHand |= d.oneHand;
        }
        if (d.channel == TrickChannel::Whole) p.scooterAway = std::max(p.scooterAway, std::sin(saturate(a.progress) * kPi));
        if (!d.animation.empty()) {
            float w = d.hold ? (a.released ? 1.0f - saturate((a.time - a.def->duration) * 4.0f) : saturate(a.time * 6.0f)) : std::sin(saturate(a.progress) * kPi);
            if (w > riderW) {
                riderW = w;
                p.riderPose = d.animation;
            }
        }
    }
    p.riderPoseWeight = riderW;
    // smooth the rider pose weight so poses blend in / out
    pose_.riderPoseWeight = dampf(pose_.riderPoseWeight, p.riderPoseWeight, 14.0f, dt);
    std::string keepPose = p.riderPose.empty() ? pose_.riderPose : p.riderPose;
    float keepW = pose_.riderPoseWeight;
    pose_ = p;
    pose_.riderPose = keepPose;
    pose_.riderPoseWeight = keepW;
}

float TrickSystem::completion() const {
    float c = 1.0f;
    for (auto& a : active_)
        if (!a.def->hold) c = std::min(c, a.progress);
    return c;
}

std::string TrickSystem::bodyTrickName(bool fakie, int& score, int& difficulty) const {
    float spinDeg = std::fabs(spin_) * kRad2Deg;
    float flipDeg = flip_ * kRad2Deg;
    float rollDeg = std::fabs(roll_) * kRad2Deg;
    int spinSteps = int(std::floor((spinDeg + 60.0f) / 180.0f));
    int flips = int(std::floor((std::fabs(flipDeg) + 100.0f) / 360.0f));
    bool back = flipDeg > 0.0f;
    std::string name;
    score = 0;
    difficulty = 0;
    static const int spinScore[] = {0, 100, 250, 450, 700, 1000, 1400, 1900};
    int s = std::min(spinSteps, 7);
    if (flips >= 1 && back && spinSteps == 1) {
        name = flips == 1 ? "Flair" : "Double Flair";
        score = flips == 1 ? 1100 : 2600;
        difficulty = flips == 1 ? 4 : 7;
    } else if (flips >= 1) {
        std::string pre = flips == 2 ? "Double " : flips >= 3 ? "Triple " : "";
        name = pre + (back ? "Backflip" : "Frontflip");
        score = (back ? 600 : 750) * flips + (flips > 1 ? 600 : 0);
        difficulty = (back ? 3 : 4) + (flips - 1) * 3;
        if (spinSteps >= 1) {
            name = std::to_string(spinSteps * 180) + " " + name;
            score += spinScore[s];
            difficulty += spinSteps;
        }
    } else if (spinSteps >= 2 && rollDeg > 110.0f) {
        name = "Cork " + std::to_string(spinSteps * 180);
        score = int(float(spinScore[s]) * 1.5f);
        difficulty = spinSteps + 2;
    } else if (spinSteps >= 1) {
        name = std::to_string(spinSteps * 180);
        score = spinScore[s];
        difficulty = spinSteps;
    }
    if (fakie && !name.empty()) name = "Fakie " + name;
    return name;
}

std::string TrickSystem::currentLabel() const {
    int s, d;
    std::string body = bodyTrickName(startFakie_, s, d);
    std::string out = body;
    for (auto& a : finished_) out += (out.empty() ? "" : " ") + a.def->name;
    for (auto& a : active_) out += (out.empty() ? "" : " ") + a.def->name;
    return out;
}

bool TrickSystem::land(TrickResult& out, bool landedFakie) {
    (void)landedFakie;
    inAir_ = false;
    bool ok = true;
    std::vector<ActiveTrick> all = finished_;
    for (auto& a : active_) {
        if (a.def->hold) {
            all.push_back(a);
            continue;
        }
        float remaining = (1.0f - a.progress) * a.def->duration;
        if (remaining <= a.def->cancelWindow) {
            all.push_back(a);
        } else {
            ok = false;
        }
    }
    int bodyScore = 0, bodyDiff = 0;
    std::string body = bodyTrickName(startFakie_, bodyScore, bodyDiff);
    out = TrickResult{};
    std::string name = body;
    int score = bodyScore;
    int diff = bodyDiff;
    int n = 0;
    for (auto& a : all) {
        name += (name.empty() ? "" : " ") + a.def->name;
        int ts = a.def->score;
        if (a.def->hold) ts = int(float(ts) * (0.6f + std::min(a.holdTime, 1.5f)));  // longer grabs score more
        score += ts;
        diff += a.def->difficulty;
        out.parts.push_back(a.def->name);
        ++n;
    }
    if (!body.empty()) out.parts.insert(out.parts.begin(), body);
    // combining a body rotation with scooter tricks is worth more than the sum
    if (!body.empty() && n > 0) score = int(float(score) * (1.2f + 0.15f * float(n - 1)));
    else if (n > 1) score = int(float(score) * (1.0f + 0.15f * float(n - 1)));
    out.name = name;
    out.score = score;
    out.difficulty = diff;
    active_.clear();
    finished_.clear();
    pose_ = ScooterPose{};
    return ok;
}

}  // namespace sw
