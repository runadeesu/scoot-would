#include "game/combo/combo_manager.h"

#include <algorithm>

namespace sw {

void ComboManager::addTrick(const std::string& name, int score, const std::string& repeatKey) {
    int repeats = 0;
    for (const auto& e : entries_)
        if (e.name == repeatKey) ++repeats;
    static const float penalty[] = {1.0f, 0.7f, 0.45f, 0.3f, 0.2f};
    float f = penalty[std::min(repeats, 4)];
    ComboEntry e;
    e.name = repeatKey;
    e.baseScore = score;
    e.score = int(float(score) * f);
    e.repeat = repeats;
    entries_.push_back(e);
    comboScore_ += e.score;
    // unique tricks raise the multiplier more than repeats
    multiplier_ += repeats == 0 ? 1.0f : 0.25f;
    if (entries_.size() == 1) multiplier_ = 1.0f;
    keepAlive();
    lastComboName = name;
}

void ComboManager::addSegment(const std::string& name, int score) {
    ComboEntry e;
    e.name = name;
    e.baseScore = score;
    int repeats = 0;
    for (const auto& x : entries_)
        if (x.name == name) ++repeats;
    e.score = repeats > 2 ? score / 2 : score;
    e.repeat = repeats;
    entries_.push_back(e);
    comboScore_ += e.score;
    if (entries_.size() == 1)
        multiplier_ = 1.0f;
    else
        multiplier_ += repeats == 0 ? 0.5f : 0.1f;
    keepAlive();
}

void ComboManager::keepAlive(float window) {
    window_ = window;
    timer_ = window;
}

void ComboManager::update(float dt, bool timerRunning) {
    bannerTime += dt;
    if (entries_.empty()) return;
    if (timerRunning) timer_ -= dt;
    if (timer_ <= 0.0f) bank();
}

int ComboManager::bank() {
    if (entries_.empty()) return 0;
    int pts = comboTotal();
    total_ += pts;
    best_ = std::max(best_, pts);
    lastBanked_ = pts;
    lastFailed_ = false;
    bannerTime = 0.0f;
    entries_.clear();
    comboScore_ = 0;
    multiplier_ = 1.0f;
    timer_ = 0.0f;
    return pts;
}

void ComboManager::fail() {
    if (!entries_.empty()) {
        lastFailed_ = true;
        lastBanked_ = comboTotal();
        bannerTime = 0.0f;
    }
    entries_.clear();
    comboScore_ = 0;
    multiplier_ = 1.0f;
    timer_ = 0.0f;
}

void ComboManager::reset() {
    fail();
    total_ = 0;
    best_ = 0;
    lastBanked_ = 0;
    lastFailed_ = false;
    bannerTime = 100.0f;
}

}  // namespace sw
