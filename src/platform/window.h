// scoot would - OS window (SDL3)
#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <vector>

namespace sw {

enum class WindowMode { Windowed = 0, Fullscreen, Borderless };

struct DisplayResolution {
    int width = 0, height = 0;
    float refresh = 0;
};

class Window {
public:
    bool create(const std::string& title, int width, int height, WindowMode mode);
    void destroy();
    SDL_Window* handle() const { return window_; }

    void setMode(WindowMode mode, int width, int height);
    WindowMode mode() const { return mode_; }
    void pixelSize(int& w, int& h) const;
    bool minimized() const;
    std::vector<DisplayResolution> resolutions() const;
    void setMouseCaptured(bool captured);
    bool mouseCaptured() const { return captured_; }

private:
    SDL_Window* window_ = nullptr;
    WindowMode mode_ = WindowMode::Windowed;
    bool captured_ = false;
};

}  // namespace sw
