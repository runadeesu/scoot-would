#include "game/ui/hud.h"

#include "audio/music.h"
#include "game/game.h"
#include "input/input.h"
#include "save/save_system.h"

#include <cstdio>

namespace sw {

using namespace ui;

namespace {
std::string formatScore(long long v) {
    std::string s = std::to_string(v < 0 ? -v : v), out;
    int n = 0;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        if (n && n % 3 == 0) out.insert(out.begin(), ',');
        out.insert(out.begin(), *it);
        ++n;
    }
    return v < 0 ? "-" + out : out;
}
std::string formatTime(float s) {
    int ms = int(s * 1000.0f);
    char b[32];
    std::snprintf(b, sizeof(b), "%d:%02d.%d", ms / 60000, (ms / 1000) % 60, (ms / 100) % 10);
    return b;
}
bool project(const RenderView& v, const Vec3& p, float W, Vec2& out) {
    Vec4 c = v.viewProj * Vec4(p, 1.0f);
    if (c.w <= 0.05f) return false;
    out = Vec2((c.x / c.w * 0.5f + 0.5f) * W, (1.0f - (c.y / c.w * 0.5f + 0.5f)) * 1080.0f);
    return true;
}
const Vec4 kLandColors[4] = {{0.4f, 1.0f, 0.55f, 1}, {0.95f, 0.95f, 0.95f, 1}, {1.0f, 0.7f, 0.25f, 1}, {1.0f, 0.3f, 0.25f, 1}};
const char* kLandNames[4] = {"CLEAN", "", "SKETCHY", "BAIL"};
}  // namespace

void Hud::reset() {
    popups_.clear();
    bannerAge_ = 10.0f;
    toastAge_ = 10.0f;
    lastCountdown_ = -1;
}

void Hud::toast(const std::string& text, float seconds) {
    toast_ = text;
    toastAge_ = 0.0f;
    toastLife_ = seconds;
}

void Hud::onEvent(const GameEvent& e) {
    switch (e.type) {
        case GameEventType::TrickLanded: {
            Popup p;
            p.text = e.text;
            p.score = e.score;
            p.landing = e.landing;
            popups_.insert(popups_.begin(), p);
            if (popups_.size() > 3) popups_.pop_back();
            break;
        }
        case GameEventType::Land:
            if (e.landing == 2 && !popups_.empty() && popups_.front().age < 0.1f) popups_.front().landing = 2;
            break;
        case GameEventType::ComboBanked:
            if (e.score > 0) {
                banner_ = "+" + formatScore(e.score);
                bannerScore_ = e.score;
                bannerFail_ = false;
                bannerAge_ = 0.0f;
            }
            break;
        case GameEventType::ComboFailed:
            if (e.score > 0) {
                banner_ = "COMBO LOST";
                bannerScore_ = e.score;
                bannerFail_ = true;
                bannerAge_ = 0.0f;
            }
            break;
        case GameEventType::Checkpoint: toast("Checkpoint set", 1.5f); break;
        default: break;
    }
}

void Hud::draw(Context& ui, float dt, const RenderView& view) {
    time_ += dt;
    for (auto& p : popups_) p.age += dt;
    while (!popups_.empty() && popups_.back().age > 2.6f) popups_.pop_back();
    bannerAge_ += dt;
    toastAge_ += dt;
    areaAge_ += dt;
    trackAge_ += dt;
    const GameplaySettings& gs = saves().settings().gameplay;
    // area name + now playing (shown when they change)
    std::string area = game_.currentArea();
    if (area != area_) {
        area_ = area;
        areaAge_ = 0.0f;
    }
    if (const TrackInfo* t = music().current()) {
        if (t->title != track_) {
            track_ = t->title;
            trackAge_ = 0.0f;
        }
    }
    if (!gs.showHud) {
        drawMode(ui, view);
        return;
    }
    drawMode(ui, view);
    drawBalance(ui, view);
    drawCombo(ui);
    if (gs.showTrickNames) drawTrickPopup(ui);
    drawSpeed(ui);
    drawToasts(ui);
}

void Hud::drawCombo(Context& ui) {
    const ComboManager& c = game_.player().combo;
    float W = ui.width();
    if (c.active()) {
        // chain of the last entries
        std::string chain;
        const auto& es = c.entries();
        size_t start = es.size() > 5 ? es.size() - 5 : 0;
        if (start > 0) chain = "... + ";
        for (size_t i = start; i < es.size(); ++i) chain += (i > start ? " + " : "") + es[i].name;
        float cw = ui.measure(chain, 30.0f, FontStyle::Bold);
        float boxW = std::max(420.0f, cw + 60.0f);
        Rect box(W * 0.5f - boxW * 0.5f, 858, boxW, 150);
        ui.rectGradient(box, Vec4(0, 0, 0, 0.0f), Vec4(0, 0, 0, 0.55f), 12.0f);
        ui.text(chain, Vec2(W * 0.5f, box.y + 10), 30.0f, ui.theme().text, FontStyle::Bold, Align::Center, 0.7f);
        char b[64];
        std::snprintf(b, sizeof(b), "%s", formatScore(c.comboScore()).c_str());
        float sw = ui.measure(b, 58.0f, FontStyle::Display);
        char m[32];
        std::snprintf(m, sizeof(m), "x%.1f", c.multiplier());
        float mw = ui.measure(m, 58.0f, FontStyle::Display);
        float x0 = W * 0.5f - (sw + mw + 30) * 0.5f;
        ui.text(b, Vec2(x0, box.y + 48), 58.0f, ui.theme().text, FontStyle::Display, Align::Left, 0.6f);
        ui.text(m, Vec2(x0 + sw + 30, box.y + 48), 58.0f, ui.theme().accent, FontStyle::Display, Align::Left, 0.6f);
        // flow timer
        float t = c.timerMax() > 0 ? saturate(c.timer() / c.timerMax()) : 0.0f;
        bool running = game_.player().state() == PlayerState::Riding;
        Rect bar(W * 0.5f - 160, box.y + 128, 320, 8);
        ui.rect(bar, Vec4(1, 1, 1, 0.18f), 4);
        ui.rect(Rect(bar.x, bar.y, bar.w * (running ? t : 1.0f), bar.h), running && t < 0.35f ? ui.theme().bad : ui.theme().accent, 4);
    }
    if (bannerAge_ < 2.2f) {
        float a = saturate(bannerAge_ * 6.0f) * saturate((2.2f - bannerAge_) * 3.0f);
        float rise = bannerAge_ * 30.0f;
        Vec4 col = bannerFail_ ? ui.theme().bad : ui.theme().good;
        ui.text(banner_, Vec2(W * 0.5f, 770 - rise), 64.0f, col * Vec4(1, 1, 1, a), FontStyle::Display, Align::Center, 0.7f);
        if (bannerFail_) {
            std::string lost = formatScore(bannerScore_);
            ui.text(lost, Vec2(W * 0.5f, 840 - rise), 30.0f, ui.theme().textDim * Vec4(1, 1, 1, a), FontStyle::Bold, Align::Center, 0.6f);
        }
    }
    // total score (top right)
    long long total = game_.modes().inChallenge() ? game_.modes().score() : game_.player().combo.totalScore();
    ui.text(formatScore(total), Vec2(W - 60, 40), 60.0f, ui.theme().text, FontStyle::Display, Align::Right, 0.6f);
    ui.text("SCORE", Vec2(W - 60, 112), 22.0f, ui.theme().textDim, FontStyle::Bold, Align::Right, 0.6f);
}

void Hud::drawTrickPopup(Context& ui) {
    float W = ui.width();
    float y = 250;
    for (size_t i = 0; i < popups_.size(); ++i) {
        const Popup& p = popups_[i];
        float in = saturate(p.age * 8.0f), out = saturate((2.6f - p.age) * 3.0f);
        float a = in * out * (i == 0 ? 1.0f : 0.55f);
        float size = i == 0 ? 64.0f * (1.0f + 0.25f * (1.0f - in)) : 40.0f;
        ui.text(p.text, Vec2(W * 0.5f, y), size, ui.theme().text * Vec4(1, 1, 1, a), FontStyle::Display, Align::Center, 0.7f);
        y += size + 4;
        if (i == 0) {
            char b[64];
            int li = std::clamp(p.landing, 0, 3);
            if (kLandNames[li][0]) std::snprintf(b, sizeof(b), "%s  +%s", kLandNames[li], formatScore(p.score).c_str());
            else std::snprintf(b, sizeof(b), "+%s", formatScore(p.score).c_str());
            ui.text(b, Vec2(W * 0.5f, y), 30.0f, kLandColors[li] * Vec4(1, 1, 1, a), FontStyle::Bold, Align::Center, 0.6f);
            y += 44;
        }
    }
    // live trick name while in the air
    const Player& pl = game_.player();
    if (pl.state() == PlayerState::Air && !pl.tricks.currentLabel().empty()) {
        ui.text(pl.tricks.currentLabel(), Vec2(W * 0.5f, 170), 42.0f, ui.theme().accent, FontStyle::Display, Align::Center, 0.6f);
    }
}

void Hud::drawSpeed(Context& ui) {
    float W = ui.width();
    bool metric = saves().settings().gameplay.metric;
    float v = game_.player().speed() * (metric ? 3.6f : 2.237f);
    speedShown_ = dampf(speedShown_, v, 10.0f, 1.0f / 60.0f);
    Vec2 c(W - 150, 960);
    float t = saturate(speedShown_ / (metric ? 60.0f : 38.0f));
    ui.arc(c, 78, 10, kPi * 0.75f, kPi * 2.25f, Vec4(1, 1, 1, 0.15f));
    ui.arc(c, 78, 10, kPi * 0.75f, kPi * 0.75f + kPi * 1.5f * t, t > 0.8f ? ui.theme().bad : ui.theme().accent);
    char b[16];
    std::snprintf(b, sizeof(b), "%d", int(speedShown_ + 0.5f));
    ui.text(b, Vec2(c.x, c.y - 42), 60.0f, ui.theme().text, FontStyle::Display, Align::Center, 0.6f);
    ui.text(metric ? "KM/H" : "MPH", Vec2(c.x, c.y + 22), 22.0f, ui.theme().textDim, FontStyle::Bold, Align::Center);
    // state line (air time on big airs)
    const Player& p = game_.player();
    if (p.state() == PlayerState::Air && p.airTime() > 0.6f) {
        char a[32];
        std::snprintf(a, sizeof(a), "AIR %.1fs", p.airTime());
        ui.text(a, Vec2(c.x, c.y + 60), 24.0f, ui.theme().info, FontStyle::Bold, Align::Center, 0.6f);
    }
}

void Hud::drawBalance(Context& ui, const RenderView& view) {
    const Player& p = game_.player();
    float W = ui.width();
    Vec2 anchor;
    bool vis = project(view, p.position() + Vec3(0, 2.1f, 0), W, anchor);
    if (!vis) anchor = Vec2(W * 0.5f, 380);
    if (p.state() == PlayerState::Grinding) {
        float b = clampf(p.grind.balance, -1.2f, 1.2f);
        Rect bar(anchor.x - 130, anchor.y - 12, 260, 14);
        ui.rect(bar.shrink(-4), Vec4(0, 0, 0, 0.45f), 9);
        ui.rect(Rect(bar.x + bar.w * 0.35f, bar.y, bar.w * 0.3f, bar.h), Vec4(0.35f, 0.9f, 0.45f, 0.5f), 7);
        float x = bar.x + bar.w * (0.5f + b * 0.5f);
        Vec4 col = std::fabs(b) > 0.75f ? ui.theme().bad : ui.theme().text;
        ui.rect(Rect(x - 4, bar.y - 8, 8, bar.h + 16), col, 3);
    } else if (p.state() == PlayerState::Manual) {
        float b = clampf(p.manual.balance, -1.2f, 1.2f);
        Rect bar(anchor.x + 60, anchor.y - 110, 14, 220);
        ui.rect(bar.shrink(-4), Vec4(0, 0, 0, 0.45f), 9);
        ui.rect(Rect(bar.x, bar.y + bar.h * 0.35f, bar.w, bar.h * 0.3f), Vec4(0.35f, 0.9f, 0.45f, 0.5f), 7);
        float y = bar.y + bar.h * (0.5f - b * 0.5f);
        Vec4 col = std::fabs(b) > 0.75f ? ui.theme().bad : ui.theme().text;
        ui.rect(Rect(bar.x - 8, y - 4, bar.w + 16, 8), col, 3);
    }
    // bail / respawn hint
    if (p.state() == PlayerState::Bailed) {
        ui.text("BAIL", Vec2(W * 0.5f, 420), 90.0f, ui.theme().bad, FontStyle::Display, Align::Center, 0.7f);
        ui.text(p.bailReason(), Vec2(W * 0.5f, 520), 30.0f, ui.theme().text, FontStyle::Bold, Align::Center, 0.6f);
    }
}

void Hud::drawMode(Context& ui, const RenderView& view) {
    ModeManager& mm = game_.modes();
    float W = ui.width();
    if (!mm.inChallenge()) {
        // free ride: area + best combo
        if (areaAge_ < 4.0f && !area_.empty()) {
            float a = saturate(areaAge_ * 3.0f) * saturate((4.0f - areaAge_) * 2.0f);
            ui.text(area_, Vec2(60, 40), 56.0f, ui.theme().text * Vec4(1, 1, 1, a), FontStyle::Display, Align::Left, 0.6f);
            ui.rect(Rect(64, 108, 120 * a, 6), ui.theme().accent * Vec4(1, 1, 1, a), 3);
        }
        int best = game_.player().combo.bestCombo();
        if (best > 0) {
            ui.text("BEST COMBO  " + formatScore(best), Vec2(W - 60, 140), 24.0f, ui.theme().textDim, FontStyle::Bold, Align::Right, 0.6f);
        }
        return;
    }
    const ChallengeDef* d = mm.current();
    // top left: name, timer, objective
    Rect box(40, 30, 520, 150);
    ui.rectGradient(box, Vec4(0, 0, 0, 0.55f), Vec4(0, 0, 0, 0.25f), 12);
    ui.text(d->name, Vec2(box.x + 24, box.y + 14), 34.0f, ui.theme().accent, FontStyle::Bold);
    float t = d->timeLimit > 0 ? mm.remaining() : mm.elapsed();
    Vec4 tc = (d->timeLimit > 0 && t < 10.0f) ? ui.theme().bad : ui.theme().text;
    ui.text(formatTime(t), Vec2(box.x + 24, box.y + 54), 56.0f, tc, FontStyle::Display);
    ui.text(mm.objectiveText(), Vec2(box.x + box.w - 24, box.y + 72), 28.0f, ui.theme().text, FontStyle::Bold, Align::Right);
    // trick list checklist
    if (!d->trickList.empty()) {
        float y = box.y + box.h + 14;
        for (size_t i = 0; i < d->trickList.size(); ++i) {
            bool done = i < mm.tricksDone().size() && mm.tricksDone()[i];
            ui.rect(Rect(box.x + 8, y + 8, 22, 22), done ? ui.theme().good : Vec4(1, 1, 1, 0.2f), 5);
            ui.text(d->trickList[i], Vec2(box.x + 42, y), 28.0f, done ? ui.theme().textDim : ui.theme().text, FontStyle::SemiBold, Align::Left, 0.6f);
            y += 38;
        }
    }
    // gates: marker + distance for the next gate
    if (mm.nextGate() < mm.gateCount() && mm.state() != ModeState::Finished) {
        const Gate& g = d->gates[size_t(mm.nextGate())];
        Vec3 c = g.position + Vec3(0, g.radius, 0);
        Vec2 s;
        float dist = (c - game_.player().position()).length();
        bool onScreen = project(view, c, W, s) && s.x > 40 && s.x < W - 40 && s.y > 40 && s.y < 1040;
        if (onScreen) {
            ui.circle(s, 10, ui.theme().accent);
            char b[32];
            std::snprintf(b, sizeof(b), "%.0f m", dist);
            ui.text(b, Vec2(s.x, s.y + 16), 26.0f, ui.theme().text, FontStyle::Bold, Align::Center, 0.7f);
            if (!g.require.empty()) {
                std::string req = g.require == "air" ? "AIR" : g.require == "grind" ? "GRIND" : "MANUAL";
                ui.text(req, Vec2(s.x, s.y - 50), 26.0f, ui.theme().accent, FontStyle::Bold, Align::Center, 0.7f);
            }
        } else {
            // edge arrow in the direction of the gate
            Vec3 to = (c - view.position);
            float x = dot(to, view.right), z = dot(to, view.forward);
            float ang = std::atan2(x, z);
            Vec2 center(W * 0.5f, 540);
            Vec2 dir(std::sin(ang), -std::cos(ang));
            Vec2 p = center + dir * 420.0f;
            Vec2 n(-dir.y, dir.x);
            ui.polygon({p + dir * 26.0f, p + n * 16.0f, p - n * 16.0f}, ui.theme().accent);
        }
    }
    if (mm.gateMessageTime < 1.6f && !mm.gateMessage.empty())
        ui.text(mm.gateMessage, Vec2(W * 0.5f, 640), 40.0f, ui.theme().bad, FontStyle::Display, Align::Center, 0.7f);
    // countdown
    if (mm.state() == ModeState::Countdown) {
        int n = int(std::ceil(mm.countdown()));
        float frac = mm.countdown() - std::floor(mm.countdown());
        if (n != lastCountdown_) {
            lastCountdown_ = n;
            game_.sound().ui("checkpoint");
        }
        float s = 1.0f + frac * 0.4f;
        ui.text(std::to_string(n), Vec2(W * 0.5f, 380 - 30 * s), 170.0f * s, ui.theme().text, FontStyle::Display, Align::Center, 0.7f);
        ui.paragraph(d->description, Rect(W * 0.5f - 400, 640, 800, 200), 30.0f, ui.theme().text, FontStyle::SemiBold);
    } else if (mm.state() == ModeState::Running && mm.elapsed() < 0.8f) {
        float a = saturate((0.8f - mm.elapsed()) * 3.0f);
        ui.text("GO!", Vec2(W * 0.5f, 300), 170.0f, ui.theme().accent * Vec4(1, 1, 1, a), FontStyle::Display, Align::Center, 0.7f);
        lastCountdown_ = -1;
    }
}

void Hud::drawToasts(Context& ui) {
    float W = ui.width();
    if (toastAge_ < toastLife_) {
        float a = saturate(toastAge_ * 5.0f) * saturate((toastLife_ - toastAge_) * 3.0f);
        ui.text(toast_, Vec2(W * 0.5f, 140), 32.0f, ui.theme().text * Vec4(1, 1, 1, a), FontStyle::Bold, Align::Center, 0.7f);
    }
    if (trackAge_ < 5.0f && !track_.empty()) {
        float a = saturate(trackAge_ * 3.0f) * saturate((5.0f - trackAge_) * 2.0f);
        ui.text("NOW PLAYING", Vec2(60, 960), 20.0f, ui.theme().textDim * Vec4(1, 1, 1, a), FontStyle::Bold, Align::Left, 0.6f);
        ui.text(track_, Vec2(60, 986), 30.0f, ui.theme().text * Vec4(1, 1, 1, a), FontStyle::Bold, Align::Left, 0.6f);
    }
}

}  // namespace sw
