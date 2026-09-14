// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/input.hpp"

#include <SDL.h>

#include <cmath>

namespace gaius::platform {

namespace {
constexpr float kStickDeadzone = 0.20f;
constexpr float kTriggerDeadzone = 0.10f;
}  // namespace

std::optional<Command> translate_event(const SDL_Event& event, int physical_w, int physical_h) {
    switch (event.type) {
        case SDL_QUIT:
            return Command{CommandType::Quit};

        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_ESCAPE) return Command{CommandType::Quit};
            if (event.key.keysym.sym == SDLK_F11) return Command{CommandType::ToggleWindowMode};
            if (event.key.keysym.sym == SDLK_TAB) return Command{CommandType::CycleTool};
            if (event.key.keysym.sym == SDLK_v) return Command{CommandType::CycleVariant};
            if (event.key.keysym.sym == SDLK_SPACE) return Command{CommandType::ToggleTime};
            return std::nullopt;

        // --- Mouse: left=Select, right=Secondary, middle=Pan ---
        case SDL_MOUSEBUTTONDOWN: {
            Command c;
            c.x = event.button.x;
            c.y = event.button.y;
            if (event.button.button == SDL_BUTTON_LEFT) {
                c.type = CommandType::Select;
                return c;
            }
            if (event.button.button == SDL_BUTTON_RIGHT) {
                // The original's drag-cancel gesture: right button while the
                // left is still held down (SDL_GetMouseState reflects the
                // left button's current state, not just this event's own).
                c.type = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) ? CommandType::CancelDrag
                                                                                   : CommandType::Secondary;
                return c;
            }
            if (event.button.button == SDL_BUTTON_MIDDLE) {
                c.type = CommandType::PanBegin;
                return c;
            }
            return std::nullopt;
        }

        case SDL_MOUSEMOTION: {
            if ((event.motion.state & SDL_BUTTON_LMASK) && !(event.motion.state & SDL_BUTTON_MMASK)) {
                Command drag;
                drag.type = CommandType::SelectMove;
                drag.x = event.motion.x;
                drag.y = event.motion.y;
                return drag;
            }
            if (!(event.motion.state & SDL_BUTTON_MMASK)) {
                Command hover;
                hover.type = CommandType::Hover;
                hover.x = event.motion.x;
                hover.y = event.motion.y;
                return hover;
            }
            Command c;
            c.type = CommandType::PanMove;
            c.x = event.motion.x;
            c.y = event.motion.y;
            c.dx = event.motion.xrel;
            c.dy = event.motion.yrel;
            return c;
        }

        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_MIDDLE) {
                Command c;
                c.type = CommandType::PanEnd;
                c.x = event.button.x;
                c.y = event.button.y;
                return c;
            }
            return std::nullopt;

        case SDL_MOUSEWHEEL: {
            Command c;
            c.type = CommandType::Zoom;
            c.zoom_delta = static_cast<float>(event.wheel.y);
            return c;
        }

        // --- Touch: single finger drag = pan. Tap-vs-drag / pinch-to-
        // zoom / two-finger-secondary gestures are intentionally NOT
        // implemented yet -- masterplan section 5a flags this as a real
        // design decision to make deliberately (point 7: "drag-to-build
        // needs a touch gesture that doesn't fight with map-panning"),
        // not something to bolt on casually here. This is enough to prove
        // the abstraction shape without pretending gesture design is done.
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
        case SDL_FINGERUP: {
            if (physical_w <= 0 || physical_h <= 0) return std::nullopt;
            Command c;
            c.x = static_cast<int>(event.tfinger.x * physical_w);
            c.y = static_cast<int>(event.tfinger.y * physical_h);
            c.dx = static_cast<int>(event.tfinger.dx * physical_w);
            c.dy = static_cast<int>(event.tfinger.dy * physical_h);
            c.type = (event.type == SDL_FINGERDOWN)   ? CommandType::PanBegin
                     : (event.type == SDL_FINGERMOTION) ? CommandType::PanMove
                                                          : CommandType::PanEnd;
            return c;
        }

        // --- Gamepad: A/South=Select, B/East=Secondary, left stick=pan,
        // triggers=zoom. Continuous-axis events are reported as PanMove/
        // Zoom deltas directly (no separate begin/end pairing -- a
        // simplification acceptable for a Phase 1 skeleton; Phase 9's
        // Steam Deck pass is where this gets a real look, per
        // GAIUS_ROADMAP.md).
        case SDL_CONTROLLERBUTTONDOWN: {
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) return Command{CommandType::Select};
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) return Command{CommandType::Secondary};
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_X) return Command{CommandType::CycleTool};
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_Y) return Command{CommandType::ToggleTime};
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) return Command{CommandType::CycleVariant};
            return std::nullopt;
        }

        case SDL_CONTROLLERAXISMOTION: {
            float norm = event.caxis.value / 32768.0f;
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                if (std::fabs(norm) < kStickDeadzone) return std::nullopt;
                Command c;
                c.type = CommandType::PanMove;
                if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
                    c.dx = static_cast<int>(norm * 20);
                else
                    c.dy = static_cast<int>(norm * 20);
                return c;
            }
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
                event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                if (norm < kTriggerDeadzone) return std::nullopt;
                Command c;
                c.type = CommandType::Zoom;
                c.zoom_delta = (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) ? norm : -norm;
                return c;
            }
            return std::nullopt;
        }

        default:
            return std::nullopt;
    }
}

}  // namespace gaius::platform
