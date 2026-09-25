// scoot would - input glyphs: the button, trigger, stick or key bound to an action, drawn for the device in
// use (Xbox / PlayStation / Nintendo face buttons, keyboard key caps). Used by menus, hint bars and the HUD.
#pragma once

#include "input/input.h"
#include "ui/ui.h"

#include <string>
#include <vector>

namespace sw::glyphs {

// every function draws with the left edge at x, vertically centred on y, `h` canvas units tall, and returns the
// width used (0 when nothing is bound)
float action(ui::Context& ui, Action a, float x, float y, float h, bool gamepad);
float button(ui::Context& ui, SDL_GamepadButton b, float x, float y, float h);
float trigger(ui::Context& ui, SDL_GamepadAxis a, float x, float y, float h);
// stick with an optional direction arrow (StickDir::None = the stick itself)
float stick(ui::Context& ui, bool right, StickDir dir, float x, float y, float h, bool gamepad);
float key(ui::Context& ui, const std::string& name, float x, float y, float h);
float plus(ui::Context& ui, float x, float y, float h);
// short text name of a gamepad button for the active controller family ("A", "Cross", "L1", ...)
std::string buttonName(SDL_GamepadButton b);
std::string triggerName(SDL_GamepadAxis a);

// hint bar entries: an action, or a stick (with an optional direction), followed by a label
struct Hint {
    enum Kind { ActionHint, LeftStick, RightStick } kind = ActionHint;
    Action action = Action::Confirm;
    std::string label;
    StickDir dir = StickDir::None;
    static Hint act(Action a, std::string l) { return {ActionHint, a, std::move(l), StickDir::None}; }
    static Hint ls(std::string l, StickDir d = StickDir::None) { return {LeftStick, Action::Confirm, std::move(l), d}; }
    static Hint rs(std::string l, StickDir d = StickDir::None) { return {RightStick, Action::Confirm, std::move(l), d}; }
};
// one hint drawn left aligned at x (glyph, then label); returns the width used
float hint(ui::Context& ui, const Hint& h, float x, float y, float size = 34.0f);
// right aligned row of hints ending at `right`
void hintRow(ui::Context& ui, const std::vector<Hint>& hints, float right, float y, float h = 34.0f);

}  // namespace sw::glyphs
