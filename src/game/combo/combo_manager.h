// scoot would - combo manager: chains air tricks, grinds and manuals into one scored combo
#pragma once

#include <string>
#include <vector>

namespace sw {

struct ComboEntry {
    std::string name;
    int baseScore = 0;
    int score = 0;  // after the repeat penalty
    int repeat = 0;
};

class ComboManager {
public:
    // tricks: repeating the same trick inside one combo is worth less each time
    void addTrick(const std::string& name, int score, const std::string& repeatKey);
    // continuous segments (grind / manual) add score and extend the combo
    void addSegment(const std::string& name, int score);
    void keepAlive(float window = 1.1f);  // landing / linking resets the flow timer
    void update(float dt, bool timerRunning);
    int bank();   // combo finished: returns banked points
    void fail();  // bail: combo lost
    void reset();

    bool active() const { return !entries_.empty(); }
    const std::vector<ComboEntry>& entries() const { return entries_; }
    int comboScore() const { return comboScore_; }
    float multiplier() const { return multiplier_; }
    int comboTotal() const { return int(float(comboScore_) * multiplier_); }
    float timer() const { return timer_; }
    float timerMax() const { return window_; }
    long long totalScore() const { return total_; }
    int bestCombo() const { return best_; }
    int lastBanked() const { return lastBanked_; }
    bool lastFailed() const { return lastFailed_; }
    std::string lastComboName;
    float bannerTime = 0.0f;  // UI: time since the last bank / fail
    int banks = 0, fails = 0; // counters (event detection)
    std::vector<ComboEntry> lastEntries;  // tricks of the last banked / failed combo

private:
    std::vector<ComboEntry> entries_;
    int comboScore_ = 0;
    float multiplier_ = 1.0f;
    float timer_ = 0.0f;
    float window_ = 1.1f;
    long long total_ = 0;
    int best_ = 0;
    int lastBanked_ = 0;
    bool lastFailed_ = false;
};

}  // namespace sw
