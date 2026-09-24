#include "core/timer.h"

#include <SDL3/SDL.h>

namespace sw {

static double perfFreqInv() {
    static double inv = 1.0 / double(SDL_GetPerformanceFrequency());
    return inv;
}

void Timer::reset() { start_ = SDL_GetPerformanceCounter(); }
double Timer::seconds() const { return double(SDL_GetPerformanceCounter() - start_) * perfFreqInv(); }
double Timer::now() {
    static uint64_t base = SDL_GetPerformanceCounter();
    return double(SDL_GetPerformanceCounter() - base) * perfFreqInv();
}

namespace {
constexpr int kCount = int(ProfileSection::Count);
struct SectionData {
    double beginTime = 0;
    double accum = 0;  // this frame
    double last = 0;
    double avg = 0;
};
SectionData g_sections[kCount];
std::array<float, 240> g_history{};
int g_historyOffset = 0;
Profiler::RenderStats g_stats, g_lastStats;
}  // namespace

void Profiler::begin(ProfileSection s) { g_sections[int(s)].beginTime = Timer::now(); }
void Profiler::end(ProfileSection s) {
    SectionData& d = g_sections[int(s)];
    d.accum += (Timer::now() - d.beginTime) * 1000.0;
}
void Profiler::add(ProfileSection s, double ms) { g_sections[int(s)].accum += ms; }

void Profiler::endFrame() {
    for (auto& d : g_sections) {
        d.last = d.accum;
        d.avg = d.avg * 0.95 + d.accum * 0.05;
        d.accum = 0;
    }
    g_history[g_historyOffset] = float(g_sections[int(ProfileSection::Frame)].last);
    g_historyOffset = (g_historyOffset + 1) % int(g_history.size());
    g_lastStats = g_stats;
    g_stats = RenderStats{};
}

double Profiler::average(ProfileSection s) { return g_sections[int(s)].avg; }
double Profiler::last(ProfileSection s) { return g_sections[int(s)].last; }
const std::array<float, 240>& Profiler::frameHistory() { return g_history; }
int Profiler::frameHistoryOffset() { return g_historyOffset; }
Profiler::RenderStats& Profiler::renderStats() { return g_stats; }
const Profiler::RenderStats& Profiler::lastRenderStats() { return g_lastStats; }

const char* Profiler::name(ProfileSection s) {
    static const char* names[] = {"Frame", "Input", "Physics", "Gameplay", "Animation", "Camera",
                                  "RenderPrep", "Render", "GPU wait", "Audio", "UI"};
    return names[int(s)];
}

}  // namespace sw
