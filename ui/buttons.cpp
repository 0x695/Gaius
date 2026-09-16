// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/buttons.hpp"

#include "ui/interface.hpp"

namespace gaius::ui {

int process_buttons(std::vector<Button>& buttons, ButtonTracker& tracker, const Pointer& pointer) {
    const int cx = pointer.x >> 4, cy = pointer.y >> 4;
    int fired = -1;
    for (size_t i = 0; i < buttons.size(); ++i) {
        Button& b = buttons[i];
        const bool over = pointer.x >= 0 && pointer.y >= 0 && b.cx == cx && b.cy == cy;
        if (over && pointer.left_held) {
            switch (b.mode) {
                case ButtonMode::Toggle:
                    // 0x0D457: once a press.
                    if (tracker.held_frames++ == 0) {
                        b.state ^= 1;
                        return static_cast<int>(i);
                    }
                    return -1;
                case ButtonMode::Momentary:
                    // 0x0D489: at once, and after 12 held frames whenever the
                    // timer has run down below 2.
                    if ((tracker.held_frames == 0 || tracker.held_frames >= 12) && b.state < 2) {
                        b.state = tracker.timer;
                        ++tracker.held_frames;
                        return static_cast<int>(i);
                    }
                    if (b.state > 0) --b.state;
                    ++tracker.held_frames;
                    return -1;
                case ButtonMode::Radio:
                    // 0x0D4D0.
                    tracker.radio = static_cast<int>(i);
                    b.state = 1;
                    return static_cast<int>(i);
                case ButtonMode::Release:
                    break;  // held does nothing; it waits for the release
            }
            continue;
        }
        if (over && pointer.left_released && b.mode == ButtonMode::Release) {
            // 0x0D4EC: the pressed frame for 5 frames, and the handler.
            b.state = 5;
            fired = static_cast<int>(i);
        }
    }
    tracker.held_frames = 0;  // 0x0D517
    return fired;
}

void draw_buttons(formats::IndexedImage& img, const formats::PL8Sheet& blocks, std::vector<Button>& buttons,
                  const ButtonTracker& tracker, const Pointer& pointer) {
    const int cx = pointer.x >> 4, cy = pointer.y >> 4;
    for (size_t i = 0; i < buttons.size(); ++i) {
        Button& b = buttons[i];
        bool down = false;
        switch (b.mode) {
            case ButtonMode::Toggle: down = b.state > 0; break;
            case ButtonMode::Radio: down = static_cast<int>(i) == tracker.radio; break;
            default:
                // 0x0D5C9: the timer, or held down over it (which takes the
                // timer below zero, as the original does).
                if (b.state > 0 ||
                    (pointer.x >= 0 && pointer.y >= 0 && b.cx == cx && b.cy == cy && pointer.left_held)) {
                    --b.state;
                    down = true;
                }
                break;
        }
        draw_block(img, blocks, down ? b.pressed : b.frame, b.cx * 16, b.cy * 16);
    }
}

int button_at(const std::vector<Button>& buttons, int x, int y) {
    if (x < 0 || y < 0) return -1;
    for (size_t i = 0; i < buttons.size(); ++i)
        if (buttons[i].cx == x / 16 && buttons[i].cy == y / 16) return static_cast<int>(i);
    return -1;
}

}  // namespace gaius::ui
