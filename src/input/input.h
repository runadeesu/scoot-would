// scoot would - input manager (gamepad first, keyboard + mouse fallback)
// Actions are bound per control layout: config/input.json (Scooter Flow style, default) or
// config/input_classic.json, with the user's overrides in <user>/input_flow.json / input_classic.json.
#pragma once

#include "core/json.h"
#include "core/math.h"

#include <SDL3/SDL.h>

#include <array>
#include <deque>
#include <string>
#include <vector>

namespace sw {

enum class Action : int {
    Push = 0,
    Brake,
    Jump,        // hold = crouch, release = pop
    SpinLeft,    // LB
    SpinRight,   // RB
    Grab,        // grab modifier (Scooter Flow layout: LT, classic: RT)
    Revert,      // 180 revert / switch stance on the ground
    Respawn,
    Checkpoint,
    Pause,
    Debug,
    CameraReset,
    CameraMode,
    // menu navigation
    Confirm,
    Back,
    NavUp,
    NavDown,
    NavLeft,
    NavRight,
    TabLeft,
    TabRight,
    MenuExtra,  // X: secondary menu action (save a setup, ...)
    TrickMod,   // scooter trick modifier (Scooter Flow layout: RT + right stick)
    Count
};

enum class Axis : int { MoveX = 0, MoveY, LookX, LookY, Brake, Grab, Trick, Count };

// control layouts: Scooter Flow style twin stick (right stick pops and rotates, RT + right stick = scooter
// tricks, LT + right stick = grabs) or the classic button layout (A pops, right stick flicks tricks)
enum class ControlScheme : int { Flow = 0, Classic = 1 };

// face button glyph family of the active controller
enum class PadStyle : int { Xbox = 0, PlayStation, Nintendo };

enum class InputDevice { Keyboard, Gamepad };

// 8-way stick direction
enum class StickDir : int { None = -1, Up = 0, UpRight, Right, DownRight, Down, DownLeft, Left, UpLeft };
const char* stickDirName(StickDir d);
StickDir stickDirFromName(const std::string& s);

struct FlickEvent {
    StickDir dir = StickDir::None;
    double time = 0.0;
    bool modifierGrab = false;   // grab modifier held
    bool modifierTrick = false;  // trick modifier held (Scooter Flow layout)
    bool modifierAlt = false;    // a bumper held: second trick / grab layer
    bool consumed = false;
};

struct Binding {
    std::vector<SDL_GamepadButton> buttons;
    std::vector<SDL_GamepadAxis> axisButtons;  // triggers used as buttons
    std::vector<SDL_Scancode> keys;
    std::vector<int> mouseButtons;
};

struct Deadzones {
    float leftStick = 0.15f;
    float rightStick = 0.2f;
    float trigger = 0.1f;
};

class Input {
public:
    void init();
    void shutdown();
    void loadBindings(const Json& defaults, const Json* userOverrides);
    Json saveBindings() const;

    void beginFrame();                 // call before polling SDL events
    void processEvent(const SDL_Event& e);
    void update(double time, float dt);  // after events

    bool down(Action a) const { return state_[int(a)].down; }
    bool pressed(Action a) const { return state_[int(a)].pressed; }    // this frame
    bool released(Action a) const { return state_[int(a)].released; }  // this frame
    float heldTime(Action a) const { return state_[int(a)].held; }
    // edge latches for fixed-step consumers
    bool consumePressed(Action a);
    bool consumeReleased(Action a);
    void clearLatches();

    float axis(Axis a) const { return axes_[int(a)]; }
    Vec2 moveStick() const { return {axes_[int(Axis::MoveX)], axes_[int(Axis::MoveY)]}; }
    Vec2 lookStick() const { return {axes_[int(Axis::LookX)], axes_[int(Axis::LookY)]}; }
    Vec2 mouseDelta() const { return mouseDelta_; }
    float mouseWheel() const { return mouseWheel_; }
    Vec2 mousePosition() const { return mousePos_; }
    bool mouseDown(int button) const;
    bool mouseClicked(int button) const;

    // right stick trick flicks (buffered)
    std::deque<FlickEvent>& flicks() { return flicks_; }
    StickDir currentRightDir() const;
    bool rightStickReturned() const { return lookMag_ < 0.35f; }

    void rumble(float low, float high, int ms);
    bool vibrationEnabled = true;
    Deadzones deadzones;
    float stickSensitivity = 1.0f;
    float cameraSensitivity = 1.0f;
    bool invertCameraY = false;

    bool gamepadConnected() const { return !pads_.empty(); }
    std::string gamepadName() const;
    PadStyle padStyle() const;
    SDL_Gamepad* activePad() const { return activePad_; }
    bool usingGamepad() const { return previewStyle_ >= 0 || (lastDevice_ == InputDevice::Gamepad && activePad_ != nullptr); }
    // screenshots / docs: show the glyphs of a controller family without one connected (-1 = off)
    void previewPadStyle(int style) { previewStyle_ = style; }
    bool previewingPad() const { return previewStyle_ >= 0; }
    InputDevice lastDevice() const { return lastDevice_; }
    ControlScheme scheme() const { return scheme_; }
    // switches the layout and loads its bindings (config defaults + the user's overrides for that layout)
    void setScheme(ControlScheme s);
    std::string userBindingsFile() const;
    bool textInputActive = false;  // console / editor text entry swallows keys

    // rebinding
    const Binding& binding(Action a) const { return bindings_[int(a)]; }
    void setGamepadBinding(Action a, SDL_GamepadButton b);
    void setKeyBinding(Action a, SDL_Scancode k);
    bool captureNextGamepadButton(SDL_GamepadButton& out);  // used by the rebind UI
    bool captureNextTrigger(SDL_GamepadAxis& out);
    void setGamepadTrigger(Action a, SDL_GamepadAxis t);
    bool captureNextKey(SDL_Scancode& out);
    void startCapture() {
        captureButton_ = SDL_GAMEPAD_BUTTON_INVALID;
        captureKey_ = SDL_SCANCODE_UNKNOWN;
        captureAxis_ = SDL_GAMEPAD_AXIS_INVALID;
        capturing_ = true;
    }
    void stopCapture() { capturing_ = false; }

    static const char* actionName(Action a);
    static Action actionFromName(const std::string& s);

private:
    struct ActionState {
        bool down = false, pressed = false, released = false;
        bool latchPressed = false, latchReleased = false;
        float held = 0.0f;
    };
    void openGamepad(SDL_JoystickID id);
    void closeGamepad(SDL_JoystickID id);
    float stickAxis(SDL_GamepadAxis a) const;
    float trigger(SDL_GamepadAxis a) const;
    float analog(Action a) const;  // 0..1: triggers bound to the action are analog
    Vec2 radialDeadzone(Vec2 v, float dz) const;
    bool rawDown(Action a) const;

    std::array<ActionState, size_t(Action::Count)> state_{};
    std::array<float, size_t(Axis::Count)> axes_{};
    std::array<Binding, size_t(Action::Count)> bindings_{};
    std::vector<SDL_Gamepad*> pads_;  // every connected controller drives the game
    SDL_Gamepad* activePad_ = nullptr;  // the one used last: sticks + glyphs
    ControlScheme scheme_ = ControlScheme::Flow;
    int previewStyle_ = -1;
    bool keys_[SDL_SCANCODE_COUNT] = {};
    uint32_t mouseButtons_ = 0, mouseClicks_ = 0;
    Vec2 mouseDelta_, mousePos_;
    float mouseWheel_ = 0.0f;
    InputDevice lastDevice_ = InputDevice::Keyboard;
    std::deque<FlickEvent> flicks_;
    float lookMag_ = 0.0f;
    float lookPeakTime_ = 0.0f;
    bool flickArmed_ = true;
    double time_ = 0.0;
    bool capturing_ = false;
    SDL_GamepadButton captureButton_ = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_Scancode captureKey_ = SDL_SCANCODE_UNKNOWN;
    SDL_GamepadAxis captureAxis_ = SDL_GAMEPAD_AXIS_INVALID;
};

Input& input();

}  // namespace sw
