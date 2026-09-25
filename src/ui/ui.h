// scoot would - game UI: immediate mode drawing + widgets on a virtual 1080p canvas
//
// All coordinates are in "canvas units": the canvas is 1080 units high and 1080 * aspect wide, and
// is scaled to the real output resolution. Widgets are focusable in submission order and are
// driven by gamepad / keyboard navigation (up/down moves focus, left/right changes values,
// confirm activates) or by the mouse (hover focuses, click activates).
#pragma once

#include "core/math.h"
#include "render/ui_draw.h"
#include "ui/font.h"

#include <string>
#include <vector>

namespace sw::ui {

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    Rect() = default;
    Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
    bool contains(Vec2 p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
    Rect shrink(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
};

enum class Align { Left, Center, Right };

enum class FontStyle { Regular = 0, SemiBold, Bold, Display, Count };

struct Theme {
    Vec4 text{0.96f, 0.96f, 0.95f, 1.0f};
    Vec4 textDim{0.72f, 0.74f, 0.77f, 1.0f};
    Vec4 panel{0.06f, 0.07f, 0.09f, 0.78f};
    Vec4 panelLight{0.13f, 0.14f, 0.17f, 0.85f};
    Vec4 accent{1.0f, 0.78f, 0.1f, 1.0f};   // signature yellow
    Vec4 accentText{0.07f, 0.07f, 0.08f, 1.0f};
    Vec4 good{0.35f, 0.9f, 0.45f, 1.0f};
    Vec4 bad{1.0f, 0.32f, 0.28f, 1.0f};
    Vec4 info{0.35f, 0.75f, 1.0f, 1.0f};
};

struct NavInput {
    bool up = false, down = false, left = false, right = false;  // edge + repeat
    bool confirm = false, back = false, tabLeft = false, tabRight = false, extra = false;
    Vec2 mouse;          // pixels
    bool mouseMoved = false, click = false, mouseDown = false;
    float wheel = 0.0f;
};

class Context {
public:
    void init();
    void begin(UIDrawList* dl, int pixelW, int pixelH, float dt, const NavInput& nav);
    void end();

    float width() const { return canvasW_; }   // canvas units
    float height() const { return 1080.0f; }
    float scale() const { return scale_; }
    float time() const { return time_; }
    const Theme& theme() const { return theme_; }
    const NavInput& nav() const { return nav_; }
    Vec2 mouse() const { return mouseCanvas_; }

    // --- drawing -----------------------------------------------------------------------
    void rect(const Rect& r, const Vec4& color, float radius = 0.0f);
    void rectGradient(const Rect& r, const Vec4& top, const Vec4& bottom, float radius = 0.0f);
    void rectOutline(const Rect& r, const Vec4& color, float thickness, float radius = 0.0f);
    void shadow(const Rect& r, float radius, float spread, float alpha);
    void line(Vec2 a, Vec2 b, float thickness, const Vec4& color);
    void polygon(const std::vector<Vec2>& convexPts, const Vec4& color);
    void circle(Vec2 c, float r, const Vec4& color, int segments = 32);
    void arc(Vec2 c, float r, float thickness, float a0, float a1, const Vec4& color);
    void image(Texture* tex, const Rect& r, const Vec4& tint = Vec4(1, 1, 1, 1));
    // frosted glass: the blurred scene behind r, mixed towards tint.rgb by tintAmount (tint.a = opacity)
    void frosted(const Rect& r, float radius, const Vec4& tint, float tintAmount);
    // returns the advance width; `size` = pixel height of the em at 1080p
    float text(const std::string& s, Vec2 pos, float size, const Vec4& color, FontStyle style = FontStyle::SemiBold, Align align = Align::Left,
               float shadowAlpha = 0.0f);
    void textBox(const std::string& s, const Rect& r, float size, const Vec4& color, FontStyle style, Align h, float shadowAlpha = 0.0f);
    // word wrapped paragraph; returns the height used
    float paragraph(const std::string& s, const Rect& r, float size, const Vec4& color, FontStyle style = FontStyle::Regular, float lineSpacing = 1.3f);
    float measure(const std::string& s, float size, FontStyle style = FontStyle::SemiBold);
    void pushClip(const Rect& r);
    void popClip();

    // --- widgets -------------------------------------------------------------------------
    // focus: every widget gets an index in submission order
    void resetFocus(int index = 0);
    int focusIndex() const { return focus_; }
    void setFocus(int index) { focus_ = index; }
    bool button(const std::string& label, const Rect& r, bool primary = false, bool enabled = true);
    // value selector: shows "< value >", returns -1 / +1 when changed
    int choice(const std::string& label, const std::string& value, const Rect& r);
    bool slider(const std::string& label, float& v, float mn, float mx, float step, const Rect& r, const char* fmt = "%.0f%%",
                float displayScale = 100.0f);
    bool toggle(const std::string& label, bool& v, const Rect& r);
    // big menu entry (main menu style); returns true when activated
    bool menuItem(const std::string& label, const Rect& r, const std::string& hint = "");
    // selectable card (map select / challenge list)
    bool card(const std::string& title, const std::string& subtitle, const Rect& r, bool selected, const Vec4& accent);
    bool lastFocused() const { return lastFocused_; }  // the widget just submitted has focus
    int widgetCount() const { return prevCount_; }
    // sound hooks
    bool movedFocus() const { return movedFocus_; }
    bool activated() const { return activatedAny_; }

    FontPtr font(FontStyle s) const { return fonts_[int(s)]; }

private:
    struct Batch {
        bool image = false;
        Texture* tex = nullptr;
        float softness = 0.0f;
        bool backdrop = false;
    };
    void setBatch(Texture* tex, bool image, float softness, bool backdrop = false);
    uint32_t vtx(Vec2 canvasPos, Vec2 uv, uint32_t color);
    Vec2 px(Vec2 c) const { return c * scale_; }
    void fillConvexAA(const std::vector<Vec2>& pts, const Vec4& color, const Vec4* colors = nullptr);
    std::vector<Vec2> roundRectPath(const Rect& r, float radius) const;
    int nextWidget(const Rect& r, bool focusable = true);
    float& hoverAnim(int idx);

    UIDrawList* dl_ = nullptr;
    FontPtr fonts_[int(FontStyle::Count)];
    Theme theme_;
    NavInput nav_;
    float scale_ = 1.0f, canvasW_ = 1920.0f, dt_ = 0.0f, time_ = 0.0f;
    Vec2 pixelSize_{1920.0f, 1080.0f};
    Vec2 mouseCanvas_;
    int focus_ = 0, count_ = 0, prevCount_ = 0;
    bool lastFocused_ = false, movedFocus_ = false, activatedAny_ = false;
    bool mouseMode_ = false;
    std::vector<float> anims_;
    std::vector<Rect> clips_;
    Batch cur_;
};

Context& context();

}  // namespace sw::ui
