#include "platform/window.hpp"

#include <SDL.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace gaius::platform {

Window::Window(const std::string& title, int logical_w, int logical_h, int initial_w, int initial_h,
               WindowMode mode)
    : logical_w_(logical_w), logical_h_(logical_h), mode_(mode) {
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            throw std::runtime_error(std::string("platform::Window: SDL_InitSubSystem failed: ") + SDL_GetError());
        }
    }

    uint32_t flags = SDL_WINDOW_RESIZABLE;
    if (mode == WindowMode::Borderless) flags |= SDL_WINDOW_BORDERLESS;
    if (mode == WindowMode::Fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, initial_w, initial_h,
                                flags);
    if (!window_) throw std::runtime_error(std::string("platform::Window: SDL_CreateWindow failed: ") + SDL_GetError());

    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        // Fall back to software rendering -- important for headless/CI
        // environments (e.g. SDL_VIDEODRIVER=dummy) and low-end targets
        // like Raspberry Pi where an accelerated driver might not be
        // available at all.
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer_) throw std::runtime_error(std::string("platform::Window: SDL_CreateRenderer failed: ") + SDL_GetError());

    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, logical_w_, logical_h_);
    if (!texture_) throw std::runtime_error(std::string("platform::Window: SDL_CreateTexture failed: ") + SDL_GetError());

    recompute_viewport();
}

Window::~Window() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
}

void Window::set_mode(WindowMode mode) {
    mode_ = mode;
    switch (mode) {
        case WindowMode::Windowed:
            SDL_SetWindowFullscreen(window_, 0);
            SDL_SetWindowBordered(window_, SDL_TRUE);
            break;
        case WindowMode::Borderless:
            SDL_SetWindowFullscreen(window_, 0);
            SDL_SetWindowBordered(window_, SDL_FALSE);
            break;
        case WindowMode::Fullscreen:
            SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN_DESKTOP);
            break;
    }
    recompute_viewport();
}

void Window::resize(int w, int h) {
    if (mode_ != WindowMode::Windowed) return;  // fullscreen/borderless follow the desktop size
    SDL_SetWindowSize(window_, w, h);
    recompute_viewport();
}

void Window::physical_size(int* w, int* h) const { SDL_GetWindowSize(window_, w, h); }

void Window::recompute_viewport() {
    int pw, ph;
    physical_size(&pw, &ph);
    if (pw <= 0 || ph <= 0) return;

    // Letterbox: fit logical_w x logical_h into pw x ph preserving aspect ratio.
    double scale = std::min(static_cast<double>(pw) / logical_w_, static_cast<double>(ph) / logical_h_);
    viewport_w_ = static_cast<int>(logical_w_ * scale);
    viewport_h_ = static_cast<int>(logical_h_ * scale);
    viewport_x_ = (pw - viewport_w_) / 2;
    viewport_y_ = (ph - viewport_h_) / 2;
}

void Window::present_rgb24(const std::vector<uint8_t>& rgb) {
    recompute_viewport();  // cheap; handles window resizes without a separate event hookup

    void* pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(texture_, nullptr, &pixels, &pitch) != 0) {
        throw std::runtime_error(std::string("platform::Window: SDL_LockTexture failed: ") + SDL_GetError());
    }
    size_t row_bytes = static_cast<size_t>(logical_w_) * 3;
    for (int y = 0; y < logical_h_; ++y) {
        std::memcpy(static_cast<uint8_t*>(pixels) + static_cast<size_t>(y) * pitch, rgb.data() + y * row_bytes,
                    row_bytes);
    }
    SDL_UnlockTexture(texture_);

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);  // letterbox bars
    SDL_RenderClear(renderer_);

    SDL_Rect dst{viewport_x_, viewport_y_, viewport_w_, viewport_h_};
    SDL_RenderCopy(renderer_, texture_, nullptr, &dst);
    SDL_RenderPresent(renderer_);
}

bool Window::window_to_logical(int win_x, int win_y, int* logical_x, int* logical_y) const {
    if (win_x < viewport_x_ || win_y < viewport_y_ || win_x >= viewport_x_ + viewport_w_ ||
        win_y >= viewport_y_ + viewport_h_) {
        return false;  // in the letterbox padding
    }
    *logical_x = (win_x - viewport_x_) * logical_w_ / viewport_w_;
    *logical_y = (win_y - viewport_y_) * logical_h_ / viewport_h_;
    return true;
}

}  // namespace gaius::platform
