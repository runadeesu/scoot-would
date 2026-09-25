#include "game/ui/glyphs.h"

#include <cmath>

namespace sw::glyphs {

using namespace ui;

namespace {

const Vec4 kCap(0.94f, 0.94f, 0.95f, 0.95f);     // keyboard key caps / shoulder buttons
const Vec4 kCapInk(0.07f, 0.07f, 0.08f, 1.0f);
const Vec4 kPadDark(0.2f, 0.21f, 0.24f, 0.97f);   // face buttons / sticks (readable on dark panels too)
const Vec4 kRing(1.0f, 1.0f, 1.0f, 0.85f);

PadStyle style() { return input().padStyle(); }

// glyph captions (button names, key caps) are never translated
struct Raw {
    Context& ui;
    bool prev;
    explicit Raw(Context& u) : ui(u), prev(u.translating()) { ui.setTranslate(false); }
    ~Raw() { ui.setTranslate(prev); }
};

void disc(Context& ui, Vec2 c, float r, float h) {
    ui.circle(c, r, kPadDark, 32);
    ui.arc(c, r - h * 0.035f, h * 0.07f, 0.0f, kTwoPi, Vec4(0.92f, 0.92f, 0.94f, 0.95f));
}

// arrow head pointing along (dx, dy) (screen space, y down)
void arrow(Context& ui, Vec2 c, Vec2 d, float s, const Vec4& col) {
    Vec2 n(-d.y, d.x);
    ui.polygon({c + d * s, c - d * (s * 0.6f) + n * (s * 0.75f), c - d * (s * 0.6f) - n * (s * 0.75f)}, col);
}

Vec2 dirVector(StickDir d) {
    const float r = 0.70710678f;
    switch (d) {
        case StickDir::Up: return {0, -1};
        case StickDir::UpRight: return {r, -r};
        case StickDir::Right: return {1, 0};
        case StickDir::DownRight: return {r, r};
        case StickDir::Down: return {0, 1};
        case StickDir::DownLeft: return {-r, r};
        case StickDir::Left: return {-1, 0};
        case StickDir::UpLeft: return {-r, -r};
        default: return {0, 0};
    }
}

float capText(Context& ui, const std::string& t, float x, float y, float h, const Vec4& fill, const Vec4& ink, float radius) {
    float size = h * 0.52f;
    float w = std::max(h, ui.measure(t, size, FontStyle::Bold) + h * 0.55f);
    Rect r(x, y - h * 0.5f, w, h);
    ui.rect(r, fill, radius);
    ui.textBox(t, r, size, ink, FontStyle::Bold, Align::Center);
    return w;
}

std::string shortKey(const std::string& n) {
    if (n == "Left Shift" || n == "Right Shift") return "Shift";
    if (n == "Left Ctrl" || n == "Right Ctrl") return "Ctrl";
    if (n == "Left Alt" || n == "Right Alt") return "Alt";
    if (n == "Return") return "Enter";
    if (n == "Escape") return "Esc";
    if (n == "Backspace") return "BkSp";
    return n;
}

}  // namespace

std::string buttonName(SDL_GamepadButton b) {
    PadStyle st = style();
    SDL_Gamepad* pad = input().activePad();
    switch (b) {
        case SDL_GAMEPAD_BUTTON_SOUTH:
        case SDL_GAMEPAD_BUTTON_EAST:
        case SDL_GAMEPAD_BUTTON_WEST:
        case SDL_GAMEPAD_BUTTON_NORTH: {
            SDL_GamepadButtonLabel l = pad && st == input().padStyle() && !input().previewingPad() ? SDL_GetGamepadButtonLabel(pad, b)
                                                                                                    : SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
            switch (l) {
                case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
                case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
                case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
                case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
                case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "Cross";
                case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "Circle";
                case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "Square";
                case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "Triangle";
                default: break;
            }
            // no label reported (preview, unknown pad): the family's layout by position
            if (st == PadStyle::PlayStation)
                return b == SDL_GAMEPAD_BUTTON_SOUTH ? "Cross" : b == SDL_GAMEPAD_BUTTON_EAST ? "Circle" : b == SDL_GAMEPAD_BUTTON_WEST ? "Square" : "Triangle";
            if (st == PadStyle::Nintendo)
                return b == SDL_GAMEPAD_BUTTON_SOUTH ? "B" : b == SDL_GAMEPAD_BUTTON_EAST ? "A" : b == SDL_GAMEPAD_BUTTON_WEST ? "Y" : "X";
            return b == SDL_GAMEPAD_BUTTON_SOUTH ? "A" : b == SDL_GAMEPAD_BUTTON_EAST ? "B" : b == SDL_GAMEPAD_BUTTON_WEST ? "X" : "Y";
        }
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return st == PadStyle::PlayStation ? "L1" : st == PadStyle::Nintendo ? "L" : "LB";
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return st == PadStyle::PlayStation ? "R1" : st == PadStyle::Nintendo ? "R" : "RB";
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: return st == PadStyle::PlayStation ? "L3" : "LS";
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return st == PadStyle::PlayStation ? "R3" : "RS";
        case SDL_GAMEPAD_BUTTON_START: return st == PadStyle::PlayStation ? "OPTIONS" : st == PadStyle::Nintendo ? "+" : "MENU";
        case SDL_GAMEPAD_BUTTON_BACK: return st == PadStyle::PlayStation ? "CREATE" : st == PadStyle::Nintendo ? "-" : "VIEW";
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-Up";
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-Down";
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-Left";
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-Right";
        default: {
            const char* s = SDL_GetGamepadStringForButton(b);
            return s ? s : "?";
        }
    }
}

std::string triggerName(SDL_GamepadAxis a) {
    PadStyle st = style();
    bool left = a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
    if (st == PadStyle::PlayStation) return left ? "L2" : "R2";
    if (st == PadStyle::Nintendo) return left ? "ZL" : "ZR";
    return left ? "LT" : "RT";
}

float button(Context& ui, SDL_GamepadButton b, float x, float y, float h) {
    Raw raw(ui);
    std::string name = buttonName(b);
    float r = h * 0.5f;
    Vec2 c(x + r, y);
    if (name == "Cross" || name == "Circle" || name == "Square" || name == "Triangle") {
        disc(ui, c, r, h);
        float s = r * 0.42f;
        if (name == "Cross") {
            Vec4 col(0.55f, 0.7f, 1.0f, 1.0f);
            ui.line(c + Vec2(-s, -s), c + Vec2(s, s), h * 0.08f, col);
            ui.line(c + Vec2(-s, s), c + Vec2(s, -s), h * 0.08f, col);
        } else if (name == "Circle") {
            ui.arc(c, s * 1.05f, h * 0.075f, 0.0f, kTwoPi, Vec4(1.0f, 0.45f, 0.45f, 1.0f));
        } else if (name == "Square") {
            ui.rectOutline(Rect(c.x - s, c.y - s, 2 * s, 2 * s), Vec4(0.95f, 0.55f, 0.85f, 1.0f), h * 0.075f, 1.0f);
        } else {
            Vec4 col(0.35f, 0.9f, 0.7f, 1.0f);
            Vec2 p0 = c + Vec2(0, -s * 1.05f), p1 = c + Vec2(s * 1.05f, s * 0.75f), p2 = c + Vec2(-s * 1.05f, s * 0.75f);
            ui.line(p0, p1, h * 0.075f, col);
            ui.line(p1, p2, h * 0.075f, col);
            ui.line(p2, p0, h * 0.075f, col);
        }
        return h;
    }
    if (name == "A" || name == "B" || name == "X" || name == "Y") {
        Vec4 col(1, 1, 1, 1);
        if (style() == PadStyle::Xbox) {
            col = name == "A" ? Vec4(0.45f, 0.85f, 0.3f, 1) : name == "B" ? Vec4(1.0f, 0.35f, 0.3f, 1)
                  : name == "X" ? Vec4(0.35f, 0.6f, 1.0f, 1) : Vec4(1.0f, 0.82f, 0.2f, 1);
        }
        disc(ui, c, r, h);
        ui.textBox(name, Rect(x, y - r, h, h), h * 0.6f, col, FontStyle::Bold, Align::Center);
        return h;
    }
    if (b >= SDL_GAMEPAD_BUTTON_DPAD_UP && b <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
        // cross with the pressed direction highlighted
        float a = h * 0.3f, l = h * 0.5f;
        ui.rect(Rect(c.x - a * 0.5f, c.y - l, a, 2 * l), kPadDark, 2);
        ui.rect(Rect(c.x - l, c.y - a * 0.5f, 2 * l, a), kPadDark, 2);
        Vec2 d = b == SDL_GAMEPAD_BUTTON_DPAD_UP ? Vec2(0, -1) : b == SDL_GAMEPAD_BUTTON_DPAD_DOWN ? Vec2(0, 1)
                 : b == SDL_GAMEPAD_BUTTON_DPAD_LEFT ? Vec2(-1, 0) : Vec2(1, 0);
        arrow(ui, c + d * (l * 0.55f), d, h * 0.16f, kRing);
        return h;
    }
    if (b == SDL_GAMEPAD_BUTTON_LEFT_STICK || b == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
        disc(ui, c, r, h);
        ui.textBox(name, Rect(x, y - r, h, h), h * 0.36f, Vec4(1, 1, 1, 1), FontStyle::Bold, Align::Center);
        return h;
    }
    return capText(ui, name, x, y, h, kCap, kCapInk, h * 0.3f);
}

float trigger(Context& ui, SDL_GamepadAxis a, float x, float y, float h) {
    Raw raw(ui);
    return capText(ui, triggerName(a), x, y, h, kCap, kCapInk, h * 0.2f);
}

float key(Context& ui, const std::string& name, float x, float y, float h) {
    Raw raw(ui);
    if (name == "Up" || name == "Down" || name == "Left" || name == "Right") {
        Rect r(x, y - h * 0.5f, h, h);
        ui.rect(r, kCap, h * 0.18f);
        Vec2 d = name == "Up" ? Vec2(0, -1) : name == "Down" ? Vec2(0, 1) : name == "Left" ? Vec2(-1, 0) : Vec2(1, 0);
        arrow(ui, r.center(), d, h * 0.24f, kCapInk);
        return h;
    }
    return capText(ui, shortKey(name), x, y, h, kCap, kCapInk, h * 0.18f);
}

float plus(Context& ui, float x, float y, float h) {
    Raw raw(ui);
    float w = h * 0.55f;
    ui.textBox("+", Rect(x, y - h * 0.5f, w, h), h * 0.6f, Vec4(1, 1, 1, 0.9f), FontStyle::Bold, Align::Center);
    return w;
}

float stick(Context& ui, bool right, StickDir dir, float x, float y, float h, bool gamepad) {
    Raw raw(ui);
    if (!gamepad) {
        // keyboard: WASD / arrow keys (a single key when a direction is given)
        if (dir != StickDir::None) {
            Vec2 v = dirVector(dir);
            std::string k;
            if (right) k = std::fabs(v.x) > std::fabs(v.y) ? (v.x > 0 ? "Right" : "Left") : (v.y > 0 ? "Down" : "Up");
            else k = std::fabs(v.x) > std::fabs(v.y) ? (v.x > 0 ? "D" : "A") : (v.y > 0 ? "S" : "W");
            return key(ui, k, x, y, h);
        }
        if (!right) return key(ui, "WASD", x, y, h);
        float w = 0.0f;
        for (const char* k : {"Left", "Up", "Down", "Right"}) w += key(ui, k, x + w, y, h * 0.8f) + 2.0f;
        return w - 2.0f;
    }
    float r = h * 0.5f;
    Vec2 c(x + r, y);
    disc(ui, c, r, h);
    const char* letter = right ? "R" : "L";
    Vec2 d = dirVector(dir);
    if (dir != StickDir::None) {
        // the stick cap pushed towards the direction, with an arrow
        Vec2 cap = c + d * (r * 0.3f);
        ui.circle(cap, r * 0.5f, Vec4(0.95f, 0.95f, 0.96f, 1), 24);
        ui.textBox(letter, Rect(cap.x - r, cap.y - r, h, h), h * 0.38f, kCapInk, FontStyle::Bold, Align::Center);
        arrow(ui, c + d * (r * 1.1f), d, h * 0.2f, ui.theme().accent);
    } else {
        ui.textBox(letter, Rect(x, y - r, h, h), h * 0.5f, Vec4(1, 1, 1, 1), FontStyle::Bold, Align::Center);
    }
    return h;
}

float action(Context& ui, Action a, float x, float y, float h, bool gamepad) {
    const Binding& b = input().binding(a);
    if (gamepad) {
        if (!b.buttons.empty()) return button(ui, b.buttons.front(), x, y, h);
        if (!b.axisButtons.empty()) return trigger(ui, b.axisButtons.front(), x, y, h);
        return 0.0f;
    }
    if (!b.keys.empty()) return key(ui, SDL_GetScancodeName(b.keys.front()), x, y, h);
    if (!b.mouseButtons.empty()) {
        int m = b.mouseButtons.front();
        return key(ui, m == SDL_BUTTON_LEFT ? "Mouse 1" : m == SDL_BUTTON_RIGHT ? "Mouse 2" : "Mouse 3", x, y, h);
    }
    return 0.0f;
}

float hint(Context& ui, const Hint& hnt, float x, float y, float h) {
    bool pad = input().usingGamepad();
    float x0 = x;
    if (hnt.kind == Hint::ActionHint) x += action(ui, hnt.action, x, y, h, pad);
    else x += stick(ui, hnt.kind == Hint::RightStick, hnt.dir, x, y, h, pad);
    x += 12.0f;
    const float labelSize = h * 0.7f;
    ui.text(hnt.label, Vec2(x, y - labelSize * 0.62f), labelSize, ui.theme().text, FontStyle::Bold, Align::Left, 0.5f);
    return x + ui.measure(hnt.label, labelSize, FontStyle::Bold) - x0;
}

void hintRow(Context& ui, const std::vector<Hint>& hints, float right, float y, float h) {
    bool pad = input().usingGamepad();
    float x = right;
    const float labelSize = h * 0.7f;
    for (auto it = hints.rbegin(); it != hints.rend(); ++it) {
        const Hint& hnt = *it;
        float lw = ui.measure(hnt.label, labelSize, FontStyle::Bold);
        x -= lw;
        ui.text(hnt.label, Vec2(x, y - labelSize * 0.62f), labelSize, ui.theme().text, FontStyle::Bold, Align::Left, 0.5f);
        x -= 12.0f;
        // measure the glyph by drawing it off screen first would double the geometry: estimate instead
        float gw;
        Raw raw(ui);
        if (hnt.kind == Hint::ActionHint) {
            const Binding& b = input().binding(hnt.action);
            if (pad) {
                std::string n = !b.buttons.empty() ? buttonName(b.buttons.front()) : !b.axisButtons.empty() ? triggerName(b.axisButtons.front()) : "";
                bool round = n == "A" || n == "B" || n == "X" || n == "Y" || n == "Cross" || n == "Circle" || n == "Square" ||
                             n == "Triangle" || n.rfind("D-", 0) == 0 || n == "LS" || n == "RS" || n == "L3" || n == "R3";
                gw = n.empty() ? 0.0f : round ? h : std::max(h, ui.measure(n, h * 0.52f, FontStyle::Bold) + h * 0.55f);
            } else {
                std::string n = !b.keys.empty() ? shortKey(SDL_GetScancodeName(b.keys.front())) : "Mouse 1";
                bool arrowKey = n == "Up" || n == "Down" || n == "Left" || n == "Right";
                gw = arrowKey ? h : std::max(h, ui.measure(n, h * 0.52f, FontStyle::Bold) + h * 0.55f);
            }
            x -= gw;
            action(ui, hnt.action, x, y, h, pad);
        } else {
            bool right = hnt.kind == Hint::RightStick;
            if (pad || hnt.dir != StickDir::None) gw = h;
            else gw = right ? (h * 0.8f + 2.0f) * 4.0f - 2.0f : std::max(h, ui.measure("WASD", h * 0.52f, FontStyle::Bold) + h * 0.55f);
            x -= gw;
            stick(ui, right, hnt.dir, x, y, h, pad);
        }
        x -= 34.0f;
    }
}

}  // namespace sw::glyphs
