#include "ui/ui.h"

#include "assets/asset_manager.h"
#include "core/log.h"

#include <cmath>
#include <cstdio>

namespace sw::ui {

namespace {
Context g_ctx;
}
Context& context() { return g_ctx; }

void Context::init() {
    fontAtlas().init();
    const char* files[int(FontStyle::Count)] = {"assets/fonts/Barlow-Regular.ttf", "assets/fonts/Barlow-SemiBold.ttf", "assets/fonts/Barlow-Bold.ttf",
                                                "assets/fonts/BarlowCondensed-ExtraBoldItalic.ttf"};
    for (int i = 0; i < int(FontStyle::Count); ++i) {
        fonts_[i] = assets().font(files[i], 0);
        if (!fonts_[i]) LOG_ERROR("ui: missing font %s", files[i]);
    }
    // warm the atlas with the printable ASCII range of every face
    for (auto& f : fonts_)
        if (f)
            for (uint32_t c = 32; c < 127; ++c) f->glyph(c);
}

void Context::begin(UIDrawList* dl, int pw, int ph, float dt, const NavInput& nav) {
    dl_ = dl;
    scale_ = float(std::max(ph, 1)) / 1080.0f;
    canvasW_ = float(pw) / scale_;
    pixelSize_ = Vec2(float(std::max(pw, 1)), float(std::max(ph, 1)));
    dt_ = dt;
    time_ += dt;
    nav_ = nav;
    mouseCanvas_ = nav.mouse / scale_;
    if (dl_) dl_->defaultAtlas = fontAtlas().texture();
    cur_ = Batch{};
    cur_.tex = reinterpret_cast<Texture*>(1);  // force a new command on first draw
    clips_.clear();
    movedFocus_ = activatedAny_ = false;
    if (nav.mouseMoved || nav.click) mouseMode_ = true;
    if (nav.up || nav.down || nav.left || nav.right || nav.confirm) mouseMode_ = false;
    if (prevCount_ > 0) {
        if (nav.up) {
            focus_ = (focus_ - 1 + prevCount_) % prevCount_;
            movedFocus_ = true;
        }
        if (nav.down) {
            focus_ = (focus_ + 1) % prevCount_;
            movedFocus_ = true;
        }
    }
    count_ = 0;
}

void Context::end() {
    prevCount_ = count_;
    if (prevCount_ > 0) focus_ = std::clamp(focus_, 0, prevCount_ - 1);
    fontAtlas().flush();
    dl_ = nullptr;
}

void Context::resetFocus(int index) {
    focus_ = index;
    anims_.assign(anims_.size(), 0.0f);
}

float& Context::hoverAnim(int idx) {
    if (size_t(idx) >= anims_.size()) anims_.resize(size_t(idx) + 16, 0.0f);
    return anims_[size_t(idx)];
}

// --- batching ----------------------------------------------------------------------------------

void Context::setBatch(Texture* tex, bool image, float softness, bool backdrop) {
    if (!dl_) return;
    Rect clip = clips_.empty() ? Rect() : clips_.back();
    int cx = int(clip.x * scale_), cy = int(clip.y * scale_), cw = int(clip.w * scale_), ch = int(clip.h * scale_);
    bool same = !dl_->commands.empty() && cur_.tex == tex && cur_.image == image && cur_.softness == softness && cur_.backdrop == backdrop;
    if (same) {
        const UIDrawCmd& last = dl_->commands.back();
        same = last.clipX == cx && last.clipY == cy && last.clipW == cw && last.clipH == ch;
    }
    if (same) return;
    UIDrawCmd c;
    c.texture = tex;
    c.rgbaImage = image;
    c.softness = softness;
    c.firstIndex = uint32_t(dl_->indices.size());
    c.clipX = cx;
    c.clipY = cy;
    c.clipW = cw;
    c.clipH = ch;
    c.backdrop = backdrop;
    dl_->commands.push_back(c);
    cur_ = {image, tex, softness, backdrop};
    if (backdrop) dl_->usesBackdrop = true;
}

uint32_t Context::vtx(Vec2 p, Vec2 uv, uint32_t color) {
    dl_->vertices.push_back({px(p), uv, color});
    return uint32_t(dl_->vertices.size() - 1);
}

static void addTri(UIDrawList* dl, uint32_t a, uint32_t b, uint32_t c) {
    dl->indices.push_back(a);
    dl->indices.push_back(b);
    dl->indices.push_back(c);
    dl->commands.back().indexCount += 3;
}

void Context::fillConvexAA(const std::vector<Vec2>& ptsCanvas, const Vec4& color, const Vec4* colors) {
    if (!dl_ || ptsCanvas.size() < 3) return;
    setBatch(nullptr, false, 0.0f);
    Vec2 wuv = fontAtlas().whiteUV();
    size_t n = ptsCanvas.size();
    // winding (screen space, y down)
    float area = 0;
    for (size_t i = 0; i < n; ++i) {
        const Vec2 &a = ptsCanvas[i], &b = ptsCanvas[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
    }
    float sgn = area >= 0 ? 1.0f : -1.0f;
    float aa = 0.6f / scale_;  // half a pixel in canvas units
    std::vector<Vec2> normals(n);
    for (size_t i = 0; i < n; ++i) {
        Vec2 prev = ptsCanvas[(i + n - 1) % n], cur = ptsCanvas[i], next = ptsCanvas[(i + 1) % n];
        Vec2 e0 = (cur - prev), e1 = (next - cur);
        Vec2 n0 = Vec2(e0.y, -e0.x) * sgn, n1 = Vec2(e1.y, -e1.x) * sgn;
        float l0 = n0.length(), l1 = n1.length();
        n0 = l0 > 1e-6f ? n0 / l0 : Vec2(0);
        n1 = l1 > 1e-6f ? n1 / l1 : Vec2(0);
        Vec2 nn = n0 + n1;
        float l = nn.length();
        nn = l > 1e-6f ? nn / l : n0;
        float d = std::max(0.3f, dot(nn, n0));
        normals[i] = nn / d;
    }
    uint32_t base = uint32_t(dl_->vertices.size());
    for (size_t i = 0; i < n; ++i) {
        Vec4 c = colors ? colors[i] : color;
        vtx(ptsCanvas[i] - normals[i] * aa, wuv, packColor(c));
        vtx(ptsCanvas[i] + normals[i] * aa, wuv, packColor(Vec4(c.x, c.y, c.z, 0.0f)));
    }
    for (size_t i = 1; i + 1 < n; ++i) addTri(dl_, base, base + uint32_t(i * 2), base + uint32_t((i + 1) * 2));
    for (size_t i = 0; i < n; ++i) {
        uint32_t i0 = base + uint32_t(i * 2), o0 = i0 + 1;
        uint32_t i1 = base + uint32_t(((i + 1) % n) * 2), o1 = i1 + 1;
        addTri(dl_, i0, o0, o1);
        addTri(dl_, i0, o1, i1);
    }
}

std::vector<Vec2> Context::roundRectPath(const Rect& r, float radius) const {
    std::vector<Vec2> p;
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    if (radius <= 0.01f) return {{r.x, r.y}, {r.x + r.w, r.y}, {r.x + r.w, r.y + r.h}, {r.x, r.y + r.h}};
    int seg = std::clamp(int(radius * scale_ / 2.5f), 2, 10);
    const Vec2 cs[4] = {{r.x + r.w - radius, r.y + radius}, {r.x + r.w - radius, r.y + r.h - radius}, {r.x + radius, r.y + r.h - radius}, {r.x + radius, r.y + radius}};
    for (int c = 0; c < 4; ++c) {
        float a0 = -kHalfPi + float(c) * kHalfPi;
        for (int i = 0; i <= seg; ++i) {
            float a = a0 + float(i) / float(seg) * kHalfPi;
            p.push_back(cs[c] + Vec2(std::cos(a), std::sin(a)) * radius);
        }
    }
    return p;
}

// --- drawing -------------------------------------------------------------------------------------

void Context::rect(const Rect& r, const Vec4& color, float radius) {
    if (color.w <= 0.001f || r.w <= 0 || r.h <= 0) return;
    fillConvexAA(roundRectPath(r, radius), color);
}

void Context::rectGradient(const Rect& r, const Vec4& top, const Vec4& bottom, float radius) {
    auto pts = roundRectPath(r, radius);
    std::vector<Vec4> cols(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) cols[i] = lerp(top, bottom, saturate((pts[i].y - r.y) / std::max(r.h, 1e-3f)));
    fillConvexAA(pts, top, cols.data());
}

void Context::rectOutline(const Rect& r, const Vec4& color, float t, float radius) {
    auto outer = roundRectPath(r, radius);
    auto inner = roundRectPath(r.shrink(t), std::max(0.0f, radius - t));
    if (outer.size() != inner.size()) {
        line({r.x, r.y}, {r.x + r.w, r.y}, t, color);
        line({r.x + r.w, r.y}, {r.x + r.w, r.y + r.h}, t, color);
        line({r.x + r.w, r.y + r.h}, {r.x, r.y + r.h}, t, color);
        line({r.x, r.y + r.h}, {r.x, r.y}, t, color);
        return;
    }
    for (size_t i = 0; i < outer.size(); ++i) {
        size_t j = (i + 1) % outer.size();
        fillConvexAA({outer[i], outer[j], inner[j], inner[i]}, color);
    }
}

void Context::shadow(const Rect& r, float radius, float spread, float alpha) {
    // layered soft shadow
    for (int i = 4; i >= 1; --i) {
        float s = spread * float(i) / 4.0f;
        rect(Rect(r.x - s, r.y - s + spread * 0.35f, r.w + 2 * s, r.h + 2 * s), Vec4(0, 0, 0, alpha / 4.0f), radius + s);
    }
}

void Context::line(Vec2 a, Vec2 b, float t, const Vec4& color) {
    Vec2 d = b - a;
    float l = d.length();
    if (l < 1e-5f) return;
    Vec2 n = Vec2(-d.y, d.x) / l * (t * 0.5f);
    fillConvexAA({a + n, b + n, b - n, a - n}, color);
}

void Context::polygon(const std::vector<Vec2>& pts, const Vec4& color) { fillConvexAA(pts, color); }

void Context::circle(Vec2 c, float r, const Vec4& color, int segments) {
    std::vector<Vec2> p;
    for (int i = 0; i < segments; ++i) {
        float a = float(i) / float(segments) * kTwoPi;
        p.push_back(c + Vec2(std::cos(a), std::sin(a)) * r);
    }
    fillConvexAA(p, color);
}

void Context::arc(Vec2 c, float r, float t, float a0, float a1, const Vec4& color) {
    int seg = std::max(4, int(std::fabs(a1 - a0) * r * scale_ / 6.0f));
    for (int i = 0; i < seg; ++i) {
        float x0 = a0 + (a1 - a0) * float(i) / float(seg), x1 = a0 + (a1 - a0) * float(i + 1) / float(seg);
        Vec2 d0(std::cos(x0), std::sin(x0)), d1(std::cos(x1), std::sin(x1));
        fillConvexAA({c + d0 * (r + t * 0.5f), c + d1 * (r + t * 0.5f), c + d1 * (r - t * 0.5f), c + d0 * (r - t * 0.5f)}, color);
    }
}

void Context::image(Texture* tex, const Rect& r, const Vec4& tint) {
    if (!dl_ || !tex) return;
    setBatch(tex, true, 0.0f);
    uint32_t c = packColor(tint);
    uint32_t a = vtx({r.x, r.y}, {0, 0}, c), b = vtx({r.x + r.w, r.y}, {1, 0}, c), d = vtx({r.x + r.w, r.y + r.h}, {1, 1}, c),
             e = vtx({r.x, r.y + r.h}, {0, 1}, c);
    addTri(dl_, a, b, d);
    addTri(dl_, a, d, e);
}

void Context::frosted(const Rect& r, float radius, const Vec4& tint, float tintAmount) {
    if (!dl_) return;
    std::vector<Vec2> pts = roundRectPath(r, radius);
    if (pts.size() < 3) return;
    setBatch(nullptr, false, clampf(tintAmount, 0.0f, 1.0f), true);
    size_t n = pts.size();
    float aa = 0.6f / scale_;
    Vec2 c = r.center();
    uint32_t base = uint32_t(dl_->vertices.size());
    auto suv = [&](Vec2 p) { return Vec2(p.x * scale_ / pixelSize_.x, p.y * scale_ / pixelSize_.y); };
    for (size_t i = 0; i < n; ++i) {
        Vec2 d = (pts[i] - c);
        float l = d.length();
        Vec2 o = l > 1e-4f ? pts[i] + d / l * aa : pts[i];
        vtx(pts[i], suv(pts[i]), packColor(tint));
        vtx(o, suv(o), packColor(Vec4(tint.x, tint.y, tint.z, 0.0f)));
    }
    for (size_t i = 1; i + 1 < n; ++i) addTri(dl_, base, base + uint32_t(i * 2), base + uint32_t((i + 1) * 2));
    for (size_t i = 0; i < n; ++i) {
        uint32_t i0 = base + uint32_t(i * 2), o0 = i0 + 1;
        uint32_t i1 = base + uint32_t(((i + 1) % n) * 2), o1 = i1 + 1;
        addTri(dl_, i0, o0, o1);
        addTri(dl_, i0, o1, i1);
    }
}

float Context::measure(const std::string& s, float size, FontStyle style) {
    FontPtr f = fonts_[int(style)];
    return f ? f->measure(s, size) : 0.0f;
}

float Context::text(const std::string& s, Vec2 pos, float size, const Vec4& color, FontStyle style, Align align, float shadowAlpha) {
    FontPtr f = fonts_[int(style)];
    if (!dl_ || !f || s.empty() || color.w <= 0.001f) return 0.0f;
    float w = f->measure(s, size);
    if (align == Align::Center) pos.x -= w * 0.5f;
    else if (align == Align::Right) pos.x -= w;
    if (shadowAlpha > 0.0f) {
        float o = std::max(1.5f, size * 0.05f);
        auto emit = [&](Vec2 p, const Vec4& c, float soft) {
            setBatch(nullptr, false, soft);
            float sc = size / Font::kBakeSize;
            float pen = p.x, baseline = p.y + f->ascent() * sc;
            uint32_t prev = 0;
            uint32_t col = packColor(c);
            for (size_t i = 0; i < s.size();) {
                uint32_t cp = utf8Next(s, i);
                if (cp == '\n') break;
                if (prev) pen += f->kerning(prev, cp) * sc;
                const Glyph& g = f->glyph(cp);
                if (g.size.x > 0) {
                    float x0 = pen + g.offset.x * sc, y0 = baseline + g.offset.y * sc;
                    float x1 = x0 + g.size.x * sc, y1 = y0 + g.size.y * sc;
                    uint32_t a = vtx({x0, y0}, g.uv0, col), b = vtx({x1, y0}, {g.uv1.x, g.uv0.y}, col), d = vtx({x1, y1}, g.uv1, col),
                             e = vtx({x0, y1}, {g.uv0.x, g.uv1.y}, col);
                    addTri(dl_, a, b, d);
                    addTri(dl_, a, d, e);
                }
                pen += g.advance * sc;
                prev = cp;
            }
        };
        emit(pos + Vec2(o * 0.6f, o), Vec4(0, 0, 0, shadowAlpha * color.w), 0.18f);
        emit(pos, color, 0.0f);
        return w;
    }
    setBatch(nullptr, false, 0.0f);
    float sc = size / Font::kBakeSize;
    float pen = pos.x, baseline = pos.y + f->ascent() * sc;
    uint32_t prev = 0;
    uint32_t col = packColor(color);
    for (size_t i = 0; i < s.size();) {
        uint32_t cp = utf8Next(s, i);
        if (cp == '\n') {
            pen = pos.x;
            baseline += (f->ascent() - f->descent() + f->lineGap()) * sc;
            prev = 0;
            continue;
        }
        if (prev) pen += f->kerning(prev, cp) * sc;
        const Glyph& g = f->glyph(cp);
        if (g.size.x > 0) {
            float x0 = pen + g.offset.x * sc, y0 = baseline + g.offset.y * sc;
            float x1 = x0 + g.size.x * sc, y1 = y0 + g.size.y * sc;
            uint32_t a = vtx({x0, y0}, g.uv0, col), b = vtx({x1, y0}, {g.uv1.x, g.uv0.y}, col), d = vtx({x1, y1}, g.uv1, col),
                     e = vtx({x0, y1}, {g.uv0.x, g.uv1.y}, col);
            addTri(dl_, a, b, d);
            addTri(dl_, a, d, e);
        }
        pen += g.advance * sc;
        prev = cp;
    }
    return w;
}

void Context::textBox(const std::string& s, const Rect& r, float size, const Vec4& color, FontStyle style, Align h, float shadowAlpha) {
    FontPtr f = fonts_[int(style)];
    if (!f) return;
    float sc = size / Font::kBakeSize;
    float baseline = r.y + r.h * 0.5f + size * 0.34f;
    float top = baseline - f->ascent() * sc;
    float x = h == Align::Left ? r.x : h == Align::Center ? r.x + r.w * 0.5f : r.x + r.w;
    text(s, Vec2(x, top), size, color, style, h, shadowAlpha);
}

float Context::paragraph(const std::string& s, const Rect& r, float size, const Vec4& color, FontStyle style, float spacing) {
    FontPtr f = fonts_[int(style)];
    if (!f) return 0.0f;
    float lineH = size * spacing, y = r.y;
    std::string line, word;
    auto flushLine = [&]() {
        text(line, Vec2(r.x, y), size, color, style);
        y += lineH;
        line.clear();
    };
    for (size_t i = 0; i <= s.size(); ++i) {
        char c = i < s.size() ? s[i] : ' ';
        if (c == ' ' || c == '\n') {
            std::string cand = line.empty() ? word : line + " " + word;
            if (!line.empty() && f->measure(cand, size) > r.w) {
                flushLine();
                line = word;
            } else {
                line = cand;
            }
            word.clear();
            if (c == '\n') flushLine();
        } else {
            word += c;
        }
    }
    if (!line.empty()) flushLine();
    return y - r.y;
}

void Context::pushClip(const Rect& r) { clips_.push_back(r); }
void Context::popClip() {
    if (!clips_.empty()) clips_.pop_back();
}

// --- widgets -------------------------------------------------------------------------------------

int Context::nextWidget(const Rect& r, bool focusable) {
    int idx = count_++;
    if (focusable && mouseMode_ && (nav_.mouseMoved || nav_.click) && r.contains(mouseCanvas_) && focus_ != idx) {
        focus_ = idx;
        movedFocus_ = true;
    }
    lastFocused_ = idx == focus_;
    float& a = hoverAnim(idx);
    a = dampf(a, lastFocused_ ? 1.0f : 0.0f, 16.0f, dt_);
    return idx;
}

bool Context::button(const std::string& label, const Rect& r, bool primary, bool enabled) {
    int idx = nextWidget(r);
    float a = hoverAnim(idx);
    bool f = lastFocused_;
    Vec4 bg = primary ? theme_.accent : theme_.panelLight;
    if (!enabled) bg.w *= 0.4f;
    Vec4 hi = theme_.accent;
    rect(r, lerp(bg, hi, a * (primary ? 0.0f : 1.0f)), 10.0f);
    if (primary && f) rectOutline(r.shrink(-4), theme_.text, 3.0f, 13.0f);
    Vec4 tc = (primary || a > 0.5f) ? theme_.accentText : theme_.text;
    if (!enabled) tc.w *= 0.5f;
    textBox(label, r, 30.0f, tc, FontStyle::Bold, Align::Center);
    bool act = enabled && f && (nav_.confirm || (nav_.click && r.contains(mouseCanvas_)));
    activatedAny_ |= act;
    return act;
}

int Context::choice(const std::string& label, const std::string& value, const Rect& r) {
    int idx = nextWidget(r);
    float a = hoverAnim(idx);
    rect(r, lerp(theme_.panel, theme_.panelLight, a), 8.0f);
    if (a > 0.01f) rect(Rect(r.x, r.y, 6.0f, r.h), theme_.accent * Vec4(1, 1, 1, a), 3.0f);
    textBox(label, Rect(r.x + 26, r.y, r.w * 0.5f, r.h), 28.0f, theme_.text, FontStyle::SemiBold, Align::Left);
    Rect vr(r.x + r.w * 0.52f, r.y, r.w * 0.46f, r.h);
    textBox(value, vr, 28.0f, lastFocused_ ? theme_.accent : theme_.textDim, FontStyle::Bold, Align::Center);
    float cy = r.y + r.h * 0.5f, s = 9.0f;
    Vec4 ac = lastFocused_ ? theme_.text : theme_.textDim * Vec4(1, 1, 1, 0.6f);
    polygon({{vr.x + 12, cy}, {vr.x + 12 + s, cy - s}, {vr.x + 12 + s, cy + s}}, ac);
    polygon({{vr.x + vr.w - 12, cy}, {vr.x + vr.w - 12 - s, cy + s}, {vr.x + vr.w - 12 - s, cy - s}}, ac);
    int d = 0;
    if (lastFocused_) {
        if (nav_.left) d = -1;
        if (nav_.right || nav_.confirm) d = d == 0 ? 1 : d;
        if (nav_.click && vr.contains(mouseCanvas_)) d = mouseCanvas_.x < vr.x + vr.w * 0.5f ? -1 : 1;
    }
    activatedAny_ |= d != 0;
    return d;
}

bool Context::slider(const std::string& label, float& v, float mn, float mx, float step, const Rect& r, const char* fmt, float displayScale) {
    int idx = nextWidget(r);
    float a = hoverAnim(idx);
    rect(r, lerp(theme_.panel, theme_.panelLight, a), 8.0f);
    if (a > 0.01f) rect(Rect(r.x, r.y, 6.0f, r.h), theme_.accent * Vec4(1, 1, 1, a), 3.0f);
    textBox(label, Rect(r.x + 26, r.y, r.w * 0.5f, r.h), 28.0f, theme_.text, FontStyle::SemiBold, Align::Left);
    Rect bar(r.x + r.w * 0.52f + 10, r.y + r.h * 0.5f - 4, r.w * 0.34f, 8);
    float t = saturate((v - mn) / std::max(mx - mn, 1e-6f));
    rect(bar, Vec4(1, 1, 1, 0.15f), 4.0f);
    rect(Rect(bar.x, bar.y, bar.w * t, bar.h), lastFocused_ ? theme_.accent : theme_.textDim, 4.0f);
    circle(Vec2(bar.x + bar.w * t, bar.y + 4), lastFocused_ ? 12.0f : 9.0f, lastFocused_ ? theme_.text : theme_.textDim);
    char buf[64];
    std::snprintf(buf, sizeof(buf), fmt, v * displayScale);
    textBox(buf, Rect(bar.x + bar.w + 16, r.y, r.x + r.w - bar.x - bar.w - 28, r.h), 26.0f, theme_.text, FontStyle::Bold, Align::Right);
    float old = v;
    if (lastFocused_) {
        if (nav_.left) v -= step;
        if (nav_.right) v += step;
        Rect grab(bar.x - 12, r.y, bar.w + 24, r.h);
        if (nav_.mouseDown && grab.contains(mouseCanvas_)) v = mn + saturate((mouseCanvas_.x - bar.x) / bar.w) * (mx - mn);
        if (nav_.wheel != 0.0f && r.contains(mouseCanvas_)) v += step * (nav_.wheel > 0 ? 1.0f : -1.0f);
    }
    v = clampf(v, mn, mx);
    if (step > 0 && !nav_.mouseDown) v = mn + std::round((v - mn) / step) * step;
    bool changed = std::fabs(v - old) > 1e-6f;
    activatedAny_ |= changed && !nav_.mouseDown;
    return changed;
}

bool Context::toggle(const std::string& label, bool& v, const Rect& r) {
    int idx = nextWidget(r);
    float a = hoverAnim(idx);
    rect(r, lerp(theme_.panel, theme_.panelLight, a), 8.0f);
    if (a > 0.01f) rect(Rect(r.x, r.y, 6.0f, r.h), theme_.accent * Vec4(1, 1, 1, a), 3.0f);
    textBox(label, Rect(r.x + 26, r.y, r.w * 0.5f, r.h), 28.0f, theme_.text, FontStyle::SemiBold, Align::Left);
    Rect pill(r.x + r.w - 110, r.y + r.h * 0.5f - 16, 84, 32);
    rect(pill, v ? theme_.accent : Vec4(1, 1, 1, 0.14f), 16.0f);
    circle(Vec2(v ? pill.x + pill.w - 16 : pill.x + 16, pill.y + 16), 12.0f, v ? theme_.accentText : theme_.textDim);
    textBox(v ? "ON" : "OFF", Rect(pill.x - 90, r.y, 80, r.h), 24.0f, v ? theme_.text : theme_.textDim, FontStyle::Bold, Align::Right);
    bool changed = false;
    if (lastFocused_ && (nav_.confirm || nav_.left || nav_.right || (nav_.click && r.contains(mouseCanvas_)))) {
        v = !v;
        changed = true;
    }
    activatedAny_ |= changed;
    return changed;
}

bool Context::menuItem(const std::string& label, const Rect& r, const std::string& hint) {
    int idx = nextWidget(r);
    float a = hoverAnim(idx);
    float slide = a * 22.0f;
    if (a > 0.01f) {
        rect(Rect(r.x - 8, r.y + 4, (r.w + 8) * a, r.h - 8), Vec4(theme_.accent.x, theme_.accent.y, theme_.accent.z, 0.95f * a), 6.0f);
    }
    Vec4 tc = lerp(theme_.text, theme_.accentText, a);
    textBox(label, Rect(r.x + 18 + slide, r.y, r.w, r.h), 54.0f, tc, FontStyle::Display, Align::Left, a < 0.5f ? 0.5f : 0.0f);
    if (!hint.empty() && a > 0.3f) textBox(hint, Rect(r.x + r.w + 30, r.y, 600, r.h), 24.0f, theme_.textDim * Vec4(1, 1, 1, a), FontStyle::SemiBold, Align::Left, 0.6f);
    bool act = lastFocused_ && (nav_.confirm || (nav_.click && r.contains(mouseCanvas_)));
    activatedAny_ |= act;
    return act;
}

bool Context::card(const std::string& title, const std::string& subtitle, const Rect& r, bool selected, const Vec4& accent) {
    int idx = nextWidget(r);
    float a = hoverAnim(idx);
    Rect rr = r.shrink(-4.0f * a);
    shadow(rr, 12.0f, 14.0f, 0.35f);
    rectGradient(rr, Vec4(accent.x * 0.55f, accent.y * 0.55f, accent.z * 0.55f, 0.95f), Vec4(0.05f, 0.05f, 0.07f, 0.95f), 12.0f);
    if (selected || a > 0.01f) rectOutline(rr, lerp(Vec4(1, 1, 1, 0.5f), theme_.accent, a), 4.0f, 12.0f);
    text(title, Vec2(rr.x + 22, rr.y + rr.h - 92), 44.0f, theme_.text, FontStyle::Display, Align::Left, 0.5f);
    text(subtitle, Vec2(rr.x + 24, rr.y + rr.h - 40), 22.0f, theme_.textDim, FontStyle::SemiBold, Align::Left);
    bool act = lastFocused_ && (nav_.confirm || (nav_.click && r.contains(mouseCanvas_)));
    activatedAny_ |= act;
    return act;
}

}  // namespace sw::ui
