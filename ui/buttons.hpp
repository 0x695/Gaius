// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — ui/buttons.hpp
//
// The original's interface buttons: 16-byte records of a cell, a frame, a
// pressed frame, a handler, a state word and a mode, run once a frame by
// 0x0D41D (answering the mouse) and drawn by 0x0D521 from P_BLOCKS.PL8.
// Transcribed from the US-build CSR.EXE; findings section 42.1.

#pragma once

#include <vector>

#include "formats/common/types.hpp"

namespace gaius::ui {

enum class ButtonMode {
    Momentary = 0,  // acts while held: at once, then every few frames after 12
    Toggle = 1,     // acts once a press; the state flips and picks the frame
    Radio = 2,      // acts every held frame; the chosen one (DS:0x6D60) shows pressed
    Release = 3,    // acts when the left button is let go over it
};

struct Button {
    int cx = 0, cy = 0;            // +0x00, +0x02: the cell, 16 pixels each
    int frame = 0, pressed = 0;    // +0x04, +0x06: P_BLOCKS.PL8 frames
    ButtonMode mode = ButtonMode::Momentary;  // +0x0E
    int state = 0;                 // +0x0C: the pressed-frame timer, or the toggle's state
};

// The mouse as 0x113E1 reads it each frame: DS:0x6D56 the left button held,
// DS:0x6D4E the left button let go this frame.
struct Pointer {
    int x = -1, y = -1;
    bool left_held = false;
    bool left_released = false;
};

struct ButtonTracker {
    int held_frames = 0;  // DS:0x6D64
    int radio = 0;        // DS:0x6D60
    int timer = 10;       // DS:0x6D62: a momentary button's pressed frames (25 on the start screen)
};

// 0x0D41D: the index of the button whose handler runs this frame, or -1.
int process_buttons(std::vector<Button>& buttons, ButtonTracker& tracker, const Pointer& pointer);

// 0x0D521: every button, pressed or not, drawn whole at its cell. Counts a
// momentary button's timer down as it goes.
void draw_buttons(formats::IndexedImage& img, const formats::PL8Sheet& blocks, std::vector<Button>& buttons,
                  const ButtonTracker& tracker, const Pointer& pointer);

// The button at a logical point, or -1.
int button_at(const std::vector<Button>& buttons, int x, int y);

}  // namespace gaius::ui
