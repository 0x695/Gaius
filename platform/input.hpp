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
#include <string>
#include <vector>

union SDL_Event;

namespace gaius::platform {

enum class CommandType {
    Quit,         // the window closed (SDL_QUIT). Leaving the game from inside it goes
                  // through the Settings screen's Exit, which Menu opens.
    Menu,         // the game menu (the Settings screen): Escape, gamepad Start, Android's
                  // Back. Added in Phase 9: until then Escape quit on the spot, which a
                  // handheld's or phone's Back button can't be allowed to do.
    PreviousTool, // the tool before: Shift+Tab, gamepad left shoulder.
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
    CycleVariant, // next variant of the selected tool (Forum grade, Workshop goods): V,
                  // gamepad right shoulder. Touch reaches the same thing by tapping the
                  // selected toolbar button again, so it's not a keyboard-only path.
    ToggleTime,   // pause / resume the simulation clock: Space, gamepad Y/North. The
                  // clock runs by default, so a device without either (touch) still
                  // sees the city evolve; pausing is enrichment, not a gate.
    SelectMove,   // pointer moved with the primary button held (mouse left-drag). Lets
                  // drag-built commands (roads, walls) follow the pointer the way the
                  // original's drag does. Enrichment in the same sense as Hover: every
                  // device can still place those one cell per Select, so nothing
                  // depends on it.
    Hover,        // pointer moved with no button held. Mouse-only by nature: touch has
                  // no hover state and gamepad has no pointer, so those devices simply
                  // never emit this. UI that uses it must therefore treat it as
                  // enrichment (a label preview) and never as the only way to learn
                  // something -- otherwise it would become a mouse-only code path,
                  // which masterplan 5a point 4 exists to prevent.
    CycleScreen,  // next screen -- city, province, Forum: M, gamepad Back. Touch and mouse
                  // reach the same screens through the strip of buttons along the top.
    CancelDrag,   // the original's cancel gesture for a drag-built command (Road, Wall,
                  // Plaza, Clear Area): "press the right button while still holding down
                  // the left" (manual, Building Roads). Right-click while the left button
                  // is also down, so it never collides with a plain right-click's own
                  // meaning (Secondary, the viewer's map-layer cycle) -- those two states
                  // are mutually exclusive by construction.
    TextKey,      // a key while text entry is on (set_text_entry): a typed character or
                  // an editing key. Keyboard enrichment for fields that can also be
                  // edited by pointer (the name dialog's letter arrows), so no screen
                  // depends on it; with text entry on, keys lose their other meanings.
    Rebound,      // a key or button was captured for a binding (capture_binding): the
                  // command it was bound to is in `rebound`.
};

// What a TextKey command carries.
enum class TextKey { Character, Escape, Enter, Backspace, Left, Right, Delete };

struct Command {
    CommandType type;
    int x = 0, y = 0;        // physical window coordinates, where relevant (Select/Secondary/Pan*);
                             // for a gamepad's A, where the gamepad pointer is
    int dx = 0, dy = 0;      // relative motion, for PanMove
    float zoom_delta = 0.0f;  // positive = zoom in, for Zoom
    TextKey text_key = TextKey::Character;  // for TextKey
    char ch = 0;                            // for TextKey::Character: printable ASCII
    CommandType rebound = CommandType::Quit;  // for Rebound
    bool from_gamepad = false;
};

// --- Bindings (Phase 9) ------------------------------------------------------
//
// The commands a key or a gamepad button triggers can be changed. Each
// bindable command has one key and one button; binding a key or button that
// another command has moves it (the other command loses it). Mouse and touch
// aren't bindable: their meanings are positional.
//
// Defaults -- keyboard: Menu Escape, ToggleWindowMode F11, CycleTool Tab,
// PreviousTool Shift+Tab (not rebindable: Shift with the CycleTool key),
// CycleVariant V, ToggleTime Space, CycleScreen M. Gamepad: Select A,
// Secondary B, CycleTool X, ToggleTime Y, PreviousTool left shoulder,
// CycleVariant right shoulder, CycleScreen Back, Menu Start. The left stick
// moves the gamepad pointer, the right stick and the d-pad pan the map, the
// triggers zoom.
inline constexpr CommandType kBindable[] = {CommandType::Menu,        CommandType::CycleTool,
                                            CommandType::PreviousTool, CommandType::CycleVariant,
                                            CommandType::ToggleTime,  CommandType::CycleScreen,
                                            CommandType::ToggleWindowMode, CommandType::Select,
                                            CommandType::Secondary};

// A stable name for a command ("cycle_tool"), for the settings file.
const char* command_name(CommandType type);
std::optional<CommandType> command_from_name(const std::string& name);

void reset_bindings();
// SDL keycodes and SDL_GameControllerButton values; -1 for none.
void bind_key(CommandType type, int keycode);
void bind_button(CommandType type, int button);
int key_for(CommandType type);
int button_for(CommandType type);
// The names the settings file and screen use: SDL's key names ("Tab") and
// button names ("a", "leftshoulder").
std::string key_name(int keycode);
std::string button_name(int button);
int key_from_name(const std::string& name);
int button_from_name(const std::string& name);
// What a button is called on the gamepad in use: "A" on an Xbox pad or the
// Steam Deck, "Cross" on a PlayStation pad, "B" for A's place on a Nintendo pad.
std::string button_label(int button);

// While capturing, the next key or gamepad button press binds to `type`
// (Escape on the keyboard cancels) and translate_event returns Rebound.
void capture_binding(CommandType type, bool gamepad);
bool capturing_binding();
void cancel_capture();

// --- Gamepads ----------------------------------------------------------------
//
// translate_event opens gamepads as they connect and closes them as they go
// (SDL only sends a controller's events once it's open). open_gamepads opens
// those already connected at start-up.
void open_gamepads();
bool gamepad_connected();
// The pads' sticks and triggers now, -1..1 (0..1 for triggers), the strongest
// of all connected pads; zero inside the dead zone.
struct GamepadAxes {
    float left_x = 0, left_y = 0, right_x = 0, right_y = 0, left_trigger = 0, right_trigger = 0;
};
GamepadAxes gamepad_axes();
bool gamepad_button_down(int button);  // any connected pad

// Turns text entry on or off (SDL_StartTextInput / SDL_StopTextInput, which on
// a phone brings up the on-screen keyboard). While on, key presses become
// TextKey commands instead of their usual commands.
void set_text_entry(bool on);
bool text_entry();

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
