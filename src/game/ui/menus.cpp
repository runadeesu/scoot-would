#include "game/ui/menus.h"

#include "audio/music.h"
#include "core/engine.h"
#include "core/filesystem.h"
#include "core/log.h"
#include "game/game.h"
#include "input/input.h"
#include "save/save_system.h"

#include <cstdio>

namespace sw {

using namespace ui;
using glyphs::Hint;

namespace {

const Vec4 kMedalColors[4] = {{0.4f, 0.4f, 0.45f, 1}, {0.8f, 0.5f, 0.2f, 1}, {0.78f, 0.8f, 0.84f, 1}, {1.0f, 0.82f, 0.2f, 1}};

struct NamedColor {
    const char* name;
    Vec3 c;
};
// clothing / scooter palettes (linear colors)
const std::vector<NamedColor>& clothPalette() {
    static const std::vector<NamedColor> p = {
        {"Signal Red", {0.85f, 0.2f, 0.18f}},  {"Black", {0.03f, 0.03f, 0.035f}},    {"White", {0.9f, 0.9f, 0.88f}},
        {"Heather Grey", {0.35f, 0.36f, 0.38f}}, {"Navy", {0.05f, 0.08f, 0.2f}},     {"Royal Blue", {0.08f, 0.22f, 0.7f}},
        {"Sky", {0.35f, 0.62f, 0.9f}},          {"Forest", {0.07f, 0.25f, 0.1f}},    {"Olive", {0.25f, 0.28f, 0.1f}},
        {"Mustard", {0.8f, 0.55f, 0.05f}},      {"Orange", {0.95f, 0.35f, 0.05f}},   {"Pink", {0.9f, 0.4f, 0.55f}},
        {"Purple", {0.3f, 0.1f, 0.5f}},         {"Sand", {0.6f, 0.5f, 0.35f}},       {"Denim", {0.18f, 0.22f, 0.32f}},
        {"Washed Denim", {0.38f, 0.45f, 0.58f}}};
    return p;
}
const std::vector<NamedColor>& skinPalette() {
    static const std::vector<NamedColor> p = {{"Tone 1", {0.74f, 0.5f, 0.38f}},  {"Tone 2", {0.58f, 0.36f, 0.25f}}, {"Tone 3", {0.42f, 0.24f, 0.15f}},
                                              {"Tone 4", {0.27f, 0.145f, 0.085f}}, {"Tone 5", {0.16f, 0.085f, 0.05f}}, {"Tone 6", {0.085f, 0.045f, 0.028f}}};
    return p;
}
const std::vector<NamedColor>& metalPalette() {
    static const std::vector<NamedColor> p = {
        {"Raw", {0.85f, 0.85f, 0.88f}},     {"Black", {0.03f, 0.03f, 0.035f}}, {"White", {0.9f, 0.9f, 0.9f}},     {"Gold", {0.9f, 0.65f, 0.2f}},
        {"Red", {0.75f, 0.06f, 0.05f}},     {"Blue", {0.05f, 0.2f, 0.75f}},    {"Teal", {0.05f, 0.55f, 0.5f}},    {"Neon Green", {0.35f, 0.9f, 0.1f}},
        {"Purple", {0.35f, 0.08f, 0.6f}},   {"Orange", {0.95f, 0.35f, 0.03f}}, {"Pink", {0.9f, 0.3f, 0.55f}},     {"Oil Slick", {0.25f, 0.12f, 0.4f}}};
    return p;
}

int nearestIndex(const std::vector<NamedColor>& pal, const Vec3& c) {
    int best = 0;
    float bd = 1e9f;
    for (size_t i = 0; i < pal.size(); ++i) {
        float d = (pal[i].c - c).lengthSq();
        if (d < bd) {
            bd = d;
            best = int(i);
        }
    }
    return best;
}
void cycleColor(const std::vector<NamedColor>& pal, Vec3& c, int d) {
    int i = nearestIndex(pal, c);
    i = (i + d + int(pal.size())) % int(pal.size());
    c = pal[size_t(i)].c;
}
std::string colorName(const std::vector<NamedColor>& pal, const Vec3& c) { return pal[size_t(nearestIndex(pal, c))].name; }

std::string padName(SDL_GamepadButton b) {
    switch (b) {
        case SDL_GAMEPAD_BUTTON_SOUTH: return "A";
        case SDL_GAMEPAD_BUTTON_EAST: return "B";
        case SDL_GAMEPAD_BUTTON_WEST: return "X";
        case SDL_GAMEPAD_BUTTON_NORTH: return "Y";
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LB";
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB";
        case SDL_GAMEPAD_BUTTON_START: return "Start";
        case SDL_GAMEPAD_BUTTON_BACK: return "Back";
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "LS";
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "RS";
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-Up";
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-Down";
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-Left";
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-Right";
        default: return SDL_GetGamepadStringForButton(b) ? SDL_GetGamepadStringForButton(b) : "?";
    }
}

std::string bindingText(const Binding& b, bool pad) {
    std::string s;
    auto add = [&](const std::string& t) { s += (s.empty() ? "" : " / ") + t; };
    if (pad) {
        for (auto x : b.buttons) add(padName(x));
        for (auto a : b.axisButtons) add(a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? "LT" : "RT");
    } else {
        for (auto k : b.keys) add(SDL_GetScancodeName(k));
        for (int m : b.mouseButtons) add(m == SDL_BUTTON_LEFT ? "Mouse 1" : m == SDL_BUTTON_RIGHT ? "Mouse 2" : "Mouse 3");
    }
    return s.empty() ? "-" : s;
}

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
std::string formatTime(int ms) {
    char b[32];
    std::snprintf(b, sizeof(b), "%d:%02d.%02d", ms / 60000, (ms / 1000) % 60, (ms / 10) % 100);
    return b;
}
std::string valueText(const ChallengeDef& d, int v) { return d.lowerIsBetter ? (v > 0 ? formatTime(v) : "--") : formatScore(v); }

}  // namespace

// ---------------------------------------------------------------------------------------------

void Menus::open(Screen s, bool push) {
    if (push && screen_ != Screen::None && screen_ != s && screen_ != Screen::Loading) stack_.push_back(screen_);
    if (s == Screen::Scooter) {
        shopFocus_ = -1;
        shopPreviewKey_ = -1;
    }
    screen_ = s;
    screenTime_ = 0.0f;
    scroll_ = 0.0f;
    rebindAction_ = -1;
    context().resetFocus(0);
}

void Menus::back() {
    game_.sound().ui("ui_back");
    if (screen_ == Screen::Scooter) {
        // drop the unconfirmed preview: back to the equipped setup
        game_.applyCustomization();
        shopFocus_ = -1;
        shopPreviewKey_ = -1;
    }
    if (screen_ == Screen::Rider || screen_ == Screen::Scooter) {
        saves().markDirty();
        saves().flush();
    }
    if (screen_ == Screen::Settings) saves().saveSettings();
    if (stack_.empty()) {
        if (screen_ == Screen::Pause) {
            close();
            game_.setPaused(false);
        }
        return;
    }
    screen_ = stack_.back();
    stack_.pop_back();
    screenTime_ = 0.0f;
    context().resetFocus(0);
}

void Menus::close() {
    screen_ = Screen::None;
    stack_.clear();
}

void Menus::requestLoad(const std::string& map, const std::string& spawn, const std::string& challenge) {
    pendingMap_ = map;
    pendingSpawn_ = spawn;
    pendingChallenge_ = challenge;
    loadingFrames_ = 0;
    screen_ = Screen::Loading;
    stack_.clear();
}

void Menus::performPendingLoad() {
    // wait for the loading screen to be presented once
    if (pendingMap_.empty() || ++loadingFrames_ < 3) return;
    std::string map = pendingMap_, spawn = pendingSpawn_, ch = pendingChallenge_;
    pendingMap_.clear();
    if (map != game_.mapPath()) game_.loadMap(map);
    close();
    if (!ch.empty()) {
        game_.startChallenge(ch);
    } else {
        if (!spawn.empty()) game_.spawnAt(spawn);
        saves().data().lastMap = map;
        saves().data().lastSpawn = spawn;
        saves().markDirty();
        game_.startFreeRide();
    }
}

void Menus::customizationChanged() {
    game_.applyCustomization();
    saves().markDirty();
}

void Menus::settingsChanged(bool video) { game_.applySettings(video); }

// --- shared pieces --------------------------------------------------------------------------------

void Menus::header(Context& ui, const std::string& title, const std::string& subtitle) {
    ui.rectGradient(Rect(0, 0, ui.width(), 230), Vec4(0, 0, 0, 0.8f), Vec4(0, 0, 0, 0.0f));
    ui.rect(Rect(90, 70, 10, 64), ui.theme().accent, 2);
    ui.text(title, Vec2(118, 48), 80.0f, ui.theme().text, FontStyle::Display, Align::Left, 0.4f);
    if (!subtitle.empty()) ui.text(subtitle, Vec2(122, 138), 26.0f, ui.theme().text, FontStyle::SemiBold, Align::Left, 0.9f);
}

void Menus::footer(Context& ui, const std::vector<glyphs::Hint>& hints, const std::string& note) {
    ui.rectGradient(Rect(0, 990, ui.width(), 90), Vec4(0, 0, 0, 0.0f), Vec4(0, 0, 0, 0.7f));
    if (!note.empty()) ui.text(note, Vec2(ui.width() - 60, 1026), 24.0f, ui.theme().text, FontStyle::Bold, Align::Right);
    else glyphs::hintRow(ui, hints, ui.width() - 56, 1041);
    if (toastTime_ < 3.0f && !toast_.empty()) ui.text(toast_, Vec2(60, 1026), 24.0f, ui.theme().accent, FontStyle::Bold);
}

void Menus::medal(Context& ui, Vec2 c, float r, int m) {
    Vec4 col = kMedalColors[std::clamp(m, 0, 3)];
    if (m == 0) {
        ui.arc(c, r * 0.8f, 3.0f, 0, kTwoPi, col);
        return;
    }
    ui.circle(c, r, col * Vec4(0.6f, 0.6f, 0.6f, 1.0f));
    ui.circle(c, r * 0.8f, col);
    ui.circle(c + Vec2(-r * 0.25f, -r * 0.25f), r * 0.25f, Vec4(1, 1, 1, 0.35f));
}

// --- screens -------------------------------------------------------------------------------------

void Menus::draw(Context& ui, float dt) {
    screenTime_ += dt;
    toastTime_ += dt;
    const NavInput& nav = ui.nav();
    // rebinding swallows navigation
    if (screen_ == Screen::Settings && rebindAction_ >= 0) {
        drawSettings(ui);
        return;
    }
    if (nav.back && screen_ != Screen::Main && screen_ != Screen::Loading && screen_ != Screen::Results && screenTime_ > 0.1f) {
        back();
        return;
    }
    switch (screen_) {
        case Screen::Main: drawMain(ui); break;
        case Screen::Play: drawPlay(ui); break;
        case Screen::Challenges: drawChallenges(ui); break;
        case Screen::Map: drawMap(ui); break;
        case Screen::Rider: drawRider(ui); break;
        case Screen::Scooter: drawScooter(ui); break;
        case Screen::Settings: drawSettings(ui); break;
        case Screen::Pause: drawPause(ui); break;
        case Screen::Results: drawResults(ui); break;
        case Screen::Quit: drawQuit(ui); break;
        case Screen::Loading: drawLoading(ui); break;
        default: break;
    }
    if (ui.movedFocus()) game_.sound().ui("ui_move");
    if (ui.activated()) game_.sound().ui("ui_select");
}

void Menus::drawMain(Context& ui) {
    float W = ui.width();
    float in = saturate(screenTime_ * 3.0f);
    ui.rectGradient(Rect(0, 0, 820, 1080), Vec4(0.02f, 0.02f, 0.03f, 0.88f), Vec4(0.02f, 0.02f, 0.03f, 0.55f));
    ui.rect(Rect(820, 0, 6, 1080), Vec4(ui.theme().accent.x, ui.theme().accent.y, ui.theme().accent.z, 0.8f));
    // logo
    float lx = 96 - (1.0f - in) * 60.0f;
    ui.text("scoot", Vec2(lx, 70), 150.0f, ui.theme().text * Vec4(1, 1, 1, in), FontStyle::Display, Align::Left, 0.5f);
    ui.text("would", Vec2(lx + 250, 178), 150.0f, ui.theme().accent * Vec4(1, 1, 1, in), FontStyle::Display, Align::Left, 0.5f);
    ui.text("FREESTYLE SCOOTER", Vec2(lx + 6, 342), 26.0f, ui.theme().textDim * Vec4(1, 1, 1, in), FontStyle::Bold);

    struct Item {
        const char* label;
        const char* hint;
    };
    const Item items[] = {{"PLAY", "Free ride or take on challenges"}, {"MAP", "Pick a district"},     {"RIDER", "Clothes and colors"},
                          {"SCOOTER", "Build your setup"},            {"SETTINGS", "Graphics, controls, audio"}, {"EXIT", "Quit to desktop"}};
    float y = 420;
    for (int i = 0; i < 6; ++i) {
        if (ui.menuItem(items[i].label, Rect(96, y, 560, 84), "")) {
            switch (i) {
                case 0: open(Screen::Play); break;
                case 1: open(Screen::Map); break;
                case 2: open(Screen::Rider); break;
                case 3: open(Screen::Scooter); break;
                case 4: open(Screen::Settings); break;
                case 5: open(Screen::Quit); break;
            }
            return;
        }
        if (ui.lastFocused()) ui.text(items[i].hint, Vec2(870, y + 26), 28.0f, ui.theme().text, FontStyle::SemiBold, Align::Left, 0.8f);
        y += 88;
    }
    // location + progress
    const SaveData& sd = saves().data();
    std::string where = game_.currentMap() ? game_.currentMap()->name : "";
    std::string area = game_.currentArea();
    if (!area.empty()) where += "  /  " + area;
    ui.text(where, Vec2(100, 960), 26.0f, ui.theme().textDim, FontStyle::SemiBold);
    float mx = 100;
    for (int m = 3; m >= 1; --m) {
        medal(ui, Vec2(mx + 14, 1016), 13, m);
        ui.text(std::to_string(sd.medals(m) - (m < 3 ? sd.medals(m + 1) : 0)), Vec2(mx + 36, 1000), 26.0f, ui.theme().text, FontStyle::Bold);
        mx += 90;
    }
    if (const TrackInfo* t = music().current())
        ui.text(std::string("Now playing: ") + t->title, Vec2(W - 60, 960), 24.0f, ui.theme().textDim, FontStyle::SemiBold, Align::Right, 0.6f);
    ui.text("v" SCOOT_VERSION, Vec2(W - 60, 1010), 22.0f, ui.theme().textDim * Vec4(1, 1, 1, 0.6f), FontStyle::Regular, Align::Right);
    if (ui.nav().back && screenTime_ > 0.2f) open(Screen::Quit);
}

void Menus::drawPlay(Context& ui) {
    header(ui, "PLAY", game_.currentMap() ? game_.currentMap()->name + "  /  " + game_.currentArea() : "");
    ui.rectGradient(Rect(0, 170, 900, 910), Vec4(0, 0, 0, 0.6f), Vec4(0, 0, 0, 0.3f));
    const ModeType types[] = {ModeType::FreeRide, ModeType::Trick, ModeType::Line, ModeType::BestTrick, ModeType::TimeAttack};
    float y = 230;
    for (ModeType t : types) {
        std::string label = modeName(t);
        for (auto& ch : label) ch = char(std::toupper(ch));
        bool act = ui.menuItem(label, Rect(96, y, 700, 84));
        if (ui.lastFocused()) {
            Rect info(960, 240, std::min(820.0f, ui.width() - 1020), 380);
            ui.shadow(info, 14, 18, 0.4f);
            ui.rect(info, ui.theme().panel, 14);
            ui.text(modeName(t), Vec2(info.x + 36, info.y + 30), 52.0f, ui.theme().accent, FontStyle::Display);
            ui.paragraph(modeDescription(t), Rect(info.x + 38, info.y + 110, info.w - 76, 200), 28.0f, ui.theme().text);
            if (t != ModeType::FreeRide) {
                auto list = game_.modes().forMap("", t);
                int gold = 0, any = 0;
                for (auto* d : list) {
                    auto it = saves().data().challenges.find(d->id);
                    if (it != saves().data().challenges.end()) {
                        gold += it->second.medal == 3;
                        any += it->second.medal > 0;
                    }
                }
                char b[96];
                std::snprintf(b, sizeof(b), "%zu challenges   %d completed   %d gold", list.size(), any, gold);
                ui.text(b, Vec2(info.x + 38, info.y + info.h - 64), 26.0f, ui.theme().textDim, FontStyle::SemiBold);
            }
        }
        if (act) {
            if (t == ModeType::FreeRide) {
                close();
                game_.startFreeRide();
            } else {
                listType_ = t;
                open(Screen::Challenges);
            }
            return;
        }
        y += 92;
    }
    footer(ui, {Hint::act(Action::Confirm, "SELECT"), Hint::act(Action::Back, "BACK")});
}

void Menus::drawChallenges(Context& ui) {
    std::string title = modeName(listType_);
    for (auto& ch : title) ch = char(std::toupper(ch));
    header(ui, title, modeDescription(listType_));
    auto list = game_.modes().forMap("", listType_);
    ui.rectGradient(Rect(0, 170, 900, 910), Vec4(0, 0, 0, 0.6f), Vec4(0, 0, 0, 0.3f));
    if (list.empty()) {
        ui.text("No challenges of this type yet.", Vec2(100, 260), 30.0f, ui.theme().textDim);
        footer(ui, {Hint::act(Action::Back, "BACK")});
        return;
    }
    float y = 220;
    for (size_t i = 0; i < list.size(); ++i) {
        const ChallengeDef& d = *list[i];
        Rect r(90, y, 740, 78);
        const ChallengeRecord* rec = nullptr;
        auto it = saves().data().challenges.find(d.id);
        if (it != saves().data().challenges.end()) rec = &it->second;
        bool act = ui.button("", r);
        bool f = ui.lastFocused();
        ui.text(d.name, Vec2(r.x + 70, r.y + 16), 34.0f, f ? ui.theme().accentText : ui.theme().text, FontStyle::Bold);
        medal(ui, Vec2(r.x + 36, r.y + 39), 16, rec ? rec->medal : 0);
        if (rec && rec->best != 0)
            ui.text(valueText(d, rec->best), Vec2(r.x + r.w - 24, r.y + 22), 28.0f, f ? ui.theme().accentText : ui.theme().textDim, FontStyle::Bold, Align::Right);
        if (f) {
            Rect info(960, 220, std::min(820.0f, ui.width() - 1020), 560);
            ui.shadow(info, 14, 18, 0.4f);
            ui.rect(info, ui.theme().panel, 14);
            ui.text(d.name, Vec2(info.x + 36, info.y + 26), 54.0f, ui.theme().accent, FontStyle::Display);
            std::string where;
            for (auto& m : game_.maps())
                for (auto& dist : m.districts)
                    if (m.scene == d.map && dist.spawn == d.spawn) where = m.name + "  /  " + dist.name;
            ui.text(where, Vec2(info.x + 38, info.y + 100), 24.0f, ui.theme().textDim, FontStyle::SemiBold);
            float py = info.y + 150;
            py += ui.paragraph(d.description, Rect(info.x + 38, py, info.w - 76, 200), 28.0f, ui.theme().text) + 20;
            if (d.timeLimit > 0) {
                char b[64];
                std::snprintf(b, sizeof(b), "Time limit  %d:%02d", int(d.timeLimit) / 60, int(d.timeLimit) % 60);
                ui.text(b, Vec2(info.x + 38, py), 26.0f, ui.theme().textDim, FontStyle::SemiBold);
                py += 44;
            }
            if (!d.trickList.empty()) {
                std::string tl = "Land: ";
                for (size_t k = 0; k < d.trickList.size(); ++k) tl += (k ? ", " : "") + d.trickList[k];
                py += ui.paragraph(tl, Rect(info.x + 38, py, info.w - 76, 120), 26.0f, ui.theme().text) + 10;
            }
            const char* names[3] = {"BRONZE", "SILVER", "GOLD"};
            float tx = info.x + 38;
            for (int k = 0; k < 3; ++k) {
                medal(ui, Vec2(tx + 16, info.y + info.h - 70), 16, k + 1);
                ui.text(names[k], Vec2(tx + 42, info.y + info.h - 96), 20.0f, ui.theme().textDim, FontStyle::Bold);
                ui.text(valueText(d, d.targets[k]), Vec2(tx + 42, info.y + info.h - 72), 28.0f, ui.theme().text, FontStyle::Bold);
                tx += 230;
            }
            if (rec) {
                char b[96];
                std::snprintf(b, sizeof(b), "Best %s   Attempts %d", valueText(d, rec->best).c_str(), rec->attempts);
                ui.text(b, Vec2(info.x + 38, info.y + info.h + 20), 26.0f, ui.theme().text, FontStyle::SemiBold, Align::Left, 0.6f);
            }
        }
        if (act) {
            requestLoad(d.map, d.spawn, d.id);
            return;
        }
        y += 88;
    }
    footer(ui, {Hint::act(Action::Confirm, "START"), Hint::act(Action::Back, "BACK")});
}

void Menus::drawMap(Context& ui) {
    header(ui, "MAP", "Choose where to ride. Every district of Scoot City is connected.");
    std::vector<std::pair<const MapInfo*, const DistrictInfo*>> all;
    for (auto& m : game_.maps())
        for (auto& d : m.districts) all.push_back({&m, &d});
    int cols = 4;
    float cw = std::min(400.0f, (ui.width() - 180.0f - float(cols - 1) * 24.0f) / float(cols)), chh = 200;
    float totalW = float(cols) * cw + float(cols - 1) * 24.0f;
    float x0 = (ui.width() - totalW) * 0.5f;
    int rows = int((all.size() + size_t(cols) - 1) / size_t(cols));
    // keep the focused row visible
    int frow = ui.focusIndex() / cols;
    float wantScroll = std::max(0.0f, float(frow) * (chh + 24) - 420.0f);
    scroll_ = dampf(scroll_, wantScroll, 12.0f, 1.0f / 60.0f);
    ui.pushClip(Rect(0, 180, ui.width(), 700));
    const DistrictInfo* focused = nullptr;
    const MapInfo* focusedMap = nullptr;
    for (size_t i = 0; i < all.size(); ++i) {
        int c = int(i) % cols, r = int(i) / cols;
        Rect rc(x0 + float(c) * (cw + 24), 200 + float(r) * (chh + 24) - scroll_, cw, chh);
        bool current = game_.mapPath() == all[i].first->scene && game_.currentArea() == all[i].second->name;
        bool act = ui.card(all[i].second->name, all[i].first->name, rc, current, Vec4(all[i].second->color, 1));
        if (ui.lastFocused()) {
            focused = all[i].second;
            focusedMap = all[i].first;
        }
        // left / right move across the grid too
        if (act) {
            ui.popClip();
            requestLoad(all[i].first->scene, all[i].second->spawn, "");
            return;
        }
    }
    ui.popClip();
    (void)rows;
    if (ui.nav().left) ui.setFocus(std::max(0, ui.focusIndex() - 1));
    if (ui.nav().right) ui.setFocus(std::min(int(all.size()) - 1, ui.focusIndex() + 1));
    if (focused) {
        Rect info(x0, 900, totalW, 90);
        ui.rect(info, ui.theme().panel, 12);
        ui.text(focused->name, Vec2(info.x + 28, info.y + 14), 34.0f, Vec4(focused->color, 1), FontStyle::Bold);
        ui.text(focused->description, Vec2(info.x + 28, info.y + 54), 24.0f, ui.theme().text, FontStyle::SemiBold);
        (void)focusedMap;
    }
    footer(ui, {Hint::ls("NAVIGATION"), Hint::act(Action::Confirm, "RIDE HERE"), Hint::act(Action::Back, "BACK")});
}

void Menus::drawRider(Context& ui) {
    header(ui, "RIDER", "Clothes, headwear and colors");
    Customization c = game_.visual().customization();
    float W = ui.width();
    Rect panel(W - 820, 190, 760, 800);
    ui.shadow(panel, 14, 20, 0.4f);
    ui.rect(panel, ui.theme().panel, 14);
    float y = panel.y + 30, x = panel.x + 30, w = panel.w - 60, h = 66, gap = 10;
    bool changed = false;
    auto row = [&](const std::string& label, const std::string& value) {
        int d = ui.choice(label, value, Rect(x, y, w, h));
        y += h + gap;
        return d;
    };
    const char* tops[] = {"T-Shirt", "Hoodie", "Tank Top"};
    const char* pants[] = {"Jeans", "Shorts"};
    const char* shoes[] = {"Low Tops", "High Tops"};
    const char* heads[] = {"None", "Helmet", "Cap"};
    if (int d = row("Top", tops[c.top])) { c.top = (c.top + d + 3) % 3; changed = true; }
    if (int d = row("Top Color", colorName(clothPalette(), c.topColor))) { cycleColor(clothPalette(), c.topColor, d); changed = true; }
    if (int d = row("Pants", pants[c.pants])) { c.pants = (c.pants + d + 2) % 2; changed = true; }
    if (int d = row("Pants Color", colorName(clothPalette(), c.pantsColor))) { cycleColor(clothPalette(), c.pantsColor, d); changed = true; }
    if (int d = row("Shoes", shoes[c.shoes])) { c.shoes = (c.shoes + d + 2) % 2; changed = true; }
    if (int d = row("Shoe Color", colorName(clothPalette(), c.shoesColor))) { cycleColor(clothPalette(), c.shoesColor, d); changed = true; }
    if (int d = row("Headwear", heads[c.helmet])) { c.helmet = (c.helmet + d + 3) % 3; changed = true; }
    if (int d = row("Headwear Color", colorName(clothPalette(), c.helmetColor))) { cycleColor(clothPalette(), c.helmetColor, d); changed = true; }
    if (int d = row("Skin Tone", colorName(skinPalette(), c.skinColor))) { cycleColor(skinPalette(), c.skinColor, d); changed = true; }
    if (ui.button("DONE", Rect(x, y + 16, w, 70), true)) {
        back();
        return;
    }
    if (changed) {
        saves().data().custom = c;
        customizationChanged();
    }
    footer(ui, {Hint::ls("CHANGE", StickDir::Right), Hint::act(Action::Back, "BACK")});
}

// --- scooter shop -----------------------------------------------------------------------------------
namespace {

const std::vector<NamedColor>& urethanePalette() {
    static const std::vector<NamedColor> p = {
        {"White", {0.92f, 0.92f, 0.9f}},  {"Black", {0.04f, 0.04f, 0.045f}}, {"Smoke", {0.32f, 0.33f, 0.35f}}, {"Red", {0.8f, 0.07f, 0.06f}},
        {"Blue", {0.06f, 0.2f, 0.75f}},   {"Teal", {0.05f, 0.55f, 0.5f}},     {"Lime", {0.4f, 0.85f, 0.12f}},  {"Pink", {0.9f, 0.35f, 0.58f}},
        {"Purple", {0.35f, 0.1f, 0.6f}},  {"Orange", {0.95f, 0.38f, 0.04f}},  {"Yellow", {0.95f, 0.78f, 0.08f}}, {"Ice", {0.72f, 0.82f, 0.9f}}};
    return p;
}

struct ShopCategory {
    const char* title;
    PartThumbnails::Kind kind;
    int variants;
    const std::vector<NamedColor>& (*palette)();
    const char* names[3];
    int brand[3];
    const char* specs[3];
};

// fictional brands (their banners hang in the shop)
const char* kBrands[3] = {"FLOWLAB", "KDX PRO", "AXLE CO."};

const ShopCategory kShopCats[6] = {
    {"DECK", PartThumbnails::Deck, 3, metalPalette, {"Street 4.8\"", "Wide 5.3\"", "Park 4.5\""}, {0, 1, 2},
     {"122 mm wide  /  6061-T6 aluminium  /  integrated headtube", "135 mm wide  /  6061-T6 aluminium  /  flat nose",
      "115 mm wide  /  6061-T6 aluminium  /  round nose"}},
    {"BARS", PartThumbnails::Bars, 3, metalPalette, {"Standard", "Tall", "Wide"}, {2, 0, 1},
     {"86 cm grip height  /  56 cm wide  /  4130 chromoly", "92 cm grip height  /  56 cm wide  /  4130 chromoly",
      "86 cm grip height  /  62 cm wide  /  4130 chromoly"}},
    {"CLAMP", PartThumbnails::Clamp, 1, metalPalette, {"Double Clamp", "", ""}, {2, 2, 2},
     {"4 bolt double clamp  /  7075 aluminium", "", ""}},
    {"WHEELS", PartThumbnails::Wheel, 3, metalPalette, {"6 Spoke", "12 Spoke", "Solid Core"}, {1, 0, 2},
     {"110 x 24 mm  /  hollow core  /  ABEC 9", "110 x 24 mm  /  12 spoke core  /  ABEC 9", "110 x 24 mm  /  solid core  /  ABEC 9"}},
    {"URETHANE", PartThumbnails::Urethane, 1, urethanePalette, {"88A Street Urethane", "", ""}, {0, 0, 0},
     {"88A hardness  /  high rebound pour", "", ""}},
    {"GRIPS", PartThumbnails::Grips, 1, clothPalette, {"Soft Grips 160 mm", "", ""}, {1, 1, 1},
     {"160 mm  /  soft compound  /  flanged", "", ""}},
};

// equipped (variant, colour index) of a category
void shopEquipped(const Customization& c, int cat, int& variant, int& color) {
    const ShopCategory& k = kShopCats[cat];
    const auto& pal = k.palette();
    switch (cat) {
        case 0: variant = c.deck; color = nearestIndex(pal, c.deckColor); break;
        case 1: variant = c.bars; color = nearestIndex(pal, c.barsColor); break;
        case 2: variant = 0; color = nearestIndex(pal, c.clampColor); break;
        case 3: variant = c.wheels; color = nearestIndex(pal, c.coreColor); break;
        case 4: variant = 0; color = nearestIndex(pal, c.wheelColor); break;
        default: variant = 0; color = nearestIndex(pal, c.gripColor); break;
    }
}

Customization shopApply(Customization c, int cat, int variant, int color) {
    const Vec3 col = kShopCats[cat].palette()[size_t(color)].c;
    switch (cat) {
        case 0: c.deck = variant; c.deckColor = col; break;
        case 1: c.bars = variant; c.barsColor = col; break;
        case 2: c.clampColor = col; break;
        case 3: c.wheels = variant; c.coreColor = col; break;
        case 4: c.wheelColor = col; break;
        default: c.gripColor = col; break;
    }
    return c;
}

void brandMark(Context& ui, int brand, Vec2 c) {
    switch (brand) {
        case 0: ui.text(kBrands[0], c + Vec2(0, -30), 58.0f, Vec4(0.07f, 0.07f, 0.08f, 1), FontStyle::Display, Align::Center); break;
        case 1: ui.text(kBrands[1], c + Vec2(0, -30), 58.0f, Vec4(0.86f, 0.13f, 0.1f, 1), FontStyle::Display, Align::Center); break;
        default: {
            float w = ui.measure(kBrands[2], 46.0f, FontStyle::Bold) + 44.0f;
            ui.rect(Rect(c.x - w * 0.5f, c.y - 34, w, 68), Vec4(0.19f, 0.21f, 0.24f, 1), 6);
            ui.text(kBrands[2], c + Vec2(0, -25), 46.0f, Vec4(0.38f, 0.9f, 0.55f, 1), FontStyle::Bold, Align::Center);
            break;
        }
    }
}

}  // namespace

void Menus::shopHints(Context& ui) {
    float W = ui.width();
    ui.rectGradient(Rect(0, 990, W, 90), Vec4(0, 0, 0, 0.0f), Vec4(0, 0, 0, 0.62f));
    glyphs::hintRow(ui, {Hint::ls("NAVIGATION"), Hint::rs("ROTATE CAMERA"), Hint::act(Action::MenuExtra, "SAVE"), Hint::act(Action::Confirm, "SELECT"),
                         Hint::act(Action::Back, "BACK")},
                    W - 56, 1041);
    if (toastTime_ < 2.5f && !toast_.empty()) {
        float a = saturate((2.5f - toastTime_) * 3.0f);
        float tw = ui.measure(toast_, 26.0f, FontStyle::Bold) + 48;
        Rect r(W - 56 - tw, 44, tw, 56);
        ui.rect(r, ui.theme().accent * Vec4(1, 1, 1, a), 10);
        ui.textBox(toast_, r, 26.0f, ui.theme().accentText * Vec4(1, 1, 1, a), FontStyle::Bold, Align::Center);
    }
}

void Menus::drawScooter(Context& ui) {
    const NavInput& nav = ui.nav();
    thumbs_.beginFrame();
    const Customization equipped = saves().data().custom;
    // category switching (LB / RB, Q / E)
    int prevCat = shopCat_;
    if (nav.tabLeft) shopCat_ = (shopCat_ + 5) % 6;
    if (nav.tabRight) shopCat_ = (shopCat_ + 1) % 6;
    const ShopCategory& cat = kShopCats[shopCat_];
    const auto& pal = cat.palette();
    const int perVariant = int(pal.size());
    const int count = cat.variants * perVariant;
    const int cols = 3;
    int eqV, eqC;
    shopEquipped(equipped, shopCat_, eqV, eqC);
    const int eqIndex = std::clamp(eqV, 0, cat.variants - 1) * perVariant + eqC;
    if (shopFocus_ < 0 || prevCat != shopCat_) {
        shopFocus_ = eqIndex;
        shopScroll_ = std::max(0.0f, float(shopFocus_ / cols - 1) * 182.0f);
        if (prevCat != shopCat_) game_.sound().ui("ui_move");
    }
    int before = shopFocus_;
    if (nav.left && shopFocus_ % cols > 0) --shopFocus_;
    if (nav.right && shopFocus_ % cols < cols - 1 && shopFocus_ + 1 < count) ++shopFocus_;
    if (nav.up && shopFocus_ >= cols) shopFocus_ -= cols;
    if (nav.down && shopFocus_ + cols < count) shopFocus_ += cols;

    // panel (frosted glass)
    const Rect P(44, 44, 580, 940);
    ui.shadow(P, 18, 24, 0.25f);
    ui.frosted(P, 18, Vec4(0.9f, 0.915f, 0.935f, 1.0f), 0.5f);
    ui.rectGradient(P, Vec4(1, 1, 1, 0.16f), Vec4(1, 1, 1, 0.04f), 18);
    ui.rectOutline(P, Vec4(1, 1, 1, 0.45f), 1.5f, 18);
    const Vec4 ink(0.07f, 0.075f, 0.09f, 1.0f), inkDim(0.3f, 0.32f, 0.36f, 1.0f);
    // header: LB  TITLE  RB + page dots
    bool pad = input().usingGamepad();
    Rect lb(P.x + 24, P.y + 30, 62, 44), rb(P.x + P.w - 24 - 62, P.y + 30, 62, 44);
    for (int i = 0; i < 2; ++i) {
        Rect r = i == 0 ? lb : rb;
        bool hover = r.contains(ui.mouse());
        ui.rect(r, Vec4(0.08f, 0.085f, 0.1f, hover ? 1.0f : 0.88f), 10);
        // the glyph of the bound button / key, centred in the chip
        Action a = i == 0 ? Action::TabLeft : Action::TabRight;
        const Binding& bnd = input().binding(a);
        std::string gl = pad ? (!bnd.buttons.empty() ? glyphs::buttonName(bnd.buttons.front()) : "") : (!bnd.keys.empty() ? SDL_GetScancodeName(bnd.keys.front()) : "");
        ui.textBox(gl, r, 22.0f, Vec4(1, 1, 1, 1), FontStyle::Bold, Align::Center);
        if (hover && nav.click) {
            shopCat_ = (shopCat_ + (i == 0 ? 5 : 1)) % 6;
            shopFocus_ = -1;
            game_.sound().ui("ui_move");
        }
    }
    ui.text(cat.title, Vec2(P.x + P.w * 0.5f, P.y + 18), 64.0f, ink, FontStyle::Display, Align::Center);
    for (int i = 0; i < 6; ++i) {
        Vec2 c(P.x + P.w * 0.5f + (float(i) - 2.5f) * 22.0f, P.y + 112);
        ui.circle(c, i == shopCat_ ? 6.0f : 4.5f, i == shopCat_ ? ink : ink * Vec4(1, 1, 1, 0.28f), 16);
    }
    const int focusV = std::min(shopFocus_ / perVariant, cat.variants - 1), focusC = shopFocus_ % perVariant;
    // item name, colour, spec
    ui.text(kBrands[cat.brand[focusV]], Vec2(P.x + 28, P.y + 134), 22.0f, inkDim, FontStyle::Bold);
    ui.text(cat.names[focusV], Vec2(P.x + 28, P.y + 158), 40.0f, ink, FontStyle::Bold);
    Vec3 fc = pal[size_t(focusC)].c;
    Vec3 sw(std::pow(fc.x, 1.0f / 2.2f), std::pow(fc.y, 1.0f / 2.2f), std::pow(fc.z, 1.0f / 2.2f));
    ui.circle(Vec2(P.x + 38, P.y + 222), 10, Vec4(sw, 1), 24);
    ui.arc(Vec2(P.x + 38, P.y + 222), 10, 1.5f, 0, kTwoPi, ink * Vec4(1, 1, 1, 0.35f));
    std::string colLine = pal[size_t(focusC)].name;
    if (shopFocus_ == eqIndex) colLine += "   -   EQUIPPED";
    ui.text(colLine, Vec2(P.x + 58, P.y + 208), 24.0f, ink, FontStyle::SemiBold);
    ui.text(cat.specs[focusV], Vec2(P.x + 28, P.y + 242), 20.0f, inkDim, FontStyle::SemiBold);

    // thumbnail grid
    const float tile = 168.0f, gap = 14.0f, gx = P.x + 24, gy = P.y + 284, gh = 3 * (tile + gap) - gap + 8;
    const int rows = (count + cols - 1) / cols;
    const float maxScroll = std::max(0.0f, float(rows) * (tile + gap) - gap - gh + 8);
    Rect gridRect(gx - 6, gy - 4, 3 * tile + 2 * gap + 12, gh + 8);
    if (gridRect.contains(ui.mouse()) && nav.wheel != 0.0f) shopScroll_ = clampf(shopScroll_ - nav.wheel * 90.0f, 0.0f, maxScroll);
    if (shopFocus_ != before) {
        int row = shopFocus_ / cols;
        float top = float(row) * (tile + gap), bottom = top + tile;
        if (top < shopScroll_) shopScroll_ = top;
        if (bottom > shopScroll_ + gh - 8) shopScroll_ = bottom - gh + 8;
        game_.sound().ui("ui_move");
    }
    static float scrollAnim = 0.0f;
    scrollAnim = dampf(scrollAnim, shopScroll_, 16.0f, 1.0f / 60.0f);
    ui.pushClip(gridRect);
    int clicked = -1;
    for (int i = 0; i < count; ++i) {
        int r = i / cols, c = i % cols;
        Rect t(gx + float(c) * (tile + gap), gy + float(r) * (tile + gap) - scrollAnim, tile, tile);
        if (t.y + t.h < gridRect.y || t.y > gridRect.y + gridRect.h) continue;
        bool focused = i == shopFocus_;
        bool hover = t.contains(ui.mouse()) && gridRect.contains(ui.mouse());
        if (hover && nav.mouseMoved && shopFocus_ != i) shopFocus_ = i;
        if (hover && nav.click) clicked = i;
        int v = i / perVariant, ci = i % perVariant;
        ui.rect(t, Vec4(0.97f, 0.975f, 0.98f, focused ? 0.98f : 0.8f), 12);
        ui.rectGradient(Rect(t.x, t.y + t.h * 0.55f, t.w, t.h * 0.45f), Vec4(0, 0, 0, 0.0f), Vec4(0, 0, 0, 0.06f), 12);
        if (Texture* tex = thumbs_.get(cat.kind, v, pal[size_t(ci)].c)) {
            ui.image(tex, t.shrink(6));
        } else {
            float ph = std::fmod(ui.time() * 2.0f + float(i) * 0.13f, 1.0f);
            ui.circle(t.center(), 5.0f + 3.0f * ph, ink * Vec4(1, 1, 1, 0.25f * (1.0f - ph)), 16);
        }
        if (i == eqIndex) {
            Vec2 bc(t.x + t.w - 20, t.y + 20);
            ui.circle(bc, 12, ui.theme().accent, 20);
            ui.line(bc + Vec2(-5.5f, 0.5f), bc + Vec2(-1.5f, 4.5f), 2.6f, ui.theme().accentText);
            ui.line(bc + Vec2(-1.5f, 4.5f), bc + Vec2(6.0f, -4.5f), 2.6f, ui.theme().accentText);
        }
        if (focused) ui.rectOutline(t.shrink(-3), ui.theme().accent, 4.0f, 14);
    }
    ui.popClip();
    if (maxScroll > 0.0f) {
        float bh = gh * gh / (gh + maxScroll);
        float by = gy + (gh - bh) * (scrollAnim / maxScroll);
        ui.rect(Rect(P.x + P.w - 12, gy, 4, gh), ink * Vec4(1, 1, 1, 0.08f), 2);
        ui.rect(Rect(P.x + P.w - 12, by, 4, bh), ink * Vec4(1, 1, 1, 0.45f), 2);
    }
    // brand of the focused part
    ui.rect(Rect(P.x + 28, P.y + P.h - 124, P.w - 56, 1.5f), ink * Vec4(1, 1, 1, 0.15f));
    brandMark(ui, cat.brand[focusV], Vec2(P.x + P.w * 0.5f, P.y + P.h - 62));

    // live preview of the focused part on the display scooter; select equips it
    const int previewKey = shopCat_ * 1000 + shopFocus_;
    if (previewKey != shopPreviewKey_) {
        shopPreviewKey_ = previewKey;
        game_.visual().applyCustomization(shopApply(equipped, shopCat_, focusV, focusC));
    }
    if (clicked >= 0) shopFocus_ = clicked;
    if ((nav.confirm || clicked >= 0) && screenTime_ > 0.15f) {
        int v = std::min(shopFocus_ / perVariant, cat.variants - 1), c = shopFocus_ % perVariant;
        saves().data().custom = shopApply(equipped, shopCat_, v, c);
        customizationChanged();
        shopPreviewKey_ = -1;
        toast_ = std::string(cat.names[v]) + "  /  " + pal[size_t(c)].name;
        toastTime_ = 0.0f;
        game_.sound().ui("ui_select");
    }
    if (nav.extra) {
        saves().markDirty();
        saves().flush();
        toast_ = "SETUP SAVED";
        toastTime_ = 0.0f;
        game_.sound().ui("ui_select");
    }
    shopHints(ui);
}

void Menus::drawSettings(Context& ui) {
    header(ui, "SETTINGS");
    Settings& s = saves().settings();
    const char* tabs[] = {"GRAPHICS", "GAMEPLAY", "AUDIO", "CONTROLS"};
    float W = ui.width();
    if (rebindAction_ < 0) {
        if (ui.nav().tabLeft) { settingsTab_ = (settingsTab_ + 3) % 4; ui.resetFocus(0); scroll_ = 0; game_.sound().ui("ui_move"); }
        if (ui.nav().tabRight) { settingsTab_ = (settingsTab_ + 1) % 4; ui.resetFocus(0); scroll_ = 0; game_.sound().ui("ui_move"); }
    }
    float tx = 124;
    for (int t = 0; t < 4; ++t) {
        float tw = ui.measure(tabs[t], 30.0f, FontStyle::Bold) + 48;
        Rect tr(tx, 150, tw, 50);
        bool sel = t == settingsTab_;
        ui.rect(tr, sel ? ui.theme().accent : Vec4(0.05f, 0.05f, 0.07f, 0.8f), 8);
        ui.textBox(tabs[t], tr, 30.0f, sel ? ui.theme().accentText : ui.theme().textDim, FontStyle::Bold, Align::Center);
        if (ui.nav().click && tr.contains(ui.mouse()) && rebindAction_ < 0) {
            settingsTab_ = t;
            ui.resetFocus(0);
        }
        tx += tw + 12;
    }
    {
        bool pad = input().usingGamepad();
        float gx = tx + 20;
        gx += glyphs::action(ui, Action::TabLeft, gx, 175, 34, pad) + 6;
        gx += glyphs::action(ui, Action::TabRight, gx, 175, 34, pad) + 12;
        ui.text("SWITCH TAB", Vec2(gx, 162), 22.0f, ui.theme().text, FontStyle::SemiBold, Align::Left, 0.9f);
    }

    Rect panel(100, 230, std::min(1100.0f, W - 200), 760);
    ui.rect(panel, ui.theme().panel, 14);
    float h = 62, gap = 8;
    int focusRow = ui.focusIndex();
    float wantScroll = std::max(0.0f, float(focusRow) * (h + gap) - (panel.h - 160));
    scroll_ = dampf(scroll_, wantScroll, 14.0f, 1.0f / 60.0f);
    ui.pushClip(Rect(panel.x, panel.y + 10, panel.w, panel.h - 20));
    float x = panel.x + 26, w = panel.w - 52, y = panel.y + 24 - scroll_;
    auto R = [&]() {
        Rect r(x, y, w, h);
        y += h + gap;
        return r;
    };
    bool video = false, other = false;
    auto& G = s.graphics;
    auto& P = s.gameplay;
    auto& A = s.audio;
    if (settingsTab_ == 0) {
        const char* modes[] = {"Windowed", "Fullscreen", "Borderless"};
        if (int d = ui.choice("Display Mode", modes[G.windowMode], R())) { G.windowMode = (G.windowMode + d + 3) % 3; video = true; }
        auto res = engine().window().resolutions();
        std::vector<std::pair<int, int>> sizes;
        for (auto& r : res)
            if (std::find(sizes.begin(), sizes.end(), std::make_pair(r.width, r.height)) == sizes.end()) sizes.push_back({r.width, r.height});
        if (sizes.empty()) sizes = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
        std::sort(sizes.begin(), sizes.end());
        char rb[32];
        std::snprintf(rb, sizeof(rb), "%d x %d", G.width, G.height);
        if (int d = ui.choice("Resolution", rb, R())) {
            size_t cur = 0;
            for (size_t i = 0; i < sizes.size(); ++i)
                if (sizes[i].first == G.width && sizes[i].second == G.height) cur = i;
            cur = (cur + sizes.size() + size_t(d)) % sizes.size();
            G.width = sizes[cur].first;
            G.height = sizes[cur].second;
            video = true;
        }
        if (ui.toggle("VSync", G.vsync, R())) video = true;
        const int limits[] = {30, 60, 90, 120, 144, 165, 240, 0};
        int li = 7;
        for (int i = 0; i < 8; ++i)
            if (limits[i] == G.fpsLimit) li = i;
        std::string lt = G.fpsLimit == 0 ? "Unlimited" : std::to_string(G.fpsLimit);
        if (int d = ui.choice("FPS Limit", lt, R())) { G.fpsLimit = limits[(li + d + 8) % 8]; video = true; }
        const char* q[] = {"Low", "Medium", "High", "Ultra"};
        int preset = G.shadowQuality <= 1 ? 0 : G.shadowQuality == 2 ? 1 : G.shadowQuality == 3 ? 2 : 3;
        if (int d = ui.choice("Quality Preset", q[preset], R())) {
            preset = std::clamp(preset + d, 0, 3);
            G.shadowQuality = preset + 1;
            G.textureQuality = preset;
            G.ssao = preset >= 1;
            G.bloom = true;
            G.drawDistance = 350.0f + float(preset) * 200.0f;
            G.lodBias = 0.6f + float(preset) * 0.3f;
            G.renderScale = preset == 0 ? 0.75f : 1.0f;
            video = true;
        }
        if (ui.slider("Render Scale", G.renderScale, 0.5f, 1.0f, 0.05f, R())) video = true;
        const char* sq[] = {"Off", "Low", "Medium", "High", "Ultra"};
        if (int d = ui.choice("Shadows", sq[G.shadowQuality], R())) { G.shadowQuality = std::clamp(G.shadowQuality + d, 0, 4); video = true; }
        if (int d = ui.choice("Textures (next map load)", q[G.textureQuality], R())) { G.textureQuality = std::clamp(G.textureQuality + d, 0, 3); video = true; }
        if (ui.toggle("Ambient Occlusion", G.ssao, R())) video = true;
        if (ui.toggle("Bloom", G.bloom, R())) video = true;
        if (ui.toggle("Motion Blur", G.motionBlur, R())) video = true;
        if (ui.toggle("Anti-Aliasing (FXAA)", G.fxaa, R())) video = true;
        if (ui.toggle("Sharpening", G.sharpen, R())) video = true;
        if (ui.slider("Draw Distance", G.drawDistance, 200.0f, 1200.0f, 50.0f, R(), "%.0f m", 1.0f)) video = true;
        const char* lq[] = {"Low", "Medium", "High", "Ultra"};
        int lod = std::clamp(int(std::lround((G.lodBias - 0.6f) / 0.3f)), 0, 3);
        if (int d = ui.choice("Level of Detail", lq[lod], R())) { G.lodBias = 0.6f + float(std::clamp(lod + d, 0, 3)) * 0.3f; video = true; }
        if (ui.slider("Field of View", G.fov, 60.0f, 100.0f, 1.0f, R(), "%.0f", 1.0f)) video = true;
        if (ui.slider("Brightness", G.brightness, 0.5f, 1.8f, 0.05f, R())) video = true;
        if (ui.toggle("Particles", G.particles, R())) video = true;
    } else if (settingsTab_ == 1) {
        const char* stance[] = {"Regular (left foot forward)", "Goofy (right foot forward)"};
        if (int d = ui.choice("Stance", stance[P.stance], R())) { (void)d; P.stance = 1 - P.stance; other = true; }
        const char* assist[] = {"Off", "Low", "Normal"};
        if (int d = ui.choice("Landing Assist", assist[P.landingAssist], R())) { P.landingAssist = std::clamp(P.landingAssist + d, 0, 2); other = true; }
        const char* bal[] = {"Easy", "Normal", "Hard"};
        int bi = P.balanceDifficulty < 0.9f ? 0 : P.balanceDifficulty > 1.1f ? 2 : 1;
        if (int d = ui.choice("Balance Difficulty", bal[bi], R())) { P.balanceDifficulty = 0.75f + 0.25f * float(std::clamp(bi + d, 0, 2)); other = true; }
        if (ui.slider("Camera Sensitivity", P.cameraSensitivity, 0.2f, 3.0f, 0.1f, R())) other = true;
        if (ui.toggle("Invert Camera Y", P.invertY, R())) other = true;
        if (ui.slider("Camera Distance", P.cameraDistance, 0.7f, 1.5f, 0.05f, R())) other = true;
        const char* sh[] = {"Off", "Low", "Normal"};
        if (int d = ui.choice("Camera Shake", sh[P.cameraShake], R())) { P.cameraShake = std::clamp(P.cameraShake + d, 0, 2); other = true; }
        if (ui.toggle("Vibration", P.vibration, R())) other = true;
        if (ui.toggle("Show HUD", P.showHud, R())) other = true;
        if (ui.toggle("Show Trick Names", P.showTrickNames, R())) other = true;
        if (int d = ui.choice("Speed Units", P.metric ? "km/h" : "mph", R())) { (void)d; P.metric = !P.metric; other = true; }
    } else if (settingsTab_ == 2) {
        if (ui.slider("Master Volume", A.master, 0.0f, 1.0f, 0.05f, R())) other = true;
        if (ui.slider("Music", A.music, 0.0f, 1.0f, 0.05f, R())) other = true;
        if (ui.slider("Sound Effects", A.sfx, 0.0f, 1.0f, 0.05f, R())) other = true;
        if (ui.slider("Environment", A.environment, 0.0f, 1.0f, 0.05f, R())) other = true;
        if (ui.slider("Interface", A.ui, 0.0f, 1.0f, 0.05f, R())) other = true;
        if (ui.toggle("Music Enabled", A.musicEnabled, R())) other = true;
        std::string np = music().current() ? music().current()->title : "-";
        if (ui.choice("Now Playing", np, R()) != 0) music().next();
    } else {
        // controls: layout choice, then one row per action (gamepad glyph + key); confirm rebinds the action to
        // the next button, trigger or key pressed
        bool flow = input().scheme() == ControlScheme::Flow;
        const char* layouts[] = {"Scooter Flow style (twin stick)", "Classic (buttons)"};
        if (ui.choice("Control Layout", layouts[flow ? 0 : 1], R())) {
            P.controlScheme = flow ? 1 : 0;
            input().setScheme(ControlScheme(P.controlScheme));
            rebindAction_ = -1;
            other = true;
            flow = !flow;
        }
        std::vector<std::pair<Action, const char*>> rows;
        if (flow)
            rows = {{Action::Push, "Push"}, {Action::Brake, "Brake"}, {Action::TrickMod, "Trick (hold + right stick)"}, {Action::Grab, "Grab (hold + right stick)"},
                    {Action::SpinLeft, "Spin Left"}, {Action::SpinRight, "Spin Right"}, {Action::Revert, "Revert"}, {Action::Jump, "Pop (keyboard, hold to crouch)"},
                    {Action::Respawn, "Respawn"}, {Action::Checkpoint, "Set Checkpoint"}, {Action::Pause, "Pause"}, {Action::CameraMode, "Camera Mode"},
                    {Action::CameraReset, "Camera Reset"}};
        else
            rows = {{Action::Push, "Push"}, {Action::Jump, "Jump / Pop (hold to crouch)"}, {Action::Brake, "Brake"}, {Action::SpinLeft, "Spin Left"},
                    {Action::SpinRight, "Spin Right"}, {Action::Grab, "Grab (modifier)"}, {Action::Revert, "Revert"}, {Action::Respawn, "Respawn"},
                    {Action::Checkpoint, "Set Checkpoint"}, {Action::Pause, "Pause"}, {Action::CameraMode, "Camera Mode"}, {Action::CameraReset, "Camera Reset"}};
        for (int i = 0; i < int(rows.size()); ++i) {
            Rect r = R();
            bool act = ui.button("", r);
            bool f = ui.lastFocused();
            Vec4 tc = f ? ui.theme().accentText : ui.theme().text;
            ui.textBox(rows[size_t(i)].second, Rect(r.x + 24, r.y, r.w * 0.5f, r.h), 26.0f, tc, FontStyle::SemiBold, Align::Left);
            const Binding& b = input().binding(rows[size_t(i)].first);
            if (rebindAction_ == i) {
                ui.textBox("Press a button or key...  (Esc cancels)", Rect(r.x + r.w * 0.45f, r.y, r.w * 0.53f, r.h), 24.0f, tc, FontStyle::Bold, Align::Right);
            } else {
                float cy = r.y + r.h * 0.5f;
                if (!b.buttons.empty() || !b.axisButtons.empty()) glyphs::action(ui, rows[size_t(i)].first, r.x + r.w * 0.6f, cy, 40, true);
                else ui.textBox("-", Rect(r.x + r.w * 0.6f, r.y, 40, r.h), 26.0f, tc, FontStyle::Bold, Align::Center);
                if (!b.keys.empty() || !b.mouseButtons.empty()) glyphs::action(ui, rows[size_t(i)].first, r.x + r.w * 0.78f, cy, 40, false);
                else ui.textBox("-", Rect(r.x + r.w * 0.78f, r.y, 40, r.h), 26.0f, tc, FontStyle::Bold, Align::Center);
            }
            if (act && rebindAction_ < 0) {
                rebindAction_ = i;
                rebindTime_ = 0.0f;
                input().startCapture();
            }
            if (rebindAction_ == i) {
                rebindTime_ += 1.0f / 60.0f;
                SDL_GamepadButton gb;
                SDL_GamepadAxis ax;
                SDL_Scancode sc;
                Action target = rows[size_t(i)].first;
                if (rebindTime_ > 0.25f && input().captureNextGamepadButton(gb)) {
                    input().setGamepadBinding(target, gb);
                    rebindAction_ = -1;
                    other = true;
                } else if (rebindTime_ > 0.25f && input().captureNextTrigger(ax)) {
                    input().setGamepadTrigger(target, ax);
                    rebindAction_ = -1;
                    other = true;
                } else if (rebindTime_ > 0.25f && input().captureNextKey(sc)) {
                    if (sc != SDL_SCANCODE_ESCAPE) input().setKeyBinding(target, sc);
                    input().stopCapture();
                    rebindAction_ = -1;
                    other = true;
                }
            }
        }
        if (ui.button("RESET TO DEFAULTS", R())) {
            fs::removeFile(fs::userPath(input().userBindingsFile()));
            input().setScheme(input().scheme());
            toast_ = "Controls reset";
            toastTime_ = 0.0f;
        } else if (other) {
            saveJsonFile(fs::userPath(input().userBindingsFile()), input().saveBindings(), true);
        }
    }
    ui.popClip();
    if (settingsTab_ == 3 && W - (panel.x + panel.w) > 560) drawMoveList(ui, Rect(panel.x + panel.w + 30, 230, W - (panel.x + panel.w) - 90, 760));
    if (video || other) settingsChanged(video);
    footer(ui, {Hint::act(Action::TabRight, "SWITCH TAB"), Hint::ls("CHANGE", StickDir::Right), Hint::act(Action::Back, "BACK")},
           rebindAction_ >= 0 ? "Press the new button or key  (Esc cancels)" : "");
}

// move list of the active layout: each line is a glyph combo and what it does
void Menus::drawMoveList(Context& ui, const Rect& area) {
    ui.rect(area, ui.theme().panel, 14);
    bool pad = input().usingGamepad();
    bool flow = input().scheme() == ControlScheme::Flow;
    ui.text(flow ? "HOW TO RIDE  -  SCOOTER FLOW STYLE" : "HOW TO RIDE  -  CLASSIC", Vec2(area.x + 28, area.y + 22), 28.0f, ui.theme().accent, FontStyle::Bold);
    float y = area.y + 88;
    const float gh = 32.0f, gap = 41.5f;
    auto row = [&](const std::string& label, auto drawGlyphs) {
        float x = area.x + 28;
        x = drawGlyphs(x, y);
        ui.text(label, Vec2(std::max(x + 16, area.x + 236), y - 14), 22.0f, ui.theme().text, FontStyle::SemiBold);
        y += gap;
    };
    auto act = [&](Action a) { return [=, &ui](float x, float yy) { return x + glyphs::action(ui, a, x, yy, gh, pad); }; };
    auto stickDir = [&](bool right, StickDir d) { return [=, &ui](float x, float yy) { return x + glyphs::stick(ui, right, d, x, yy, gh, pad); }; };
    auto combo = [&](Action a, StickDir d) {
        return [=, &ui](float x, float yy) {
            x += glyphs::action(ui, a, x, yy, gh, pad);
            x += glyphs::plus(ui, x, yy, gh);
            return x + glyphs::stick(ui, true, d, x, yy, gh, pad);
        };
    };
    row("Carve / lean", stickDir(false, StickDir::None));
    row("Manual  /  nose manual", [&](float x, float yy) {
        x += glyphs::stick(ui, false, StickDir::Down, x, yy, gh, pad) + 6;
        return x + glyphs::stick(ui, false, StickDir::Up, x, yy, gh, pad);
    });
    row("Push", act(Action::Push));
    row("Brake", act(Action::Brake));
    if (flow) {
        row("Compress / pump (hold)", stickDir(true, StickDir::Down));
        row("Pop (flick up)", stickDir(true, StickDir::Up));
        row("Spin / flip in the air", [&](float x, float yy) {
            x += glyphs::stick(ui, true, StickDir::None, x, yy, gh, pad) + 6;
            x += glyphs::action(ui, Action::SpinLeft, x, yy, gh, pad) + 4;
            return x + glyphs::action(ui, Action::SpinRight, x, yy, gh, pad);
        });
        row("Tailwhip", combo(Action::TrickMod, StickDir::Right));
        row("Heelwhip", combo(Action::TrickMod, StickDir::Left));
        row("Barspin", combo(Action::TrickMod, StickDir::Up));
        row("Fingerwhip", combo(Action::TrickMod, StickDir::Down));
        row("Whip rewind (right, then left)", [&](float x, float yy) {
            x = combo(Action::TrickMod, StickDir::Right)(x, yy) + 6;
            return x + glyphs::stick(ui, true, StickDir::Left, x, yy, gh, pad);
        });
        row("Scooter flip  /  bri flip  /  kickless", [&](float x, float yy) {
            x = combo(Action::TrickMod, StickDir::UpRight)(x, yy) + 6;
            x += glyphs::stick(ui, true, StickDir::UpLeft, x, yy, gh, pad) + 6;
            return x + glyphs::stick(ui, true, StickDir::DownRight, x, yy, gh, pad);
        });
        row("Grabs: no hander / one hand / can can", combo(Action::Grab, StickDir::Up));
    } else {
        row("Crouch (hold)  /  pop (release)", act(Action::Jump));
        row("Spin / flip in the air", stickDir(false, StickDir::None));
        row("Tailwhip", stickDir(true, StickDir::Right));
        row("Heelwhip", stickDir(true, StickDir::Down));
        row("Barspin", stickDir(true, StickDir::Up));
        row("Grabs", combo(Action::Grab, StickDir::Up));
    }
    row("Revert", act(Action::Revert));
    if (y + 40 < area.y + area.h)
        ui.paragraph(flow ? "Push the right stick all the way for tricks to register. Repeat the input during a whip or barspin to double it."
                          : "Flick the right stick in the air for scooter tricks, hold the grab trigger for grabs.",
                     Rect(area.x + 28, y - 8, area.w - 56, area.y + area.h - y), 20.0f, ui.theme().textDim);
}

void Menus::drawPause(Context& ui) {
    ui.rect(Rect(0, 0, ui.width(), 1080), Vec4(0, 0, 0, 0.45f));
    float W = ui.width();
    Rect panel(W * 0.5f - 330, 190, 660, 700);
    ui.shadow(panel, 16, 24, 0.5f);
    ui.rect(panel, Vec4(0.05f, 0.05f, 0.07f, 0.92f), 16);
    ui.text("PAUSED", Vec2(W * 0.5f, panel.y + 34), 80.0f, ui.theme().text, FontStyle::Display, Align::Center);
    std::string sub = game_.modes().inChallenge() ? game_.modes().current()->name : std::string(modeName(ModeType::FreeRide));
    ui.text(sub, Vec2(W * 0.5f, panel.y + 132), 28.0f, ui.theme().accent, FontStyle::Bold, Align::Center);
    const char* items[] = {"RESUME", "RESTART", "RESPAWN", "SETTINGS", "MAIN MENU"};
    float y = panel.y + 200;
    for (int i = 0; i < 5; ++i) {
        if (ui.button(items[i], Rect(panel.x + 70, y, panel.w - 140, 76), i == 0)) {
            switch (i) {
                case 0:
                    close();
                    game_.setPaused(false);
                    break;
                case 1:
                    close();
                    game_.setPaused(false);
                    game_.restartRun();
                    break;
                case 2:
                    close();
                    game_.setPaused(false);
                    game_.player().respawn(true);
                    break;
                case 3: open(Screen::Settings); break;
                case 4:
                    saves().flush();
                    game_.goToMenu();
                    break;
            }
            return;
        }
        y += 92;
    }
    if (ui.nav().back || (input().pressed(Action::Pause) && screenTime_ > 0.2f)) {
        close();
        game_.setPaused(false);
    }
}

void Menus::drawResults(Context& ui) {
    const ModeManager& mm = game_.modes();
    const ChallengeDef* d = mm.current();
    if (!d) {
        close();
        return;
    }
    const ModeResult& r = mm.result();
    float W = ui.width();
    float in = saturate(screenTime_ * 2.5f);
    ui.rect(Rect(0, 0, W, 1080), Vec4(0, 0, 0, 0.5f * in));
    Rect panel(W * 0.5f - 420, 150, 840, 780);
    ui.shadow(panel, 16, 24, 0.5f);
    ui.rect(panel, Vec4(0.05f, 0.05f, 0.07f, 0.94f), 16);
    ui.text(d->name, Vec2(W * 0.5f, panel.y + 30), 60.0f, ui.theme().text, FontStyle::Display, Align::Center);
    ui.text(r.reason, Vec2(W * 0.5f, panel.y + 108), 28.0f, r.completed || !d->lowerIsBetter ? ui.theme().good : ui.theme().bad, FontStyle::Bold, Align::Center);
    float pulse = 1.0f + 0.06f * std::sin(screenTime_ * 4.0f) * (r.medal > 0 ? 1.0f : 0.0f);
    medal(ui, Vec2(W * 0.5f, panel.y + 250), 70.0f * pulse * in, r.medal);
    const char* mn[4] = {"NO MEDAL", "BRONZE", "SILVER", "GOLD"};
    ui.text(mn[r.medal], Vec2(W * 0.5f, panel.y + 336), 40.0f, kMedalColors[r.medal], FontStyle::Display, Align::Center);
    ui.text(valueText(*d, r.value), Vec2(W * 0.5f, panel.y + 392), 72.0f, ui.theme().text, FontStyle::Display, Align::Center, 0.5f);
    if (r.newBest) ui.text("NEW PERSONAL BEST", Vec2(W * 0.5f, panel.y + 478), 28.0f, ui.theme().accent, FontStyle::Bold, Align::Center);
    float tx = panel.x + 120;
    for (int k = 0; k < 3; ++k) {
        medal(ui, Vec2(tx + 14, panel.y + 552), 13, k + 1);
        ui.text(valueText(*d, d->targets[k]), Vec2(tx + 36, panel.y + 534), 28.0f, ui.theme().textDim, FontStyle::Bold);
        tx += 220;
    }
    float bw = (panel.w - 140 - 40) / 3.0f;
    if (ui.button("RETRY", Rect(panel.x + 70, panel.y + 630, bw, 76), true)) {
        close();
        game_.restartRun();
        return;
    }
    if (ui.button("FREE RIDE", Rect(panel.x + 70 + bw + 20, panel.y + 630, bw, 76))) {
        close();
        game_.startFreeRide();
        return;
    }
    if (ui.button("MENU", Rect(panel.x + 70 + 2 * (bw + 20), panel.y + 630, bw, 76))) {
        game_.goToMenu();
        return;
    }
    if (ui.nav().left) ui.setFocus(std::max(0, ui.focusIndex() - 1));
    if (ui.nav().right) ui.setFocus(std::min(2, ui.focusIndex() + 1));
}

void Menus::drawQuit(Context& ui) {
    float W = ui.width();
    ui.rect(Rect(0, 0, W, 1080), Vec4(0, 0, 0, 0.55f));
    Rect panel(W * 0.5f - 360, 360, 720, 330);
    ui.shadow(panel, 16, 24, 0.5f);
    ui.rect(panel, Vec4(0.05f, 0.05f, 0.07f, 0.95f), 16);
    ui.text("Quit scoot would?", Vec2(W * 0.5f, panel.y + 50), 56.0f, ui.theme().text, FontStyle::Display, Align::Center);
    if (ui.button("QUIT", Rect(panel.x + 70, panel.y + 190, 270, 76), false)) {
        saves().flush();
        game_.requestQuit();
    }
    if (ui.button("STAY", Rect(panel.x + 380, panel.y + 190, 270, 76), true)) back();
    if (ui.nav().left) ui.setFocus(0);
    if (ui.nav().right) ui.setFocus(1);
}

void Menus::drawLoading(Context& ui) {
    float W = ui.width();
    ui.rect(Rect(0, 0, W, 1080), Vec4(0.02f, 0.02f, 0.03f, 1.0f));
    ui.text("LOADING", Vec2(W - 80, 960), 64.0f, ui.theme().text, FontStyle::Display, Align::Right);
    std::string what;
    for (auto& m : game_.maps())
        if (m.scene == pendingMap_) {
            what = m.name;
            for (auto& d : m.districts)
                if (d.spawn == pendingSpawn_) what += "  /  " + d.name;
        }
    ui.text(what, Vec2(W - 84, 900), 28.0f, ui.theme().accent, FontStyle::Bold, Align::Right);
    ui.text("Tip: flick the right stick in the air for whips and bar spins, hold RT + flick for grabs.", Vec2(80, 980), 26.0f, ui.theme().textDim,
            FontStyle::SemiBold);
}

}  // namespace sw
