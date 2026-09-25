// scoot would - data driven trick system
//
// Scooter tricks (whips, bar tricks, flips of the scooter, grabs) are defined in
// assets/data/tricks.json and triggered from buffered right stick flicks / sequences on one of four input
// layers (Scooter Flow layout: RT, LT, RB + RT, RB + LT with the right stick). Holding the stick keeps a whip
// or bar going (double, triple, quad). Body rotations (spins, flips, flairs, corks, barrel rolls) come from
// the physics body's actual angular motion and are classified at landing, then combined with the scooter
// tricks into one name the way riders say it ("360 Whip", "Flair Quad Whip Bar", "540 Flair", "Buttercup",
// "Truck Driver", ...).
#pragma once

#include "core/json.h"
#include "core/math.h"
#include "input/input.h"

#include <string>
#include <vector>

namespace sw {

enum class TrickChannel { Deck = 0, Bars, Rider, Whole, Count };

enum class ScooterAxis { None, Steer, DeckRoll, DeckPitch, Bars, WholePitch, WholeYaw, WholeRoll, BarPitch };

// input layer a trick lives on (Scooter Flow layout: RT, LT, RB + RT, RB + LT; classic: bare flick, RT,
// bumper + flick, bumper + RT)
enum class TrickLayer { Trick = 0, Grab, TrickAlt, GrabAlt, None };

struct TrickRotation {
    ScooterAxis axis = ScooterAxis::Steer;
    float degrees = 360.0f;
    int direction = 1;
    bool rewind = false;  // goes out and back (rewind, x-up)
};

struct TrickDefinition {
    std::string id;
    std::string name;
    std::string category;  // whip, bar, flip, grab, body
    TrickChannel channel = TrickChannel::Deck;
    // input
    std::string inputType = "flick";  // flick | sequence | grab | chain
    std::vector<StickDir> dirs;       // one for flick, several for sequence (classic layout)
    std::vector<StickDir> flowDirs;   // Scooter Flow layout: right stick directions on the trick's layer
    TrickLayer layer = TrickLayer::Trick;
    // rules
    float minAirtime = 0.3f;  // expected remaining air time needed to finish
    float duration = 0.4f;
    std::vector<TrickRotation> rotations;  // what the scooter does (several at once: full whip = whip + bar)
    float degrees = 0.0f;     // main rotation amount (chaining keeps the angle done)
    std::string animation;    // rider pose hint
    std::string shortName;    // name inside a combination ("Whip", "Double Whip", "Bar")
    bool feetOff = false, handsOff = false, oneHand = false, frontFootOff = false, backFootOff = false;
    int score = 100;
    int difficulty = 1;
    float cancelWindow = 0.08f;  // landing tolerance at the end of the rotation (seconds)
    std::string chainTo;         // repeating (or holding) the input during the trick upgrades to this trick
    bool hold = false;           // grabs: active while the grab button is held
    std::string family;          // whip, heel, bar, bri, inward, ... (named combinations)
};

struct ActiveTrick {
    const TrickDefinition* def = nullptr;
    float time = 0.0f;
    float progress = 0.0f;     // 0..1 rotation progress
    float startAir = 0.0f;     // air time when it started (combination order)
    int direction = 1;         // mirror (classic layout: left side inputs spin the other way)
    bool done = false;
    float holdTime = 0.0f;     // grabs
    bool released = false;
};

// what happened during one airtime / grind / manual segment
struct TrickResult {
    std::string name;
    int score = 0;
    int difficulty = 0;
    std::vector<std::string> parts;
};

struct ScooterPose {
    // rotations applied to the scooter visual relative to the rider (radians)
    float deckSteer = 0.0f;   // deck around the steer axis (tailwhip)
    float deckRoll = 0.0f;    // deck around its long axis (bri flip, fingerwhip)
    float deckPitch = 0.0f;   // deck around the lateral axis
    float bars = 0.0f;        // bars around the steer axis (barspin)
    float wholePitch = 0.0f;  // whole scooter flips (front scoot)
    float wholeYaw = 0.0f;
    float wholeRoll = 0.0f;   // turndown / canvert tilt
    float barPitch = 0.0f;    // whole scooter around the grips' axis, hands stay on (bri flip, inward)
    bool feetOff = false, handsOff = false, oneHand = false, frontFootOff = false, backFootOff = false;
    std::string riderPose;    // current grab / trick pose name
    float riderPoseWeight = 0.0f;
    float scooterAway = 0.0f; // 0..1 how far the scooter is pushed away from the body (kickless)
};

class TrickSystem {
public:
    bool loadDefinitions(const std::string& absPath);
    // Scooter Flow layout: tricks need the trick / grab modifier and use flowDirs
    void setScheme(bool flow) { flow_ = flow; }
    bool flowScheme() const { return flow_; }
    const std::vector<TrickDefinition>& definitions() const { return defs_; }
    const TrickDefinition* find(const std::string& id) const;

    // start of an airtime
    void beginAir(float predictedAirtime, bool fakie);
    // per physics step while airborne
    // grabAxis / trickAxis: LT / RT held amount, altHeld: a bumper is held (second layer)
    void airUpdate(float dt, std::deque<FlickEvent>& flicks, double now, float grabAxis, float trickAxis, bool altHeld, StickDir rightDir,
                   const Vec3& angVel, const Vec3& bodyRight, const Vec3& bodyForward, float timeToLand);
    // landing: returns whether every scooter trick completed (else bail)
    bool land(TrickResult& out, bool landedFakie);
    void cancel();  // bail / respawn

    bool inAir() const { return inAir_; }
    const ScooterPose& pose() const { return pose_; }
    const std::vector<ActiveTrick>& active() const { return active_; }
    std::string currentLabel() const;     // live display while airborne
    float spinDegrees() const { return spin_ * kRad2Deg; }
    float flipDegrees() const { return flip_ * kRad2Deg; }
    float rollDegrees() const { return roll_ * kRad2Deg; }
    float completion() const;             // lowest progress of the active scooter tricks
    std::string lastStarted;              // for UI / audio
    bool startedThisStep = false;
    std::string bodyTrickName(bool fakie, int& score, int& difficulty) const;
    // layer of a flick / held state for the current control layout
    TrickLayer layerFor(bool grab, bool trick, bool alt) const;

private:
    bool tryStart(const TrickDefinition& d, int direction, double now);
    void updatePose(float dt);
    // riders' names for the combination: body trick + scooter tricks in order, named combos, short names
    std::string composeName(const std::string& body, std::vector<ActiveTrick> parts, int& bonusScore, std::vector<std::string>* outParts) const;
    void upgrade(ActiveTrick& a, const TrickDefinition* up);

    std::vector<TrickDefinition> defs_;
    std::vector<ActiveTrick> active_;
    std::vector<ActiveTrick> finished_;  // completed during this air
    ScooterPose pose_;
    bool inAir_ = false;
    bool flow_ = true;
    bool startFakie_ = false;
    float spin_ = 0.0f, flip_ = 0.0f, roll_ = 0.0f;
    float airTime_ = 0.0f;
    float predicted_ = 0.0f;
    float grabCooldown_ = 0.0f;
};

}  // namespace sw
