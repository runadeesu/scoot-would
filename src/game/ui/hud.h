// scoot would - in-game HUD: trick name, combo chain, score, multiplier, flow timer, speed,
// grind / manual balance meters, challenge timer + objective, gate compass, toasts
#pragma once

#include "game/events.h"
#include "render/renderer.h"
#include "ui/ui.h"

#include <string>
#include <vector>

namespace sw {

class Game;

class Hud {
public:
    explicit Hud(Game& g) : game_(g) {}
    void onEvent(const GameEvent& e);
    void draw(ui::Context& ui, float dt, const RenderView& view);
    void toast(const std::string& text, float seconds = 3.0f);
    void reset();

private:
    float hintAge_ = 0.0f;   // control hints fade out after the first seconds of a ride / first tricks
    int hintTricks_ = 0;
    void drawCombo(ui::Context& ui);
    void drawControlHints(ui::Context& ui, float dt);
    void drawTrickPopup(ui::Context& ui);
    void drawSpeed(ui::Context& ui);
    void drawBalance(ui::Context& ui, const RenderView& view);
    void drawMode(ui::Context& ui, const RenderView& view);
    void drawToasts(ui::Context& ui);

    Game& game_;
    struct Popup {
        std::string text, sub;
        int score = 0;
        int landing = 0;
        float age = 0.0f;
    };
    std::vector<Popup> popups_;
    std::string banner_;
    int bannerScore_ = 0;
    bool bannerFail_ = false;
    float bannerAge_ = 10.0f;
    std::string toast_;
    float toastAge_ = 10.0f, toastLife_ = 3.0f;
    std::string area_;
    float areaAge_ = 10.0f;
    std::string track_;
    float trackAge_ = 10.0f;
    float speedShown_ = 0.0f;
    float time_ = 0.0f;
    int lastCountdown_ = -1;
};

}  // namespace sw
