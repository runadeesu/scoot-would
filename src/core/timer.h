// scoot would - timing and lightweight CPU profiler
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace sw {

class Timer {
public:
    Timer() { reset(); }
    void reset();
    double seconds() const;
    double milliseconds() const { return seconds() * 1000.0; }
    static double now();  // seconds since app start (high resolution)

private:
    uint64_t start_ = 0;
};

// Rolling statistics per profiler section. Sections are fixed so the overlay can
// show them in a stable order.
enum class ProfileSection : int {
    Frame = 0,
    Input,
    Physics,
    Gameplay,
    Animation,
    Camera,
    RenderPrep,
    Render,
    GpuWait,
    Audio,
    UI,
    Count
};

class Profiler {
public:
    static void begin(ProfileSection s);
    static void end(ProfileSection s);
    static void add(ProfileSection s, double ms);
    static void endFrame();
    static double average(ProfileSection s);  // ms
    static double last(ProfileSection s);     // ms
    static const char* name(ProfileSection s);
    static const std::array<float, 240>& frameHistory();
    static int frameHistoryOffset();

    // render statistics collected by the renderer each frame
    struct RenderStats {
        uint32_t drawCalls = 0;
        uint32_t triangles = 0;
        uint32_t instances = 0;
        uint32_t culledObjects = 0;
        uint32_t visibleObjects = 0;
        uint32_t shadowDrawCalls = 0;
        uint32_t lights = 0;
    };
    static RenderStats& renderStats();
    static const RenderStats& lastRenderStats();
};

struct ProfileScope {
    ProfileSection s;
    explicit ProfileScope(ProfileSection sec) : s(sec) { Profiler::begin(s); }
    ~ProfileScope() { Profiler::end(s); }
};

}  // namespace sw

#define SW_PROFILE(section) ::sw::ProfileScope sw_profile_scope_##__LINE__(::sw::ProfileSection::section)
