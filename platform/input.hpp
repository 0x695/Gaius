// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — platform/input.hpp
//
// Unified input abstraction per GAIUS_MASTERPLAN.md section 5a point 4:
// "One internal 'command stream'... that mouse+keyboard, touch, and
// gamepad all feed into, rather than three parallel UI implementations."
//
// This is deliberately minimal for Phase 1 (a viewer has no build-mode
// drag-to-place yet -- that lands in Phase 5) but the shape is designed
// to not need reworking when drag-to-build arrives: Pan/Zoom/Select/
// Secondary/Quit already cover "the original's command-mode/scroll-mode
// toggle" concern called out in the masterplan, with room to add
// DragStart/DragMove/DragEnd for construction without changing this
// enum's meaning for existing commands.

#pragma once

#include <optional>
#include <vector>

union SDL_Event;

namespace gaius::platform {

enum class CommandType {
    Quit,
    Select,       // primary action: left click, single tap, gamepad A/South
    Secondary,    // secondary action: right click, two-finger tap, gamepad B/East
    PanBegin,     // pan gesture starts (middle-mouse-drag, single-finger-drag, right stick)
    PanMove,
    PanEnd,
    Zoom,         // relative zoom delta: scroll wheel, pinch gesture, gamepad triggers
    ToggleWindowMode,  // dev/debug binding in Phase 1; a real settings UI arrives Phase 9
    CycleTool,    // next construction tool: Tab, gamepad X/West. Added in Phase 5 for
                  // build mode -- exactly the kind of extension this enum's original
                  // comment anticipated ("room to add ... without changing this enum's
                  // meaning for existing commands").
    Hover,        // pointer moved with no button held. Mouse-only by nature: touch has
                  // no hover state and gamepad has no pointer, so those devices simply
                  // never emit this. UI that uses it must therefore treat it as
                  // enrichment (a label preview) and never as the only way to learn
                  // something -- otherwise it would become a mouse-only code path,
                  // which masterplan 5a point 4 exists to prevent.
};

struct Command {
    CommandType type;
    int x = 0, y = 0;        // physical window coordinates, where relevant (Select/Secondary/Pan*)
    int dx = 0, dy = 0;      // relative motion, for PanMove
    float zoom_delta = 0.0f;  // positive = zoom in, for Zoom
};

// Translates a raw SDL event into zero or one unified Command. Returns
// std::nullopt for events this layer doesn't care about (window focus
// changes, unhandled key codes, etc.) -- callers should keep polling
// SDL_PollEvent and feed every event through this rather than branching
// on event.type themselves, so touch/gamepad support added later doesn't
// require call-site changes.
//
// physical_w/physical_h: needed to convert normalized (0..1) touch-finger
// coordinates into physical pixel coordinates matching mouse events.
// Pass the window's current physical size; if omitted (<=0), finger
// events are dropped rather than reported with wrong coordinates.
std::optional<Command> translate_event(const SDL_Event& event, int physical_w = 0, int physical_h = 0);

}  // namespace gaius::platform
