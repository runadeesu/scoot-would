#include "input/input.h"
#include "core/filesystem.h"
#include "core/log.h"

#include <cstring>

namespace sw {

Input& input() {
    static Input in;
    return in;
}

static const char* kActionNames[] = {"Push", "Brake", "Jump", "SpinLeft", "SpinRight", "Grab", "Revert", "Respawn", "Checkpoint",
                                     "Pause", "Debug", "CameraReset", "CameraMode", "Confirm", "Back", "NavUp", "NavDown", "NavLeft",
                                     "NavRight", "TabLeft", "TabRight", "MenuExtra", "TrickMod"};
static_assert(sizeof(kActionNames) / sizeof(kActionNames[0]) == size_t(Action::Count), "action names");

const char* Input::actionName(Action a) { return kActionNames[int(a)]; }
Action Input::actionFromName(const std::string& s) {
    for (int i = 0; i < int(Action::Count); ++i)
        if (s == kActionNames[i]) return Action(i);
    return Action::Count;
}

const char* stickDirName(StickDir d) {
    static const char* n[] = {"up", "up_right", "right", "down_right", "down", "down_left", "left", "up_left"};
    return d == StickDir::None ? "none" : n[int(d)];
}

StickDir stickDirFromName(const std::string& s) {
    for (int i = 0; i < 8; ++i)
        if (s == stickDirName(StickDir(i))) return StickDir(i);
    return StickDir::None;
}

static StickDir quantize(Vec2 v) {
    if (v.lengthSq() < 0.04f) return StickDir::None;
    float a = std::atan2(v.x, v.y);  // 0 = up, clockwise
    int idx = int(std::lround(a / (kPi / 4.0f)));
    idx = ((idx % 8) + 8) % 8;
    return StickDir(idx);
}

static SDL_GamepadButton buttonFromName(const std::string& s) {
    SDL_GamepadButton b = SDL_GetGamepadButtonFromString(s.c_str());
    if (b != SDL_GAMEPAD_BUTTON_INVALID) return b;
    // friendly Xbox names
    if (s == "A") return SDL_GAMEPAD_BUTTON_SOUTH;
    if (s == "B") return SDL_GAMEPAD_BUTTON_EAST;
    if (s == "X") return SDL_GAMEPAD_BUTTON_WEST;
    if (s == "Y") return SDL_GAMEPAD_BUTTON_NORTH;
    if (s == "LB") return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    if (s == "RB") return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    if (s == "Start") return SDL_GAMEPAD_BUTTON_START;
    if (s == "Back") return SDL_GAMEPAD_BUTTON_BACK;
    if (s == "LS") return SDL_GAMEPAD_BUTTON_LEFT_STICK;
    if (s == "RS") return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    if (s == "DpadUp") return SDL_GAMEPAD_BUTTON_DPAD_UP;
    if (s == "DpadDown") return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    if (s == "DpadLeft") return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    if (s == "DpadRight") return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    return SDL_GAMEPAD_BUTTON_INVALID;
}

void Input::init() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids) {
        for (int i = 0; i < count; ++i) openGamepad(ids[i]);
        SDL_free(ids);
    }
}

void Input::shutdown() {
    for (SDL_Gamepad* p : pads_) SDL_CloseGamepad(p);
    pads_.clear();
    activePad_ = nullptr;
}

void Input::openGamepad(SDL_JoystickID id) {
    for (SDL_Gamepad* p : pads_)
        if (SDL_GetGamepadID(p) == id) return;
    SDL_Gamepad* pad = SDL_OpenGamepad(id);
    if (!pad) return;
    pads_.push_back(pad);
    if (!activePad_) activePad_ = pad;
    LOG_INFO("input: gamepad connected: %s", SDL_GetGamepadName(pad));
    lastDevice_ = InputDevice::Gamepad;
}

void Input::closeGamepad(SDL_JoystickID id) {
    for (size_t i = 0; i < pads_.size(); ++i) {
        if (SDL_GetGamepadID(pads_[i]) != id) continue;
        LOG_INFO("input: gamepad disconnected: %s", SDL_GetGamepadName(pads_[i]));
        if (activePad_ == pads_[i]) activePad_ = nullptr;
        SDL_CloseGamepad(pads_[i]);
        pads_.erase(pads_.begin() + long(i));
        break;
    }
    if (!activePad_ && !pads_.empty()) activePad_ = pads_.front();
    if (pads_.empty()) lastDevice_ = InputDevice::Keyboard;
}

std::string Input::gamepadName() const { return activePad_ ? SDL_GetGamepadName(activePad_) : ""; }

PadStyle Input::padStyle() const {
    if (previewStyle_ >= 0) return PadStyle(previewStyle_);
    if (!activePad_) return PadStyle::Xbox;
    switch (SDL_GetGamepadType(activePad_)) {
        case SDL_GAMEPAD_TYPE_PS3:
        case SDL_GAMEPAD_TYPE_PS4:
        case SDL_GAMEPAD_TYPE_PS5: return PadStyle::PlayStation;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return PadStyle::Nintendo;
        default: return PadStyle::Xbox;
    }
}

std::string Input::userBindingsFile() const { return scheme_ == ControlScheme::Flow ? "input_flow.json" : "input_classic.json"; }

void Input::setScheme(ControlScheme s) {
    scheme_ = s;
    auto defaults = loadJsonFile(fs::resolve(s == ControlScheme::Flow ? "config/input.json" : "config/input_classic.json"));
    auto user = loadJsonFile(fs::userPath(userBindingsFile()));
    for (auto& b : bindings_) b = Binding{};
    if (defaults) loadBindings(*defaults, user ? &*user : nullptr);
    clearLatches();
}

void Input::loadBindings(const Json& defaults, const Json* user) {
    auto apply = [&](const Json& j) {
        if (j.contains("deadzones")) {
            const Json& d = j["deadzones"];
            deadzones.leftStick = jget<float>(d, "leftStick", deadzones.leftStick);
            deadzones.rightStick = jget<float>(d, "rightStick", deadzones.rightStick);
            deadzones.trigger = jget<float>(d, "trigger", deadzones.trigger);
        }
        if (!j.contains("actions")) return;
        for (auto& [name, b] : j["actions"].items()) {
            Action a = actionFromName(name);
            if (a == Action::Count) continue;
            Binding bind;
            for (auto& g : b.value("gamepad", Json::array())) {
                std::string s = g.get<std::string>();
                if (s == "LT") bind.axisButtons.push_back(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
                else if (s == "RT") bind.axisButtons.push_back(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
                else {
                    SDL_GamepadButton gb = buttonFromName(s);
                    if (gb != SDL_GAMEPAD_BUTTON_INVALID) bind.buttons.push_back(gb);
                }
            }
            for (auto& k : b.value("keyboard", Json::array())) {
                std::string s = k.get<std::string>();
                if (s == "Mouse1") bind.mouseButtons.push_back(SDL_BUTTON_LEFT);
                else if (s == "Mouse2") bind.mouseButtons.push_back(SDL_BUTTON_RIGHT);
                else if (s == "Mouse3") bind.mouseButtons.push_back(SDL_BUTTON_MIDDLE);
                else {
                    SDL_Scancode sc = SDL_GetScancodeFromName(s.c_str());
                    if (sc != SDL_SCANCODE_UNKNOWN) bind.keys.push_back(sc);
                    else LOG_WARN("input: unknown key '%s' for %s", s.c_str(), name.c_str());
                }
            }
            bindings_[int(a)] = bind;
        }
    };
    apply(defaults);
    if (user) apply(*user);
}

Json Input::saveBindings() const {
    Json j;
    j["deadzones"] = {{"leftStick", deadzones.leftStick}, {"rightStick", deadzones.rightStick}, {"trigger", deadzones.trigger}};
    Json actions = Json::object();
    for (int i = 0; i < int(Action::Count); ++i) {
        const Binding& b = bindings_[i];
        Json g = Json::array(), k = Json::array();
        for (auto gb : b.buttons) g.push_back(SDL_GetGamepadStringForButton(gb));
        for (auto ax : b.axisButtons) g.push_back(ax == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? "LT" : "RT");
        for (auto sc : b.keys) k.push_back(SDL_GetScancodeName(sc));
        for (int mb : b.mouseButtons) k.push_back(mb == SDL_BUTTON_LEFT ? "Mouse1" : mb == SDL_BUTTON_RIGHT ? "Mouse2" : "Mouse3");
        actions[kActionNames[i]] = {{"gamepad", g}, {"keyboard", k}};
    }
    j["actions"] = actions;
    return j;
}

void Input::setGamepadBinding(Action a, SDL_GamepadButton b) {
    bindings_[int(a)].buttons = {b};
    bindings_[int(a)].axisButtons.clear();
}
void Input::setKeyBinding(Action a, SDL_Scancode k) { bindings_[int(a)].keys = {k}; }
void Input::setGamepadTrigger(Action a, SDL_GamepadAxis t) {
    bindings_[int(a)].axisButtons = {t};
    bindings_[int(a)].buttons.clear();
}

bool Input::captureNextTrigger(SDL_GamepadAxis& out) {
    if (captureAxis_ == SDL_GAMEPAD_AXIS_INVALID) return false;
    out = captureAxis_;
    capturing_ = false;
    return true;
}

bool Input::captureNextGamepadButton(SDL_GamepadButton& out) {
    if (captureButton_ == SDL_GAMEPAD_BUTTON_INVALID) return false;
    out = captureButton_;
    capturing_ = false;
    return true;
}
bool Input::captureNextKey(SDL_Scancode& out) {
    if (captureKey_ == SDL_SCANCODE_UNKNOWN) return false;
    out = captureKey_;
    capturing_ = false;
    return true;
}

void Input::beginFrame() {
    mouseDelta_ = Vec2();
    mouseWheel_ = 0.0f;
    mouseClicks_ = 0;
}

void Input::processEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_GAMEPAD_ADDED: openGamepad(e.gdevice.which); break;
        case SDL_EVENT_GAMEPAD_REMOVED: closeGamepad(e.gdevice.which); break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            lastDevice_ = InputDevice::Gamepad;
            if (SDL_Gamepad* p = SDL_GetGamepadFromID(e.gbutton.which)) activePad_ = p;
            if (capturing_) captureButton_ = SDL_GamepadButton(e.gbutton.button);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (std::abs(e.gaxis.value) > 12000) {
                lastDevice_ = InputDevice::Gamepad;
                if (SDL_Gamepad* p = SDL_GetGamepadFromID(e.gaxis.which)) activePad_ = p;
            }
            if (capturing_ && e.gaxis.value > 22000 &&
                (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER))
                captureAxis_ = SDL_GamepadAxis(e.gaxis.axis);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (e.key.scancode < SDL_SCANCODE_COUNT && !textInputActive) keys_[e.key.scancode] = true;
            if (capturing_ && !e.key.repeat) captureKey_ = e.key.scancode;
            lastDevice_ = InputDevice::Keyboard;
            // arrow keys emit trick flicks (keyboard equivalent of the right stick)
            if (!e.key.repeat && !textInputActive &&
                (e.key.scancode == SDL_SCANCODE_UP || e.key.scancode == SDL_SCANCODE_DOWN || e.key.scancode == SDL_SCANCODE_LEFT ||
                 e.key.scancode == SDL_SCANCODE_RIGHT)) {
                Vec2 v((keys_[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f) - (keys_[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f),
                       (keys_[SDL_SCANCODE_UP] ? 1.0f : 0.0f) - (keys_[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f));
                FlickEvent f;
                f.dir = quantize(v);
                f.time = time_;
                f.modifierGrab = down(Action::Grab);
                f.modifierTrick = down(Action::TrickMod);
                f.modifierAlt = down(Action::SpinLeft) || down(Action::SpinRight);
                if (f.dir != StickDir::None) flicks_.push_back(f);
            }
            break;
        case SDL_EVENT_KEY_UP:
            if (e.key.scancode < SDL_SCANCODE_COUNT) keys_[e.key.scancode] = false;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            mouseDelta_ += Vec2(e.motion.xrel, e.motion.yrel);
            mousePos_ = Vec2(e.motion.x, e.motion.y);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            mouseButtons_ |= 1u << e.button.button;
            mouseClicks_ |= 1u << e.button.button;
            lastDevice_ = InputDevice::Keyboard;
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP: mouseButtons_ &= ~(1u << e.button.button); break;
        case SDL_EVENT_MOUSE_WHEEL: mouseWheel_ += e.wheel.y; break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            std::memset(keys_, 0, sizeof(keys_));
            mouseButtons_ = 0;
            break;
        default: break;
    }
}

bool Input::mouseDown(int button) const { return (mouseButtons_ & (1u << button)) != 0; }
bool Input::mouseClicked(int button) const { return (mouseClicks_ & (1u << button)) != 0; }

float Input::stickAxis(SDL_GamepadAxis a) const {
    if (!activePad_) return 0.0f;
    return float(SDL_GetGamepadAxis(activePad_, a)) / 32767.0f;
}

float Input::trigger(SDL_GamepadAxis a) const {
    // triggers of every pad count (the strongest wins)
    float v = 0.0f;
    for (SDL_Gamepad* p : pads_) v = std::max(v, float(SDL_GetGamepadAxis(p, a)) / 32767.0f);
    return v <= deadzones.trigger ? 0.0f : std::min(1.0f, (v - deadzones.trigger) / (1.0f - deadzones.trigger));
}

float Input::analog(Action a) const {
    float v = down(a) ? 1.0f : 0.0f;
    for (SDL_GamepadAxis ax : bindings_[int(a)].axisButtons) v = std::max(v, trigger(ax));
    return v;
}

Vec2 Input::radialDeadzone(Vec2 v, float dz) const {
    float m = v.length();
    if (m <= dz) return Vec2();
    float scaled = std::min(1.0f, (m - dz) / (1.0f - dz));
    return v * (scaled / m);
}

bool Input::rawDown(Action a) const {
    const Binding& b = bindings_[int(a)];
    if (!textInputActive)
        for (SDL_Scancode k : b.keys)
            if (keys_[k]) return true;
    for (int mb : b.mouseButtons)
        if (mouseDown(mb)) return true;
    for (SDL_Gamepad* p : pads_) {
        for (SDL_GamepadButton gb : b.buttons)
            if (SDL_GetGamepadButton(p, gb)) return true;
        for (SDL_GamepadAxis ax : b.axisButtons)
            if (float(SDL_GetGamepadAxis(p, ax)) / 32767.0f > std::max(0.35f, deadzones.trigger)) return true;
    }
    return false;
}

void Input::update(double time, float dt) {
    time_ = time;
    for (int i = 0; i < int(Action::Count); ++i) {
        ActionState& s = state_[i];
        bool d = rawDown(Action(i));
        s.pressed = d && !s.down;
        s.released = !d && s.down;
        if (s.pressed) s.latchPressed = true;
        if (s.released) s.latchReleased = true;
        s.held = d ? s.held + dt : 0.0f;
        s.down = d;
    }
    // sticks
    Vec2 ls(stickAxis(SDL_GAMEPAD_AXIS_LEFTX), -stickAxis(SDL_GAMEPAD_AXIS_LEFTY));
    Vec2 rs(stickAxis(SDL_GAMEPAD_AXIS_RIGHTX), -stickAxis(SDL_GAMEPAD_AXIS_RIGHTY));
    ls = radialDeadzone(ls, deadzones.leftStick);
    rs = radialDeadzone(rs, deadzones.rightStick);
    if (!textInputActive) {
        auto key = [&](SDL_Scancode k) { return keys_[k] ? 1.0f : 0.0f; };
        Vec2 kl(key(SDL_SCANCODE_D) - key(SDL_SCANCODE_A), key(SDL_SCANCODE_W) - key(SDL_SCANCODE_S));
        if (kl.lengthSq() > 0) ls = kl.lengthSq() > 1.0f ? kl.normalized() : kl;
        Vec2 kr(key(SDL_SCANCODE_RIGHT) - key(SDL_SCANCODE_LEFT), key(SDL_SCANCODE_UP) - key(SDL_SCANCODE_DOWN));
        if (kr.lengthSq() > 0) rs = kr.lengthSq() > 1.0f ? kr.normalized() : kr;
    }
    ls *= stickSensitivity;
    axes_[int(Axis::MoveX)] = clampf(ls.x, -1, 1);
    axes_[int(Axis::MoveY)] = clampf(ls.y, -1, 1);
    axes_[int(Axis::LookX)] = rs.x;
    axes_[int(Axis::LookY)] = rs.y;
    // analog actions: a trigger bound to the action gives its travel, keys / buttons give 1
    axes_[int(Axis::Brake)] = analog(Action::Brake);
    axes_[int(Axis::Grab)] = analog(Action::Grab);
    axes_[int(Axis::Trick)] = analog(Action::TrickMod);

    // right stick flick detection (gamepad): fast move from centre to the edge. The Scooter Flow layout
    // only registers a trick when the stick is pushed all the way
    if (activePad_) {
        const float edge = scheme_ == ControlScheme::Flow ? 0.93f : 0.8f;
        Vec2 raw = radialDeadzone(Vec2(stickAxis(SDL_GAMEPAD_AXIS_RIGHTX), -stickAxis(SDL_GAMEPAD_AXIS_RIGHTY)), deadzones.rightStick);
        float m = raw.length();
        if (m < 0.35f) {
            flickArmed_ = true;
            lookPeakTime_ = 0.0f;
        } else if (flickArmed_) {
            lookPeakTime_ += dt;
            if (m > edge) {
                // accept both quick flicks and slower pushes (players do both); direction at the edge
                FlickEvent f;
                f.dir = quantize(raw);
                f.time = time;
                f.modifierGrab = axes_[int(Axis::Grab)] > 0.3f;
                f.modifierTrick = axes_[int(Axis::Trick)] > 0.3f;
                f.modifierAlt = down(Action::SpinLeft) || down(Action::SpinRight);
                flicks_.push_back(f);
                flickArmed_ = false;
            } else if (lookPeakTime_ > 0.35f) {
                flickArmed_ = false;  // slow drift is not a flick
            }
        }
        lookMag_ = m;
    }
    // expire old buffered flicks
    while (!flicks_.empty() && time - flicks_.front().time > 1.0) flicks_.pop_front();
}

StickDir Input::currentRightDir() const { return quantize(lookStick()); }

bool Input::consumePressed(Action a) {
    bool v = state_[int(a)].latchPressed;
    state_[int(a)].latchPressed = false;
    return v;
}

bool Input::consumeReleased(Action a) {
    bool v = state_[int(a)].latchReleased;
    state_[int(a)].latchReleased = false;
    return v;
}

void Input::clearLatches() {
    for (auto& s : state_) s.latchPressed = s.latchReleased = false;
    flicks_.clear();
}

void Input::rumble(float low, float high, int ms) {
    if (!activePad_ || !vibrationEnabled) return;
    SDL_RumbleGamepad(activePad_, uint16_t(clampf(low, 0, 1) * 65535.0f), uint16_t(clampf(high, 0, 1) * 65535.0f), uint32_t(ms));
}

}  // namespace sw
