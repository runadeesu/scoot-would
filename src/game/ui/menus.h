// scoot would - menus: main menu, mode / challenge / map select, rider + scooter customization,
// settings (graphics, gameplay, audio, controls with rebinding), pause, results
#pragma once

#include "game/modes/mode_manager.h"
#include "game/ui/glyphs.h"
#include "game/ui/part_thumbs.h"
#include "ui/ui.h"

#include <string>
#include <vector>

namespace sw {

class Game;

class Menus {
public:
    enum class Screen { None, Main, Play, Challenges, Map, Rider, Scooter, Settings, Pause, Results, Quit, Loading };

    explicit Menus(Game& g) : game_(g) {}
    void open(Screen s, bool push = true);
    void back();
    void close();
    Screen screen() const { return screen_; }
    bool active() const { return screen_ != Screen::None; }
    bool customizing() const { return screen_ == Screen::Rider || screen_ == Screen::Scooter; }
    int shopCategory() const { return shopCat_; }  // scooter shop: part category being edited
    void setShopCategory(int c) { shopCat_ = std::clamp(c, 0, 5); }
    void setSettingsTab(int t) { settingsTab_ = std::clamp(t, 0, 4); }
    void draw(ui::Context& ui, float dt);
    void setChallengeType(ModeType t) { listType_ = t; }
    // a map load is shown for one frame before it happens
    void requestLoad(const std::string& map, const std::string& spawn, const std::string& challenge);
    bool loadPending() const { return !pendingMap_.empty(); }
    void performPendingLoad();

private:
    void drawMain(ui::Context& ui);
    void drawPlay(ui::Context& ui);
    void drawChallenges(ui::Context& ui);
    void drawMap(ui::Context& ui);
    void drawRider(ui::Context& ui);
    void drawScooter(ui::Context& ui);
    void shopHints(ui::Context& ui);
    void drawMoveList(ui::Context& ui, const ui::Rect& area);
    void drawTrickList(ui::Context& ui, const ui::Rect& area);
    void drawSettings(ui::Context& ui);
    void drawPause(ui::Context& ui);
    void drawResults(ui::Context& ui);
    void drawQuit(ui::Context& ui);
    void drawLoading(ui::Context& ui);
    void header(ui::Context& ui, const std::string& title, const std::string& subtitle = "");
    void footer(ui::Context& ui, const std::vector<glyphs::Hint>& hints, const std::string& note = "");
    void medal(ui::Context& ui, Vec2 c, float r, int medal);
    void customizationChanged();
    void settingsChanged(bool video);

    Game& game_;
    Screen screen_ = Screen::None;
    std::vector<Screen> stack_;
    float screenTime_ = 0.0f;
    ModeType listType_ = ModeType::Trick;
    int settingsTab_ = 0;
    float scroll_ = 0.0f;
    int rebindAction_ = -1;   // action index waiting for a button / key
    float rebindTime_ = 0.0f;
    std::string pendingMap_, pendingSpawn_, pendingChallenge_;
    int loadingFrames_ = 0;
    std::string toast_;
    float toastTime_ = 10.0f;
    // scooter shop
    int shopCat_ = 0, shopFocus_ = -1, shopPreviewKey_ = -1;
    float shopScroll_ = 0.0f;
    PartThumbnails thumbs_;
};

}  // namespace sw
