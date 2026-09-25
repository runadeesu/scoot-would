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
    if (s == "whole_roll") return ScooterAxis::WholeRoll;
    if (s == "bar_pitch") return ScooterAxis::BarPitch;
    if (s == "bar_roll") return ScooterAxis::BarRoll;
    return ScooterAxis::None;
}

TrickLayer layerFrom(const std::string& s) {
    if (s == "grab") return TrickLayer::Grab;
    if (s == "trick_alt") return TrickLayer::TrickAlt;
    if (s == "grab_alt") return TrickLayer::GrabAlt;
    return TrickLayer::Trick;
}

TrickRotation rotationFrom(const Json& r) {
    TrickRotation t;
    t.axis = axisFrom(jget<std::string>(r, "axis", "steer"));
    t.degrees = jget<float>(r, "degrees", 360.0f);
    t.direction = jget<int>(r, "direction", 1);
    t.rewind = jget<bool>(r, "rewind", false);
    return t;
}

float easeInOut(float t) {
    t = saturate(t);
    return t * t * (3.0f - 2.0f * t);
}

// distance along a move with a trapezoid speed profile: speeds up over `a`, cruises, slows down over `b`
float trapezoid(float u, float a, float b) {
    u = saturate(u);
    a = clampf(a, 0.0f, 0.9f);
    b = clampf(b, 0.0f, 0.9f - a);
    float vmax = 1.0f / std::max(1.0f - 0.5f * a - 0.5f * b, 1e-3f);
    if (a > 0.0f && u < a) return vmax * u * u / (2.0f * a);
    if (b > 0.0f && u > 1.0f - b) {
        float d = 1.0f - u;
        return 1.0f - vmax * d * d / (2.0f * b);
    }
    return vmax * (0.5f * a + (u - a));
}

void readRange(const Json& j, const char* key, float out[2]) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() != 2) return;
    out[0] = j[key][0].get<float>();
    out[1] = j[key][1].get<float>();
}

bool inRange(const float r[2], float t) { return r[0] >= 0.0f && t >= r[0] && t < r[1]; }

bool conflicts(TrickChannel a, TrickChannel b) {
    if (a == b) return true;
    // a whole scooter trick uses deck + bars
    if (a == TrickChannel::Whole && (b == TrickChannel::Deck || b == TrickChannel::Bars)) return true;
    if (b == TrickChannel::Whole && (a == TrickChannel::Deck || a == TrickChannel::Bars)) return true;
    return false;
}

bool isGrabLayer(TrickLayer l) { return l == TrickLayer::Grab || l == TrickLayer::GrabAlt; }

}  // namespace

bool TrickSystem::loadDefinitions(const std::string& absPath) {
    auto j = loadJsonFile(absPath);
    if (!j) return false;
    defs_.clear();
    for (const Json& t : (*j)["tricks"]) {
        TrickDefinition d;
        d.id = jget<std::string>(t, "id", "");
        d.name = jget<std::string>(t, "name", d.id);
        d.shortName = jget<std::string>(t, "short", d.name);
        d.family = jget<std::string>(t, "family", "");
        d.category = jget<std::string>(t, "category", "whip");
        d.channel = channelFrom(jget<std::string>(t, "channel", "deck"));
        if (t.contains("input")) {
            const Json& in = t["input"];
            d.inputType = jget<std::string>(in, "type", "flick");
            for (auto& s : in.value("dirs", Json::array())) d.dirs.push_back(stickDirFromName(s.get<std::string>()));
            for (auto& s : in.value("flow_dirs", Json::array())) d.flowDirs.push_back(stickDirFromName(s.get<std::string>()));
            if (d.flowDirs.empty()) d.flowDirs = d.dirs;
            // "modifier": "grab" is the older spelling of the grab layer
            std::string layer = jget<std::string>(in, "layer", jget<std::string>(in, "modifier", "none") == "grab" ? "grab" : "trick");
            d.layer = layerFrom(layer);
        }
        d.minAirtime = jget<float>(t, "airtime", 0.3f);
        d.duration = jget<float>(t, "duration", 0.4f);
        if (t.contains("scooter_rotation")) {
            const Json& r = t["scooter_rotation"];
            if (r.is_array())
                for (const Json& e : r) d.rotations.push_back(rotationFrom(e));
            else
                d.rotations.push_back(rotationFrom(r));
        }
        d.degrees = d.rotations.empty() ? 0.0f : d.rotations[0].degrees;
        d.animation = jget<std::string>(t, "animation", "");
        d.feetOff = jget<bool>(t, "feet_off", false);
        d.handsOff = jget<bool>(t, "hands_off", false);
        d.oneHand = jget<bool>(t, "one_hand", false);
        d.frontFootOff = jget<bool>(t, "front_foot_off", false);
        d.backFootOff = jget<bool>(t, "back_foot_off", false);
        d.score = jget<int>(t, "score", 100);
        d.difficulty = jget<int>(t, "difficulty", 1);
        d.cancelWindow = jget<float>(t, "cancel_window", 0.08f);
        d.chainTo = jget<std::string>(t, "chain", "");
        d.hold = jget<bool>(t, "hold", d.inputType == "grab");
        // timeline: defaults from the contact flags, then the trick's own values
        TrickTimeline& tl = d.timeline;
        if (d.feetOff) {
            tl.backFoot[0] = 0.03f, tl.backFoot[1] = 0.9f;    // the back foot kicks and lands last
            tl.frontFoot[0] = 0.06f, tl.frontFoot[1] = 0.84f;  // the front foot comes down first and stops the deck
        }
        if (d.frontFootOff) tl.frontFoot[0] = 0.06f, tl.frontFoot[1] = 0.86f;
        if (d.backFootOff) tl.backFoot[0] = 0.04f, tl.backFoot[1] = 0.88f;
        if (d.handsOff) tl.hands[0] = 0.14f, tl.hands[1] = 0.86f;
        if (d.oneHand) tl.backHand[0] = 0.1f, tl.backHand[1] = 0.84f;
        if (t.contains("timeline")) {
            const Json& j = t["timeline"];
            readRange(j, "front_foot", tl.frontFoot);
            readRange(j, "back_foot", tl.backFoot);
            readRange(j, "hands", tl.hands);
            readRange(j, "back_hand", tl.backHand);
            readRange(j, "hand_deck", tl.handDeck);
            tl.pivot = jget<float>(j, "pivot", tl.pivot);
            tl.hover = jget<bool>(j, "hover", tl.hover);
            tl.handDeckRear = jget<std::string>(j, "hand_deck_at", "front") == "rear";
            float rot[2] = {tl.rotStart, tl.rotEnd};
            readRange(j, "rotate", rot);
            tl.rotStart = rot[0];
            tl.rotEnd = rot[1];
            tl.accel = jget<float>(j, "accel", tl.accel);
            tl.decel = jget<float>(j, "decel", tl.decel);
            tl.lift = jget<float>(j, "lift", tl.lift);
            tl.side = jget<float>(j, "side", tl.side);
            tl.raise = jget<float>(j, "raise", tl.raise);
            tl.forward = jget<float>(j, "forward", tl.forward);
            tl.barsTurn = jget<float>(j, "bars_turn", tl.barsTurn);
        }
        defs_.push_back(d);
    }
    LOG_INFO("tricks: %zu trick definitions", defs_.size());
    return true;
}

float TrickDefinition::rotationAt(const TrickRotation& r, float t) const {
    float u = saturate((t - timeline.rotStart) / std::max(timeline.rotEnd - timeline.rotStart, 0.01f));
    float f = trapezoid(u, timeline.accel, timeline.decel);
    return r.degrees * kDeg2Rad * (r.rewind ? std::sin(f * kPi) : f);
}

const TrickDefinition* TrickSystem::find(const std::string& id) const {
    for (auto& d : defs_)
        if (d.id == id) return &d;
    return nullptr;
}

TrickLayer TrickSystem::layerFor(bool grab, bool trick, bool alt) const {
    if (flow_) {
        // Scooter Flow layout: the bare right stick rotates the rider; RT = scooter tricks, LT = grabs (both
        // triggers: scooter tricks), a bumper on top selects the second layer
        if (!grab && !trick) return TrickLayer::None;
        bool g = grab && !trick;
        return g ? (alt ? TrickLayer::GrabAlt : TrickLayer::Grab) : (alt ? TrickLayer::TrickAlt : TrickLayer::Trick);
    }
    // classic: bare flicks are scooter tricks, RT + flick grabs
    return grab ? (alt ? TrickLayer::GrabAlt : TrickLayer::Grab) : (alt ? TrickLayer::TrickAlt : TrickLayer::Trick);
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
    // transitions: the next trick may start over the last quarter of the one using the same parts
    for (const ActiveTrick& a : active_)
        if (conflicts(a.def->channel, d.channel) && (a.def->hold || a.progress < 0.75f)) return false;
    ActiveTrick t;
    t.def = &d;
    t.direction = direction;
    t.startAir = airTime_;
    active_.push_back(t);
    lastStarted = d.name;
    startedThisStep = true;
    return true;
}

void TrickSystem::upgrade(ActiveTrick& a, const TrickDefinition* up) {
    // keep the angle already turned: a tailwhip at 200 degrees becomes a double whip at 200 of 720 degrees
    // (found on the new trick's speed profile, so the deck does not jump)
    float t = 0.0f;
    if (!a.def->rotations.empty() && !up->rotations.empty()) {
        float angleDone = a.def->rotationAt(a.def->rotations[0], a.progress);
        float lo = 0.0f, hi = 1.0f;
        for (int i = 0; i < 24; ++i) {
            float mid = 0.5f * (lo + hi);
            (up->rotationAt(up->rotations[0], mid) < angleDone ? lo : hi) = mid;
        }
        t = 0.5f * (lo + hi);
    }
    a.def = up;
    a.time = t * up->duration;
    a.progress = t;
    lastStarted = up->name;
    startedThisStep = true;
}

void TrickSystem::airUpdate(float dt, std::deque<FlickEvent>& flicks, double now, float grabAxis, float trickAxis, bool altHeld,
                            StickDir rightDir, const Vec3& angVel, const Vec3& bodyRight, const Vec3& bodyForward, float timeToLand) {
    (void)timeToLand;
    startedThisStep = false;
    if (!inAir_) return;
    airTime_ += dt;
    spin_ += dot(angVel, spinAxis_) * dt;
    flip_ += dot(angVel, bodyRight) * dt;
    roll_ += dot(angVel, bodyForward) * dt;

    auto dirsOf = [&](const TrickDefinition& d) -> const std::vector<StickDir>& { return flow_ ? d.flowDirs : d.dirs; };
    auto has = [](const std::vector<StickDir>& v, StickDir s) { return std::find(v.begin(), v.end(), s) != v.end(); };
    // buffered flicks -> trick starts
    for (size_t fi = 0; fi < flicks.size(); ++fi) {
        FlickEvent& f = flicks[fi];
        if (f.consumed || now - f.time > 0.25) continue;
        TrickLayer layer = layerFor(f.modifierGrab || (!flow_ && grabAxis > 0.3f), f.modifierTrick, f.modifierAlt);
        if (layer == TrickLayer::None) {
            f.consumed = true;  // bare right stick in the Scooter Flow layout: body rotation, not a trick
            continue;
        }
        // 1) sequences (rewind): previous flick + this one
        const TrickDefinition* seqMatch = nullptr;
        for (auto& d : defs_) {
            const auto& dd = dirsOf(d);
            if (d.inputType != "sequence" || dd.size() != 2 || d.layer != layer) continue;
            if (dd[1] != f.dir) continue;
            // the first flick of the sequence started a trick shortly before
            for (auto& a : active_) {
                const auto& ad = dirsOf(*a.def);
                if (a.time < 0.28f && a.def->channel == d.channel && a.def->inputType != "sequence" && has(ad, dd[0])) {
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
        // 2) chaining: the same input while the trick is running upgrades it (double / triple / quad)
        bool chained = false;
        for (auto& a : active_) {
            const auto& ad = dirsOf(*a.def);
            if (a.done || a.def->chainTo.empty() || ad.empty()) continue;
            if (!has(ad, f.dir) || a.def->layer != layer) continue;
            const TrickDefinition* up = find(a.def->chainTo);
            if (!up) continue;
            upgrade(a, up);
            f.consumed = true;
            chained = true;
            break;
        }
        if (chained) continue;
        // 3) single flick tricks
        for (auto& d : defs_) {
            if (d.inputType == "sequence" || d.inputType == "chain") continue;
            if (d.layer != layer) continue;
            if (!has(dirsOf(d), f.dir)) continue;
            // classic: left side inputs spin the trick the other way (the Scooter Flow layout gives each side its
            // own trick, e.g. right = tailwhip, left = heelwhip)
            bool leftSide = f.dir == StickDir::Left || f.dir == StickDir::UpLeft || f.dir == StickDir::DownLeft;
            int mirror = (!flow_ && leftSide && layer == TrickLayer::Trick) ? -1 : 1;
            if (tryStart(d, mirror, now)) f.consumed = true;
            break;
        }
        f.consumed = true;  // unmatched flicks are dropped (no late trigger)
    }

    // Scooter Flow layout: holding the stick keeps a whip / bar going ("continuous overheads")
    if (flow_ && rightDir != StickDir::None) {
        TrickLayer held = layerFor(grabAxis > 0.3f, trickAxis > 0.3f, altHeld);
        for (auto& a : active_) {
            if (a.done || a.def->chainTo.empty() || a.def->layer != held || a.progress < 0.62f) continue;
            if (!has(dirsOf(*a.def), rightDir)) continue;
            if (const TrickDefinition* up = find(a.def->chainTo)) upgrade(a, up);
        }
    }

    // holding the grab trigger and pointing the right stick starts a grab even without a flick
    if (grabAxis > 0.5f && rightDir != StickDir::None && !(flow_ && trickAxis > 0.3f)) {
        TrickLayer layer = altHeld ? TrickLayer::GrabAlt : TrickLayer::Grab;
        bool riderBusy = false;
        for (auto& a : active_)
            if (a.def->channel == TrickChannel::Rider) riderBusy = true;
        if (!riderBusy && grabCooldown_ <= 0.0f) {
            for (auto& d : defs_) {
                if (d.inputType != "grab" || d.layer != layer) continue;
                if (!has(dirsOf(d), rightDir)) continue;
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
        const TrickTimeline& tl = d.timeline;
        float t = saturate(a.progress);
        // held tricks (grabs, turndowns) ease in, stay while held, ease out after release
        float holdW = d.hold ? (a.released ? 1.0f - saturate((a.time - d.duration) * 4.0f) : saturate(a.time * 6.0f)) : 0.0f;
        // the scooter is held out of the way while it goes round (lifted, pushed out, bars turned)
        float away = d.hold ? easeInOut(holdW) : easeInOut(t / 0.22f) * (1.0f - easeInOut((t - 0.72f) / 0.24f));
        p.offset += Vec3(tl.side * float(a.direction), tl.lift + tl.raise, -tl.forward) * away;
        p.bars += tl.barsTurn * kDeg2Rad * float(a.direction) * away;
        for (const TrickRotation& r : d.rotations) {
            float sign = float(r.direction * a.direction);
            float v = d.hold ? r.degrees * kDeg2Rad * sign * easeInOut(holdW) : d.rotationAt(r, t) * sign;
            switch (r.axis) {
                case ScooterAxis::Steer: p.deckSteer += v; break;
                case ScooterAxis::DeckRoll: p.deckRoll += v; break;
                case ScooterAxis::DeckPitch: p.deckPitch += v; break;
                case ScooterAxis::Bars: p.bars += v; break;
                case ScooterAxis::WholePitch: p.wholePitch += v; break;
                case ScooterAxis::WholeYaw: p.wholeYaw += v; break;
                case ScooterAxis::WholeRoll: p.wholeRoll += v; break;
                case ScooterAxis::BarPitch: p.barPitch += v; break;
                case ScooterAxis::BarRoll: p.barRoll += v; break;
                case ScooterAxis::None: break;
            }
        }
        if (d.hold) {
            if (!a.released || a.progress < 1.0f) {
                p.feetOff |= d.feetOff;
                p.handsOff |= d.handsOff;
                p.oneHand |= d.oneHand;
                p.frontFootOff |= d.frontFootOff;
                p.backFootOff |= d.backFootOff;
                if (tl.handDeck[0] >= 0.0f) {
                    p.handDeck = true;
                    p.handDeckRear |= tl.handDeckRear;
                }
            }
        } else {
            // hands and feet let go and catch again at their moments of the trick
            p.frontFootOff |= inRange(tl.frontFoot, t);
            p.backFootOff |= inRange(tl.backFoot, t);
            p.handsOff |= inRange(tl.hands, t);
            p.handsHover |= tl.hover && inRange(tl.hands, t);
            p.oneHand |= inRange(tl.backHand, t);
            if (inRange(tl.handDeck, t)) {
                p.handDeck = true;
                p.handDeckRear |= tl.handDeckRear;
            }
            p.pivot = std::max(p.pivot, tl.pivot);
        }
        if (!d.animation.empty()) {
            // trick clips play along the trick (kick, tuck, catch); grabs hold their pose
            float w = d.hold ? holdW : easeInOut(t / 0.06f) * (1.0f - easeInOut((t - 0.9f) / 0.1f));
            if (w >= riderW) {
                riderW = w;
                p.riderPose = d.animation;
                p.riderPoseTime = d.hold ? 0.5f : t;
            }
        }
    }
    p.riderPoseWeight = riderW;
    // smooth the rider pose weight so poses blend in / out
    pose_.riderPoseWeight = dampf(pose_.riderPoseWeight, p.riderPoseWeight, 14.0f, dt);
    std::string keepPose = p.riderPose.empty() ? pose_.riderPose : p.riderPose;
    float keepTime = p.riderPose.empty() ? pose_.riderPoseTime : p.riderPoseTime;
    float keepW = pose_.riderPoseWeight;
    pose_ = p;
    pose_.riderPose = keepPose;
    pose_.riderPoseTime = keepTime;
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
    if (flips >= 1 && (spinSteps % 2) == 1) {
        // a flip with an odd half turn lands back into the ramp: flair family (180 / 540 / 900)
        std::string pre = spinSteps >= 5 ? "900 " : spinSteps == 3 ? "540 " : "";
        std::string base = back ? (flips >= 2 ? "Double Flair" : "Flair") : (flips >= 2 ? "Double Front Flair" : "Front Flair");
        name = pre + base;
        score = (back ? 1100 : 1400) + (flips - 1) * 1500 + (spinSteps >= 5 ? 3100 : spinSteps == 3 ? 1300 : 0);
        difficulty = (back ? 4 : 5) + (flips - 1) * 3 + (spinSteps - 1);
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
    } else if (rollDeg > 300.0f && spinSteps < 2) {
        name = "Barrel Roll";
        score = 1300;
        difficulty = 5;
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

std::string TrickSystem::composeName(const std::string& body, std::vector<ActiveTrick> parts, int& bonusScore,
                                     std::vector<std::string>* outParts) const {
    bonusScore = 0;
    std::sort(parts.begin(), parts.end(), [](const ActiveTrick& a, const ActiveTrick& b) { return a.startAir < b.startAir; });
    struct Named {
        std::string name, shortName;
    };
    std::vector<Named> names;
    auto fam = [&](size_t i) { return i < parts.size() ? parts[i].def->family : std::string(); };
    for (size_t i = 0; i < parts.size();) {
        // buttercup: whip -> bri flip -> whip in one air (front buttercup with an inward)
        if (fam(i) == "whip" && (fam(i + 1) == "bri" || fam(i + 1) == "inward") && fam(i + 2) == "whip") {
            std::string n = fam(i + 1) == "bri" ? "Buttercup" : "Front Buttercup";
            names.push_back({n, n});
            bonusScore += 1500;
            i += 3;
            continue;
        }
        names.push_back({parts[i].def->name, parts[i].def->shortName});
        ++i;
    }
    // a 360 with a barspin is a truck driver
    std::string b = body;
    std::string plain = body.rfind("Fakie ", 0) == 0 ? body.substr(6) : body;
    if (plain == "360" && names.size() == 1 && (names[0].name == "Barspin" || names[0].name == "Double Barspin")) {
        std::string n = names[0].name == "Barspin" ? "Truck Driver" : "Double Truck Driver";
        names[0] = {n, n};
        b = body.size() > plain.size() ? "Fakie" : "";
        bonusScore += 300;
    }
    bool combined = !b.empty() || names.size() > 1;
    std::string out = b;
    for (size_t i = 0; i < names.size(); ++i) {
        const std::string& s = combined ? names[i].shortName : names[i].name;
        // the same trick twice in one air, one after the other: "Whip to Whip"
        bool again = i > 0 && names[i].shortName == names[i - 1].shortName;
        out += (out.empty() ? "" : again ? " to " : " ") + s;
        if (outParts) outParts->push_back(names[i].name);
    }
    return out;
}

std::string TrickSystem::currentLabel() const {
    int s, d, bonus;
    std::string body = bodyTrickName(startFakie_, s, d);
    std::vector<ActiveTrick> parts = finished_;
    parts.insert(parts.end(), active_.begin(), active_.end());
    return composeName(body, parts, bonus, nullptr);
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
    int score = bodyScore;
    int diff = bodyDiff;
    int n = 0;
    for (auto& a : all) {
        int ts = a.def->score;
        if (a.def->hold) ts = int(float(ts) * (0.6f + std::min(a.holdTime, 1.5f)));  // longer grabs score more
        score += ts;
        diff += a.def->difficulty;
        ++n;
    }
    int bonus = 0;
    out.name = composeName(body, all, bonus, &out.parts);
    score += bonus;
    if (!body.empty()) out.parts.insert(out.parts.begin(), body);
    // combining a body rotation with scooter tricks is worth more than the sum
    if (!body.empty() && n > 0) score = int(float(score) * (1.2f + 0.15f * float(n - 1)));
    else if (n > 1) score = int(float(score) * (1.0f + 0.15f * float(n - 1)));
    out.score = score;
    out.difficulty = diff;
    active_.clear();
    finished_.clear();
    pose_ = ScooterPose{};
    return ok;
}

}  // namespace sw
