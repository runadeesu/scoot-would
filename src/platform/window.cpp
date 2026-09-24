#include "platform/window.h"
#include "core/log.h"

#include <algorithm>

namespace sw {

bool Window::create(const std::string& title, int width, int height, WindowMode mode) {
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    window_ = SDL_CreateWindow(title.c_str(), width, height, flags);
    if (!window_) {
        LOG_CRITICAL("window: SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window_, 640, 360);
    setMode(mode, width, height);
    SDL_ShowWindow(window_);
    return true;
}

void Window::destroy() {
    if (window_) SDL_DestroyWindow(window_);
    window_ = nullptr;
}

void Window::setMode(WindowMode mode, int width, int height) {
    mode_ = mode;
    switch (mode) {
        case WindowMode::Windowed:
            SDL_SetWindowFullscreen(window_, false);
            SDL_SetWindowBordered(window_, true);
            SDL_SetWindowSize(window_, width, height);
            SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            break;
        case WindowMode::Borderless:
            // borderless fullscreen desktop window
            SDL_SetWindowFullscreenMode(window_, nullptr);
            SDL_SetWindowFullscreen(window_, true);
            break;
        case WindowMode::Fullscreen: {
            SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
            SDL_DisplayMode closest{};
            if (SDL_GetClosestFullscreenDisplayMode(display, width, height, 0.0f, true, &closest))
                SDL_SetWindowFullscreenMode(window_, &closest);
            else
                SDL_SetWindowFullscreenMode(window_, nullptr);
            SDL_SetWindowFullscreen(window_, true);
            break;
        }
    }
    SDL_SyncWindow(window_);
}

void Window::pixelSize(int& w, int& h) const { SDL_GetWindowSizeInPixels(window_, &w, &h); }

bool Window::minimized() const { return (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0; }

std::vector<DisplayResolution> Window::resolutions() const {
    std::vector<DisplayResolution> out;
    SDL_DisplayID display = window_ ? SDL_GetDisplayForWindow(window_) : SDL_GetPrimaryDisplay();
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count);
    if (modes) {
        for (int i = 0; i < count; ++i) {
            DisplayResolution r{modes[i]->w, modes[i]->h, modes[i]->refresh_rate};
            bool dup = std::any_of(out.begin(), out.end(), [&](const DisplayResolution& o) { return o.width == r.width && o.height == r.height; });
            if (!dup) out.push_back(r);
        }
        SDL_free(modes);
    }
    const DisplayResolution defaults[] = {{1280, 720, 60}, {1600, 900, 60}, {1920, 1080, 60}, {2560, 1440, 60}, {3840, 2160, 60}};
    if (out.empty())
        for (auto& d : defaults) out.push_back(d);
    std::sort(out.begin(), out.end(), [](const DisplayResolution& a, const DisplayResolution& b) {
        return a.width * a.height < b.width * b.height;
    });
    return out;
}

void Window::setMouseCaptured(bool captured) {
    captured_ = captured;
    SDL_SetWindowRelativeMouseMode(window_, captured);
}

}  // namespace sw
