#include "game/autotest.h"
#include "core/log.h"

#include <algorithm>

namespace sw {

bool Autotest::load(const std::string& absPath) {
    auto j = loadJsonFile(absPath);
    if (!j) return false;
    name_ = jget<std::string>(*j, "name", "test");
    map_ = jget<std::string>(*j, "map", "assets/scenes/testpark.json");
    spawn_ = jvec3(*j, "spawn", Vec3(0, 0, 0));
    yaw_ = jget<float>(*j, "yaw", 0.0f);
    spawnVel_ = jvec3(*j, "velocity", Vec3(0));
    duration_ = jget<float>(*j, "duration", 5.0f);
    cameraMode = jget<std::string>(*j, "camera", "");
    flow_ = jget<std::string>(*j, "scheme", "classic") == "flow";
    for (auto& s : (*j)["steps"]) {
        if (s.contains("when"))
            conditional_.push_back({s.value("t", 0.0f), s});
        else
            steps_.push_back({s.value("t", 0.0f), s});
    }
    conditionalFired_.assign(conditional_.size(), false);
    std::sort(steps_.begin(), steps_.end(), [](const Step& a, const Step& b) { return a.t < b.t; });
    for (auto& e : j->value("expect", Json::array())) {
        Expect ex;
        ex.t = e.value("t", -1.0f);
        ex.cond = e;
        expects_.push_back(ex);
    }
    for (auto& s : j->value("screenshots", Json::array())) screenshotTimes.push_back(s.get<float>());
    return true;
}

void Autotest::apply(const Json& c, std::deque<FlickEvent>& flicks, double gameTime) {
    if (c.contains("move")) move_ = jvec2(c, "move");
    if (c.contains("look")) look_ = jvec2(c, "look");
    if (c.contains("brake")) brake_ = c["brake"].get<float>();
    if (c.contains("grab")) grab_ = c["grab"].get<float>();
    if (c.contains("trick")) trick_ = c["trick"].get<float>();
    if (c.contains("spin")) {
        float s = c["spin"].get<float>();
        spinL_ = s < 0;
        spinR_ = s > 0;
    }
    if (c.value("push", false)) pushPulse_ = true;
    if (c.value("revert", false)) revertPulse_ = true;
    if (c.value("respawn", false)) respawnPulse_ = true;
    if (c.contains("jump")) {
        std::string js = c["jump"].get<std::string>();
        if (js == "press") {
            jumpHeld_ = true;
            jumpPressPulse_ = true;
        } else if (js == "release") {
            jumpHeld_ = false;
            jumpReleasePulse_ = true;
        }
    }
    if (c.contains("flick")) {
        FlickEvent f;
        f.dir = stickDirFromName(c["flick"].get<std::string>());
        f.time = gameTime;
        f.modifierGrab = grab_ > 0.3f;
        f.modifierTrick = trick_ > 0.3f;
        flicks.push_back(f);
    }
}

static bool conditionMet(const Json& w, const Player& p, double t) {
    Vec3 pos = p.position();
    if (w.contains("t") && t < w["t"].get<float>()) return false;
    if (w.contains("zBelow") && !(pos.z < w["zBelow"].get<float>())) return false;
    if (w.contains("zAbove") && !(pos.z > w["zAbove"].get<float>())) return false;
    if (w.contains("xBelow") && !(pos.x < w["xBelow"].get<float>())) return false;
    if (w.contains("xAbove") && !(pos.x > w["xAbove"].get<float>())) return false;
    if (w.contains("yAbove") && !(pos.y > w["yAbove"].get<float>())) return false;
    if (w.contains("yBelow") && !(pos.y < w["yBelow"].get<float>())) return false;
    if (w.contains("vyBelow") && !(p.velocity().y < w["vyBelow"].get<float>())) return false;
    if (w.contains("state") && playerStateName(p.state()) != w["state"].get<std::string>()) return false;
    if (w.contains("airAbove") && !(p.airTime() > w["airAbove"].get<float>())) return false;
    if (w.contains("spinAbove") && !(std::fabs(p.tricks.spinDegrees()) > w["spinAbove"].get<float>())) return false;
    if (w.contains("flipAbove") && !(std::fabs(p.tricks.flipDegrees()) > w["flipAbove"].get<float>())) return false;
    return true;
}

PlayerInput Autotest::input(double t, std::deque<FlickEvent>& flicks, double gameTime, const Player& p) {
    pushPulse_ = jumpPressPulse_ = jumpReleasePulse_ = revertPulse_ = respawnPulse_ = false;
    while (nextStep_ < steps_.size() && steps_[nextStep_].t <= t) {
        apply(steps_[nextStep_].cmd, flicks, gameTime);
        ++nextStep_;
    }
    for (size_t i = 0; i < conditional_.size(); ++i) {
        if (conditionalFired_[i]) continue;
        if (conditionMet(conditional_[i].cmd["when"], p, t)) {
            conditionalFired_[i] = true;
            apply(conditional_[i].cmd, flicks, gameTime);
        }
    }
    PlayerInput in;
    in.move = move_;
    in.look = look_;
    in.jumpDown = jumpHeld_;
    in.jumpPressed = jumpPressPulse_;
    in.jumpReleased = jumpReleasePulse_;
    in.pushPressed = pushPulse_;
    in.brake = brake_;
    in.grab = grab_;
    in.spinLeft = spinL_;
    in.spinRight = spinR_;
    in.revertPressed = revertPulse_;
    in.respawnPressed = respawnPulse_;
    in.flow = flow_;
    in.trickMod = trick_;
    return in;
}

void Autotest::observe(double t, float dt, Player& p) {
    (void)dt;
    for (auto& e : p.events()) {
        if (e.type == GameEventType::TrickLanded) landedTricks_.push_back(e.text);
        if (e.type == GameEventType::GrindStart) grinds_.push_back(e.text);
        if (e.type == GameEventType::ManualStart) manuals_.push_back(e.text);
        if (e.type == GameEventType::Land) landings_.push_back(e.text);
        if (e.type == GameEventType::Bail) ++bails_;
    }
    maxSpeed_ = std::max(maxSpeed_, p.speed());
    maxAir_ = std::max(maxAir_, p.airTime());
    maxHeight_ = std::max(maxHeight_, p.position().y);
    if (t - lastLog_ >= (p.state() == PlayerState::Air ? 0.083 : 0.25)) {
        lastLog_ = t;
        char buf[512];
        Vec3 pos = p.position(), v = p.velocity();
        Vec3 fw = p.scooter.valid() ? p.scooter.forward() : Vec3(0);
        snprintf(buf, sizeof(buf), "t=%5.2f %-8s pos(%6.2f %5.2f %6.2f) v(%5.2f %5.2f %5.2f) spd=%5.2f air=%4.2f fwd(%5.2f %5.2f %5.2f) spin=%4.0f flip=%4.0f crouch=%.2f trick='%s' %s",
                 t, playerStateName(p.state()), pos.x, pos.y, pos.z, v.x, v.y, v.z, p.speed(), p.airTime(), fw.x, fw.y, fw.z, p.tricks.spinDegrees(), p.tricks.flipDegrees(), p.crouch(),
                 p.tricks.inAir() ? p.tricks.currentLabel().c_str() : "", p.state() == PlayerState::Grinding ? grindInfo(p.grind.type).name : "");
        log_.push_back(buf);
        LOG_INFO("autotest %s", buf);
    }
    for (auto& e : expects_) {
        if (e.checked || e.t < 0.0f || t < e.t) continue;
        e.pass = check(e.cond, p, e.detail);
        e.checked = true;
    }
}

bool Autotest::check(const Json& c, Player& p, std::string& detail) {
    bool ok = true;
    char buf[256];
    auto fail = [&](const char* fmt, auto... args) {
        snprintf(buf, sizeof(buf), fmt, args...);
        detail += std::string(buf) + "; ";
        ok = false;
    };
    if (c.contains("speedMin") && p.speed() < c["speedMin"].get<float>()) fail("speed %.2f < %.2f", p.speed(), c["speedMin"].get<float>());
    if (c.contains("speedMax") && p.speed() > c["speedMax"].get<float>()) fail("speed %.2f > %.2f", p.speed(), c["speedMax"].get<float>());
    if (c.contains("state") && playerStateName(p.state()) != c["state"].get<std::string>())
        fail("state %s != %s", playerStateName(p.state()), c["state"].get<std::string>().c_str());
    if (c.contains("stateNot") && playerStateName(p.state()) == c["stateNot"].get<std::string>()) fail("state is %s", playerStateName(p.state()));
    if (c.value("noBail", false) && bails_ > 0) fail("bailed %d times (%s)", bails_, p.bailReason().c_str());
    if (c.value("bail", false) && bails_ == 0) fail("expected a bail");
    if (c.contains("maxAirMin") && maxAir_ < c["maxAirMin"].get<float>()) fail("max air %.2f < %.2f", maxAir_, c["maxAirMin"].get<float>());
    if (c.contains("heightMin") && maxHeight_ < c["heightMin"].get<float>()) fail("max height %.2f < %.2f", maxHeight_, c["heightMin"].get<float>());
    if (c.contains("maxSpeedMin") && maxSpeed_ < c["maxSpeedMin"].get<float>()) fail("max speed %.2f < %.2f", maxSpeed_, c["maxSpeedMin"].get<float>());
    if (c.contains("posMin")) {
        Vec3 m = jvec3(c, "posMin");
        Vec3 pp = p.position();
        if (pp.x < m.x || pp.y < m.y || pp.z < m.z) fail("position (%.2f %.2f %.2f) below min", pp.x, pp.y, pp.z);
    }
    if (c.contains("posMax")) {
        Vec3 m = jvec3(c, "posMax");
        Vec3 pp = p.position();
        if (pp.x > m.x || pp.y > m.y || pp.z > m.z) fail("position (%.2f %.2f %.2f) above max", pp.x, pp.y, pp.z);
    }
    auto contains = [](const std::vector<std::string>& v, const std::string& s) {
        for (auto& x : v)
            if (x.find(s) != std::string::npos) return true;
        return false;
    };
    if (c.contains("trick") && !contains(landedTricks_, c["trick"].get<std::string>())) fail("trick '%s' not landed", c["trick"].get<std::string>().c_str());
    if (c.contains("grind") && !contains(grinds_, c["grind"].get<std::string>())) fail("grind '%s' not started", c["grind"].get<std::string>().c_str());
    if (c.contains("manual") && !contains(manuals_, c["manual"].get<std::string>())) fail("manual '%s' not started", c["manual"].get<std::string>().c_str());
    if (c.contains("landing") && !contains(landings_, c["landing"].get<std::string>())) fail("landing '%s' not seen", c["landing"].get<std::string>().c_str());
    if (c.contains("comboMin") && p.combo.totalScore() + p.combo.comboTotal() < c["comboMin"].get<int>())
        fail("score %lld < %d", p.combo.totalScore() + p.combo.comboTotal(), c["comboMin"].get<int>());
    return ok;
}

bool Autotest::report(Player& p) {
    bool pass = true;
    for (auto& e : expects_) {
        if (!e.checked) {
            e.detail.clear();
            e.pass = check(e.cond, p, e.detail);
            e.checked = true;
        }
        if (!e.pass) pass = false;
    }
    LOG_INFO("==== autotest '%s' : %s ====", name_.c_str(), pass ? "PASS" : "FAIL");
    std::string tricks;
    for (auto& t : landedTricks_) tricks += "[" + t + "] ";
    std::string gr;
    for (auto& t : grinds_) gr += "[" + t + "] ";
    std::string lands;
    for (auto& t : landings_) lands += t + " ";
    LOG_INFO("  tricks: %s", tricks.c_str());
    LOG_INFO("  grinds: %s", gr.c_str());
    LOG_INFO("  landings: %s  bails: %d  max speed %.2f  max air %.2f  max y %.2f", lands.c_str(), bails_, maxSpeed_, maxAir_, maxHeight_);
    for (auto& e : expects_)
        if (!e.pass) LOG_INFO("  FAILED (t=%.2f): %s", e.t, e.detail.c_str());
    return pass;
}

}  // namespace sw
