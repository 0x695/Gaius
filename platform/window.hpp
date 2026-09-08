// Gaius — platform/window.hpp
//
// Thin SDL2 window/renderer wrapper implementing the resolution-
// independence architecture from GAIUS_MASTERPLAN.md section 5a:
// game/UI code always draws into a fixed-size LOGICAL framebuffer; this
// class is the only place that knows about the actual physical window
// size, scaling, and window mode. Nothing outside platform/ should ever
// call raw SDL windowing functions directly — this is the seam Phase 9
// (platform packaging) and any future renderer backend swap would need.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace gaius::platform {

enum class WindowMode {
    Windowed,
    Borderless,
    Fullscreen,
};

class Window {
public:
    // logical_w/logical_h define the fixed coordinate space all drawing
    // happens in (see masterplan: "an internal logical-resolution
    // framebuffer... composited to an arbitrary physical window/display
    // size at draw time"). initial_w/initial_h are the starting physical
    // window size (ignored for Fullscreen).
    Window(const std::string& title, int logical_w, int logical_h, int initial_w, int initial_h,
           WindowMode mode = WindowMode::Windowed);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void set_mode(WindowMode mode);
    WindowMode mode() const { return mode_; }

    // No-op outside Windowed mode.
    void resize(int w, int h);

    int logical_width() const { return logical_w_; }
    int logical_height() const { return logical_h_; }

    // Uploads an RGB24 buffer (logical_w * logical_h * 3 bytes) as the
    // frame content, then presents it scaled to fit the current physical
    // window size with letterboxing (preserves aspect ratio -- never
    // stretches/distorts, per the "resolution independence" requirement).
    void present_rgb24(const std::vector<uint8_t>& rgb);

    // Converts a physical window coordinate (e.g. from a mouse/touch
    // event) into logical framebuffer coordinates, accounting for the
    // current letterbox scaling/offset. Returns false if the point falls
    // in the letterbox padding (outside the logical content area).
    bool window_to_logical(int win_x, int win_y, int* logical_x, int* logical_y) const;

    void physical_size(int* w, int* h) const;

    SDL_Window* sdl_window() const { return window_; }

private:
    void recompute_viewport();

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    int logical_w_, logical_h_;
    WindowMode mode_;

    // Cached letterbox viewport (physical pixels) recomputed on resize.
    int viewport_x_ = 0, viewport_y_ = 0, viewport_w_ = 0, viewport_h_ = 0;
};

}  // namespace gaius::platform
