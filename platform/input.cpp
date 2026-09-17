// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/input.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace gaius::platform {

namespace {
constexpr float kStickDeadzone = 0.20f;
constexpr float kTriggerDeadzone = 0.10f;
bool g_text_entry = false;

std::optional<Command> text_key(TextKey key) {
    Command c{CommandType::TextKey};
    c.text_key = key;
    return c;
}
}  // namespace

void set_text_entry(bool on) {
    if (on == g_text_entry) return;
    g_text_entry = on;
    if (on) {
        SDL_StartTextInput();
    } else {
        SDL_StopTextInput();
    }
}

bool text_entry() { return g_text_entry; }

namespace {

struct Binding {
    int key = -1;
    int button = -1;
};

std::map<CommandType, Binding>& bindings() {
    static std::map<CommandType, Binding> b;
    return b;
}

// The fingers down and the gesture they make.
struct Finger {
    SDL_FingerID id;
    float x, y;  // physical pixels
};
std::vector<Finger> g_fingers;
float g_touch_start_x = 0, g_touch_start_y = 0;
bool g_touch_moved = false;
int g_touch_most = 0;       // the most fingers down during the gesture
float g_pinch_distance = 0;  // two fingers apart, at the last step

bool g_capturing = false;
bool g_capture_gamepad = false;
CommandType g_capture_type = CommandType::Quit;
std::vector<SDL_GameController*> g_pads;

void open_pad(int device_index) {
    if (!SDL_IsGameController(device_index)) return;
    SDL_GameController* pad = SDL_GameControllerOpen(device_index);
    if (!pad) return;
    for (SDL_GameController* p : g_pads)
        if (p == pad) return;
    g_pads.push_back(pad);
}

void close_pad(int instance_id) {
    for (size_t i = 0; i < g_pads.size(); ++i) {
        SDL_Joystick* j = SDL_GameControllerGetJoystick(g_pads[i]);
        if (j && SDL_JoystickInstanceID(j) == instance_id) {
            SDL_GameControllerClose(g_pads[i]);
            g_pads.erase(g_pads.begin() + static_cast<long>(i));
            return;
        }
    }
}

std::optional<CommandType> command_for_key(int keycode) {
    for (const auto& [type, b] : bindings())
        if (b.key == keycode) return type;
    return std::nullopt;
}

std::optional<CommandType> command_for_button(int button) {
    for (const auto& [type, b] : bindings())
        if (b.button == button) return type;
    return std::nullopt;
}

struct BindingsInit {
    BindingsInit() { reset_bindings(); }
};
BindingsInit g_bindings_init;

}  // namespace

const char* command_name(CommandType type) {
    switch (type) {
        case CommandType::Menu: return "menu";
        case CommandType::CycleTool: return "cycle_tool";
        case CommandType::PreviousTool: return "previous_tool";
        case CommandType::CycleVariant: return "cycle_variant";
        case CommandType::ToggleTime: return "toggle_time";
        case CommandType::CycleScreen: return "cycle_screen";
        case CommandType::ToggleWindowMode: return "toggle_window_mode";
        case CommandType::Select: return "select";
        case CommandType::Secondary: return "secondary";
        default: return "";
    }
}

std::optional<CommandType> command_from_name(const std::string& name) {
    for (CommandType t : kBindable)
        if (name == command_name(t)) return t;
    return std::nullopt;
}

void reset_bindings() {
    auto& b = bindings();
    b.clear();
    b[CommandType::Menu] = {SDLK_ESCAPE, SDL_CONTROLLER_BUTTON_START};
    b[CommandType::ToggleWindowMode] = {SDLK_F11, -1};
    b[CommandType::CycleTool] = {SDLK_TAB, SDL_CONTROLLER_BUTTON_X};
    b[CommandType::PreviousTool] = {-1, SDL_CONTROLLER_BUTTON_LEFTSHOULDER};
    b[CommandType::CycleVariant] = {SDLK_v, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER};
    b[CommandType::ToggleTime] = {SDLK_SPACE, SDL_CONTROLLER_BUTTON_Y};
    b[CommandType::CycleScreen] = {SDLK_m, SDL_CONTROLLER_BUTTON_BACK};
    b[CommandType::Select] = {-1, SDL_CONTROLLER_BUTTON_A};
    b[CommandType::Secondary] = {-1, SDL_CONTROLLER_BUTTON_B};
}

void bind_key(CommandType type, int keycode) {
    if (keycode >= 0)
        for (auto& [t, b] : bindings())
            if (b.key == keycode) b.key = -1;
    bindings()[type].key = keycode;
}

void bind_button(CommandType type, int button) {
    if (button >= 0)
        for (auto& [t, b] : bindings())
            if (b.button == button) b.button = -1;
    bindings()[type].button = button;
}

int key_for(CommandType type) {
    const auto it = bindings().find(type);
    return it == bindings().end() ? -1 : it->second.key;
}

int button_for(CommandType type) {
    const auto it = bindings().find(type);
    return it == bindings().end() ? -1 : it->second.button;
}

std::string key_name(int keycode) {
    if (keycode < 0) return std::string();
    return SDL_GetKeyName(static_cast<SDL_Keycode>(keycode));
}

std::string button_name(int button) {
    if (button < 0) return std::string();
    const char* n = SDL_GameControllerGetStringForButton(static_cast<SDL_GameControllerButton>(button));
    return n ? n : std::string();
}

int key_from_name(const std::string& name) {
    const SDL_Keycode k = SDL_GetKeyFromName(name.c_str());
    return k == SDLK_UNKNOWN ? -1 : static_cast<int>(k);
}

int button_from_name(const std::string& name) {
    const SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(name.c_str());
    return b == SDL_CONTROLLER_BUTTON_INVALID ? -1 : static_cast<int>(b);
}

std::string button_label(int button) {
    if (button < 0) return std::string();
    SDL_GameControllerType type = SDL_CONTROLLER_TYPE_UNKNOWN;
    if (!g_pads.empty()) type = SDL_GameControllerGetType(g_pads.front());
    const bool playstation = type == SDL_CONTROLLER_TYPE_PS3 || type == SDL_CONTROLLER_TYPE_PS4 ||
                             type == SDL_CONTROLLER_TYPE_PS5;
    const bool nintendo = type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO;
    switch (button) {
        case SDL_CONTROLLER_BUTTON_A: return playstation ? "Cross" : nintendo ? "B" : "A";
        case SDL_CONTROLLER_BUTTON_B: return playstation ? "Circle" : nintendo ? "A" : "B";
        case SDL_CONTROLLER_BUTTON_X: return playstation ? "Square" : nintendo ? "Y" : "X";
        case SDL_CONTROLLER_BUTTON_Y: return playstation ? "Triangle" : nintendo ? "X" : "Y";
        case SDL_CONTROLLER_BUTTON_BACK: return playstation ? "Share" : nintendo ? "Minus" : "View";
        case SDL_CONTROLLER_BUTTON_START: return playstation ? "Options" : nintendo ? "Plus" : "Menu";
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return playstation ? "L1" : nintendo ? "L" : "LB";
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return playstation ? "R1" : nintendo ? "R" : "RB";
        case SDL_CONTROLLER_BUTTON_LEFTSTICK: return playstation ? "L3" : "LS";
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return playstation ? "R3" : "RS";
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return "Up";
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return "Down";
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return "Left";
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "Right";
        default: return button_name(button);
    }
}

void capture_binding(CommandType type, bool gamepad) {
    g_capturing = true;
    g_capture_gamepad = gamepad;
    g_capture_type = type;
}

bool capturing_binding() { return g_capturing; }

void cancel_capture() { g_capturing = false; }

void open_gamepads() {
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) return;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) open_pad(i);
}

bool gamepad_connected() { return !g_pads.empty(); }

GamepadAxes gamepad_axes() {
    GamepadAxes a;
    const auto strongest = [](float& into, float v) {
        if (std::fabs(v) > std::fabs(into)) into = v;
    };
    for (SDL_GameController* pad : g_pads) {
        const auto axis = [&](SDL_GameControllerAxis ax) {
            float v = SDL_GameControllerGetAxis(pad, ax) / 32767.0f;
            v = std::clamp(v, -1.0f, 1.0f);
            return std::fabs(v) < kStickDeadzone ? 0.0f : v;
        };
        strongest(a.left_x, axis(SDL_CONTROLLER_AXIS_LEFTX));
        strongest(a.left_y, axis(SDL_CONTROLLER_AXIS_LEFTY));
        strongest(a.right_x, axis(SDL_CONTROLLER_AXIS_RIGHTX));
        strongest(a.right_y, axis(SDL_CONTROLLER_AXIS_RIGHTY));
        const float lt = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f;
        const float rt = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f;
        strongest(a.left_trigger, lt < kTriggerDeadzone ? 0.0f : lt);
        strongest(a.right_trigger, rt < kTriggerDeadzone ? 0.0f : rt);
    }
    return a;
}

bool gamepad_button_down(int button) {
    for (SDL_GameController* pad : g_pads)
        if (SDL_GameControllerGetButton(pad, static_cast<SDL_GameControllerButton>(button))) return true;
    return false;
}

std::optional<Command> translate_event(const SDL_Event& event, int physical_w, int physical_h) {
    switch (event.type) {
        case SDL_QUIT:
            return Command{CommandType::Quit};

        case SDL_CONTROLLERDEVICEADDED:
            open_pad(event.cdevice.which);
            return std::nullopt;
        case SDL_CONTROLLERDEVICEREMOVED:
            close_pad(event.cdevice.which);
            return std::nullopt;

        case SDL_TEXTINPUT: {
            if (!g_text_entry) return std::nullopt;
            const unsigned char first = static_cast<unsigned char>(event.text.text[0]);
            if (first < 0x20 || first > 0x7E) return std::nullopt;
            Command c{CommandType::TextKey};
            c.ch = static_cast<char>(first);
            return c;
        }

        case SDL_KEYDOWN: {
            const SDL_Keycode sym = event.key.keysym.sym;
            if (g_capturing && !g_capture_gamepad) {
                g_capturing = false;
                if (sym == SDLK_ESCAPE) return std::nullopt;
                bind_key(g_capture_type, static_cast<int>(sym));
                Command c{CommandType::Rebound};
                c.rebound = g_capture_type;
                return c;
            }
            if (g_text_entry) {
                switch (sym) {
                    case SDLK_ESCAPE: return text_key(TextKey::Escape);
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER: return text_key(TextKey::Enter);
                    case SDLK_BACKSPACE: return text_key(TextKey::Backspace);
                    case SDLK_LEFT: return text_key(TextKey::Left);
                    case SDLK_RIGHT: return text_key(TextKey::Right);
                    case SDLK_DELETE: return text_key(TextKey::Delete);
                    default: return std::nullopt;
                }
            }
            if (sym == SDLK_AC_BACK) return Command{CommandType::Menu};  // Android's Back
            const std::optional<CommandType> bound = command_for_key(static_cast<int>(sym));
            if (!bound) return std::nullopt;
            if (*bound == CommandType::CycleTool && (event.key.keysym.mod & KMOD_SHIFT))
                return Command{CommandType::PreviousTool};
            if (*bound == CommandType::Select || *bound == CommandType::Secondary) {
                Command c{*bound};
                SDL_GetMouseState(&c.x, &c.y);
                return c;
            }
            return Command{*bound};
        }

        // --- Mouse: left=Select, right=Secondary, middle=Pan ---
        case SDL_MOUSEBUTTONDOWN: {
            if (event.button.which == SDL_TOUCH_MOUSEID) return std::nullopt;  // the fingers speak for touch
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
            if (event.motion.which == SDL_TOUCH_MOUSEID) return std::nullopt;
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
            if (event.button.which == SDL_TOUCH_MOUSEID) return std::nullopt;
            if (event.button.button == SDL_BUTTON_MIDDLE) {
                Command c;
                c.type = CommandType::PanEnd;
                c.x = event.button.x;
                c.y = event.button.y;
                return c;
            }
            return std::nullopt;

        case SDL_MOUSEWHEEL: {
            if (event.wheel.which == SDL_TOUCH_MOUSEID) return std::nullopt;
            Command c;
            c.type = CommandType::Zoom;
            c.zoom_delta = static_cast<float>(event.wheel.y);
            return c;
        }

        // --- Touch: taps, drags, pinches (see Command's comment). A finger
        // has to travel 1.5% of the screen's width before a touch is a drag
        // rather than a tap. Masterplan 5a point 7's question -- a drag that
        // builds without fighting a drag that pans -- is answered by the
        // viewer: one finger builds with a road-like tool and pans otherwise;
        // two fingers always pan.
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
        case SDL_FINGERUP: {
            if (physical_w <= 0 || physical_h <= 0) return std::nullopt;
            const float fx = event.tfinger.x * physical_w, fy = event.tfinger.y * physical_h;
            const float threshold = 0.015f * static_cast<float>(physical_w);
            const auto centroid = [](float* cx, float* cy) {
                *cx = *cy = 0;
                for (const Finger& f : g_fingers) {
                    *cx += f.x / static_cast<float>(g_fingers.size());
                    *cy += f.y / static_cast<float>(g_fingers.size());
                }
            };
            const auto spread = []() {
                if (g_fingers.size() < 2) return 0.0f;
                return std::hypot(g_fingers[0].x - g_fingers[1].x, g_fingers[0].y - g_fingers[1].y);
            };
            Command c;
            c.touch = true;
            if (event.type == SDL_FINGERDOWN) {
                if (g_fingers.empty()) {
                    g_touch_start_x = fx;
                    g_touch_start_y = fy;
                    g_touch_moved = false;
                    g_touch_most = 0;
                }
                g_fingers.push_back({event.tfinger.fingerId, fx, fy});
                g_touch_most = std::max(g_touch_most, static_cast<int>(g_fingers.size()));
                g_pinch_distance = spread();
                return std::nullopt;
            }
            auto it = std::find_if(g_fingers.begin(), g_fingers.end(),
                                   [&](const Finger& f) { return f.id == event.tfinger.fingerId; });
            if (it == g_fingers.end()) return std::nullopt;
            if (event.type == SDL_FINGERMOTION) {
                float before_x = 0, before_y = 0;
                centroid(&before_x, &before_y);
                it->x = fx;
                it->y = fy;
                float after_x = 0, after_y = 0;
                centroid(&after_x, &after_y);
                const bool was_moving = g_touch_moved;
                if (!g_touch_moved &&
                    std::hypot(after_x - g_touch_start_x, after_y - g_touch_start_y) > threshold) {
                    g_touch_moved = true;
                }
                if (!g_touch_moved) return std::nullopt;
                c.fingers = static_cast<int>(g_fingers.size());
                c.x = static_cast<int>(after_x);
                c.y = static_cast<int>(after_y);
                if (!was_moving && g_touch_most == 1) {
                    // The drag starts where the finger went down.
                    c.type = CommandType::PanBegin;
                    c.x = static_cast<int>(g_touch_start_x);
                    c.y = static_cast<int>(g_touch_start_y);
                    return c;
                }
                c.type = CommandType::PanMove;
                c.dx = static_cast<int>(after_x - before_x);
                c.dy = static_cast<int>(after_y - before_y);
                if (g_fingers.size() >= 2) {
                    const float d = spread();
                    if (g_pinch_distance > 1 && d > 1) c.zoom_delta = std::log(d / g_pinch_distance) / std::log(1.1f);
                    g_pinch_distance = d;
                }
                return c;
            }
            // SDL_FINGERUP
            g_fingers.erase(it);
            g_pinch_distance = spread();
            if (!g_fingers.empty()) return std::nullopt;
            c.x = static_cast<int>(fx);
            c.y = static_cast<int>(fy);
            c.fingers = g_touch_most;
            if (g_touch_moved) {
                c.type = CommandType::PanEnd;
            } else if (g_touch_most >= 2) {
                c.type = CommandType::Secondary;
            } else {
                c.type = CommandType::Select;
            }
            return c;
        }

        // --- Gamepad buttons, through the bindings. A and B act where the
        // gamepad pointer is (the mouse position, which the viewer moves
        // with the left stick). The sticks and triggers are read each frame
        // (gamepad_axes), not as events.
        case SDL_CONTROLLERBUTTONDOWN: {
            const int button = event.cbutton.button;
            if (g_capturing && g_capture_gamepad) {
                g_capturing = false;
                bind_button(g_capture_type, button);
                Command c{CommandType::Rebound};
                c.rebound = g_capture_type;
                c.from_gamepad = true;
                return c;
            }
            const std::optional<CommandType> bound = command_for_button(button);
            if (!bound) return std::nullopt;
            Command c{*bound};
            c.from_gamepad = true;
            if (*bound == CommandType::Select || *bound == CommandType::Secondary) SDL_GetMouseState(&c.x, &c.y);
            return c;
        }

        default:
            return std::nullopt;
    }
}

}  // namespace gaius::platform
