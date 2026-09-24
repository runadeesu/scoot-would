#include "game/modes/mode_manager.h"

#include "assets/asset_manager.h"
#include "core/filesystem.h"
#include "core/log.h"
#include "game/player/player.h"
#include "render/mesh_builder.h"
#include "save/save_system.h"

#include <algorithm>

namespace sw {

const char* modeName(ModeType t) {
    switch (t) {
        case ModeType::FreeRide: return "Free Ride";
        case ModeType::Trick: return "Trick Challenge";
        case ModeType::Line: return "Line Challenge";
        case ModeType::BestTrick: return "Best Trick";
        case ModeType::TimeAttack: return "Time Attack";
        default: return "?";
    }
}

const char* modeDescription(ModeType t) {
    switch (t) {
        case ModeType::FreeRide: return "No rules, no timer. Explore the city and build lines.";
        case ModeType::Trick: return "Score as many points as you can before the timer runs out, or land every trick on the list.";
        case ModeType::Line: return "Ride through every gate in order without bailing. Some gates must be grinded, manualled or jumped.";
        case ModeType::BestTrick: return "Only your single best combo counts. Go big.";
        case ModeType::TimeAttack: return "Race through the checkpoints as fast as you can.";
        default: return "";
    }
}

static ModeType parseMode(const std::string& s) {
    if (s == "trick") return ModeType::Trick;
    if (s == "line") return ModeType::Line;
    if (s == "besttrick") return ModeType::BestTrick;
    if (s == "timeattack") return ModeType::TimeAttack;
    return ModeType::FreeRide;
}

void ModeManager::load() {
    defs_.clear();
    auto j = loadJsonFile(fs::resolve("assets/data/challenges.json"));
    if (!j || !j->contains("challenges")) {
        LOG_WARN("modes: no challenges.json");
        return;
    }
    for (auto& c : (*j)["challenges"]) {
        ChallengeDef d;
        d.id = jget<std::string>(c, "id", "");
        d.name = jget<std::string>(c, "name", d.id);
        d.description = jget<std::string>(c, "desc", "");
        d.map = jget<std::string>(c, "map", "assets/scenes/city.json");
        d.spawn = jget<std::string>(c, "spawn", "");
        d.type = parseMode(jget<std::string>(c, "mode", "trick"));
        d.timeLimit = jget<float>(c, "time", d.type == ModeType::TimeAttack || d.type == ModeType::Line ? 0.0f : 90.0f);
        d.lowerIsBetter = d.type == ModeType::TimeAttack || d.type == ModeType::Line;
        if (c.contains("targets") && c["targets"].is_array() && c["targets"].size() == 3) {
            for (int k = 0; k < 3; ++k) {
                float v = c["targets"][size_t(k)].get<float>();
                d.targets[k] = d.lowerIsBetter ? int(v * 1000.0f) : int(v);  // timed targets are seconds in the file
            }
        }
        if (c.contains("gates"))
            for (auto& g : c["gates"]) {
                Gate gt;
                gt.position = jvec3(g, "pos", Vec3(0));
                gt.radius = jget<float>(g, "radius", 3.0f);
                gt.yaw = jget<float>(g, "yaw", 0.0f);
                gt.require = jget<std::string>(g, "require", "");
                d.gates.push_back(gt);
            }
        if (c.contains("tricks"))
            for (auto& t : c["tricks"]) d.trickList.push_back(t.get<std::string>());
        if (!d.id.empty() && d.type != ModeType::FreeRide) defs_.push_back(d);
    }
    LOG_INFO("modes: %zu challenges", defs_.size());
}

const ChallengeDef* ModeManager::find(const std::string& id) const {
    for (auto& d : defs_)
        if (d.id == id) return &d;
    return nullptr;
}

std::vector<const ChallengeDef*> ModeManager::forMap(const std::string& map, ModeType t) const {
    std::vector<const ChallengeDef*> out;
    for (auto& d : defs_)
        if ((map.empty() || d.map == map) && d.type == t) out.push_back(&d);
    return out;
}

int ModeManager::medalFor(const ChallengeDef& d, int v) {
    int m = 0;
    for (int k = 0; k < 3; ++k) {
        bool ok = d.lowerIsBetter ? (v > 0 && v <= d.targets[k]) : (v >= d.targets[k] && d.targets[k] > 0);
        if (ok) m = k + 1;
    }
    return m;
}

void ModeManager::startFreeRide() {
    def_ = nullptr;
    state_ = ModeState::Idle;
}

void ModeManager::start(const ChallengeDef& def, RenderScene& rs) {
    clearGates(&rs);
    def_ = &def;
    restart(rs);
}

void ModeManager::restart(RenderScene& rs) {
    if (!def_) return;
    state_ = ModeState::Countdown;
    countdown_ = 3.0f;
    elapsed_ = 0.0f;
    score_ = 0;
    bestCombo_ = 0;
    nextGate_ = 0;
    result_ = ModeResult{};
    tricksDone_.assign(def_->trickList.size(), false);
    lastPosValid_ = false;
    gateMessage.clear();
    gateMessageTime = 10.0f;
    clearGates(&rs);
    buildGates(rs);
    refreshGateVisuals(rs);
}

void ModeManager::stop(RenderScene* rs) {
    clearGates(rs);
    def_ = nullptr;
    state_ = ModeState::Idle;
}

void ModeManager::buildGates(RenderScene& rs) {
    if (!def_) return;
    for (const Gate& g : def_->gates) {
        std::string key = "gate_ring_" + std::to_string(int(g.radius * 10.0f));
        auto mesh = assets().getOrCreateMesh(
            key,
            [&]() {
                MeshBuilder b("gate");
                std::vector<Vec3> ring;
                for (int i = 0; i <= 48; ++i) {
                    float a = float(i) / 48.0f * kTwoPi;
                    ring.push_back(Vec3(std::cos(a) * g.radius, std::sin(a) * g.radius, 0));
                }
                b.tube(ring, 0.09f, 8, false);
                // posts down to the ground
                b.cylinder(Vec3(-g.radius, -g.radius - 0.2f, 0), 0.05f, 0.2f, 8, true);
                return b.build();
            },
            false);
        RenderObject o;
        o.mesh = mesh;
        o.materials = {assets().material("gate")};
        o.isStatic = false;
        o.castShadows = false;
        Vec3 c = g.position + Vec3(0, g.radius, 0);
        o.world = Mat4::trs(c, Quat::angleAxis(g.yaw * kDeg2Rad, Vec3(0, 1, 0)), Vec3(1));
        gateHandles_.push_back(rs.add(o));
    }
}

void ModeManager::clearGates(RenderScene* rs) {
    if (rs)
        for (auto h : gateHandles_) rs->remove(h);
    gateHandles_.clear();
}

void ModeManager::refreshGateVisuals(RenderScene& rs) {
    MaterialPtr next = assets().material("gate_next"), later = assets().material("gate");
    for (size_t i = 0; i < gateHandles_.size(); ++i) {
        bool done = int(i) < nextGate_;
        rs.setVisible(gateHandles_[i], !done && int(i) <= nextGate_ + 2);
        rs.setMaterials(gateHandles_[i], {int(i) == nextGate_ ? next : later});
    }
}

std::string ModeManager::objectiveText() const {
    if (!def_) return "";
    char buf[160];
    switch (def_->type) {
        case ModeType::Trick:
            if (!def_->trickList.empty()) {
                int done = 0;
                for (bool b : tricksDone_) done += b ? 1 : 0;
                std::snprintf(buf, sizeof(buf), "Tricks %d / %zu", done, def_->trickList.size());
                return buf;
            }
            std::snprintf(buf, sizeof(buf), "Gold %d", def_->targets[2]);
            return buf;
        case ModeType::BestTrick: std::snprintf(buf, sizeof(buf), "Best combo  %d", bestCombo_); return buf;
        case ModeType::Line:
        case ModeType::TimeAttack: std::snprintf(buf, sizeof(buf), "Gate %d / %zu", std::min(nextGate_ + 1, int(def_->gates.size())), def_->gates.size()); return buf;
        default: return "";
    }
}

void ModeManager::finish(bool completed, const std::string& reason, Player& player) {
    if (state_ == ModeState::Finished || !def_) return;
    player.combo.bank();
    state_ = ModeState::Finished;
    finishedTime_ = 0.0f;
    result_.completed = completed;
    result_.reason = reason;
    if (def_->lowerIsBetter) result_.value = completed ? int(elapsed_ * 1000.0f) : 0;
    else if (def_->type == ModeType::BestTrick) result_.value = bestCombo_;
    else result_.value = score_;
    bool listOk = def_->trickList.empty() || std::all_of(tricksDone_.begin(), tricksDone_.end(), [](bool b) { return b; });
    result_.medal = (completed || !def_->lowerIsBetter) && listOk ? medalFor(*def_, result_.value) : 0;
    // trick lists: completing the list is worth at least bronze
    if (!def_->trickList.empty() && listOk && result_.medal == 0) result_.medal = 1;
    ChallengeRecord& rec = saves().data().challenges[def_->id];
    rec.attempts++;
    bool better = def_->lowerIsBetter ? (result_.value > 0 && (rec.best == 0 || result_.value < rec.best)) : result_.value > rec.best;
    if (better && (completed || !def_->lowerIsBetter)) {
        rec.best = result_.value;
        result_.newBest = true;
    }
    rec.medal = std::max(rec.medal, result_.medal);
    saves().markDirty();
    saves().flush();
    LOG_INFO("modes: '%s' finished (%s) value %d medal %d", def_->name.c_str(), reason.c_str(), result_.value, result_.medal);
}

void ModeManager::update(float dt, Player& player, RenderScene& rs) {
    if (!def_) return;
    gateMessageTime += dt;
    if (state_ == ModeState::Countdown) {
        countdown_ -= dt;
        if (countdown_ <= 0.0f) {
            state_ = ModeState::Running;
            elapsed_ = 0.0f;
            player.combo.reset();
        }
        lastPos_ = player.position();
        lastPosValid_ = true;
        return;
    }
    if (state_ == ModeState::Finished) {
        finishedTime_ += dt;
        return;
    }
    if (state_ != ModeState::Running) return;
    elapsed_ += dt;
    // gates: segment test between the previous and current position
    Vec3 pos = player.position();
    if (!def_->gates.empty() && nextGate_ < int(def_->gates.size()) && lastPosValid_ && player.state() != PlayerState::Bailed) {
        const Gate& g = def_->gates[size_t(nextGate_)];
        Vec3 c = g.position + Vec3(0, g.radius * 0.8f, 0);
        Vec3 cp;
        closestPointOnSegment(lastPos_, pos, c, cp);
        Vec3 d = cp - c;
        float horiz = Vec3(d.x, 0, d.z).length();
        bool inside = horiz < g.radius && cp.y > g.position.y - 0.5f && cp.y < g.position.y + g.radius * 2.0f + 1.0f;
        if (inside) {
            bool ok = true;
            PlayerState st = player.state();
            if (g.require == "air") ok = st == PlayerState::Air;
            else if (g.require == "grind") ok = st == PlayerState::Grinding;
            else if (g.require == "manual") ok = st == PlayerState::Manual;
            if (ok) {
                ++nextGate_;
                player.setCheckpoint();
                gateMessage.clear();
                refreshGateVisuals(rs);
                if (nextGate_ >= int(def_->gates.size())) finish(true, "Finished", player);
            } else if (gateMessageTime > 1.5f) {
                gateMessage = g.require == "air" ? "Jump through this gate!" : g.require == "grind" ? "Grind through this gate!" : "Manual through this gate!";
                gateMessageTime = 0.0f;
            }
        }
    }
    lastPos_ = pos;
    lastPosValid_ = true;
    if (def_->timeLimit > 0.0f && elapsed_ >= def_->timeLimit) finish(def_->gates.empty(), "Time up", player);
    if (def_->type == ModeType::Trick && !def_->trickList.empty() && std::all_of(tricksDone_.begin(), tricksDone_.end(), [](bool b) { return b; }))
        finish(true, "All tricks landed", player);
}

void ModeManager::onEvent(const GameEvent& e, Player& player) {
    if (!def_ || state_ != ModeState::Running) return;
    switch (e.type) {
        case GameEventType::ComboBanked:
            score_ += e.score;
            bestCombo_ = std::max(bestCombo_, e.score);
            break;
        case GameEventType::TrickLanded:
        case GameEventType::GrindStart:
        case GameEventType::ManualStart:
            for (size_t i = 0; i < def_->trickList.size(); ++i)
                if (!tricksDone_[i] && e.text.find(def_->trickList[i]) != std::string::npos) tricksDone_[i] = true;
            break;
        case GameEventType::Bail:
            if (def_->type == ModeType::Line) finish(false, "Bailed", player);
            break;
        default: break;
    }
}

}  // namespace sw
