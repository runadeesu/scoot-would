// scoot would asset tools - procedural textures: decals (cracks, oil, water stains, tire marks,
// grime, graffiti, stickers, rust, asphalt patches, road arrows, drains), street + shop signs,
// tileable detail normals (surface micro detail, skin pores). All original, generated from noise
// and the project fonts; written as PNG into assets/textures/gen.
#include "assetgen.h"

#include "core/math.h"

#include <stb_image_write.h>
#include <stb_truetype.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace sw::tools {
namespace {

struct Img {
    int w = 0, h = 0;
    std::vector<Vec4> px;
    Img(int w_, int h_, Vec4 c = Vec4(0, 0, 0, 0)) : w(w_), h(h_), px(size_t(w_) * size_t(h_), c) {}
    Vec4& at(int x, int y) { return px[size_t(y) * size_t(w) + size_t(x)]; }
    // "over" compositing of a straight alpha color
    void blend(int x, int y, const Vec4& c) {
        if (x < 0 || y < 0 || x >= w || y >= h || c.w <= 0.0f) return;
        Vec4& d = at(x, y);
        float a = c.w + d.w * (1.0f - c.w);
        if (a <= 1e-6f) return;
        Vec3 rgb = (c.xyz() * c.w + d.xyz() * d.w * (1.0f - c.w)) / a;
        d = Vec4(rgb, a);
    }
};

bool savePng(const Img& im, const std::string& path) {
    std::vector<uint8_t> out(size_t(im.w) * size_t(im.h) * 4);
    for (size_t i = 0; i < im.px.size(); ++i) {
        const Vec4& p = im.px[i];
        auto enc = [](float v) {
            v = clampf(v, 0.0f, 1.0f);
            float s = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
            return uint8_t(std::lround(s * 255.0f));
        };
        out[i * 4 + 0] = enc(p.x);
        out[i * 4 + 1] = enc(p.y);
        out[i * 4 + 2] = enc(p.z);
        out[i * 4 + 3] = uint8_t(std::lround(clampf(p.w, 0.0f, 1.0f) * 255.0f));
    }
    return stbi_write_png(path.c_str(), im.w, im.h, 4, out.data(), im.w * 4) != 0;
}
// linear data (normal maps): no sRGB encoding
bool savePngLinear(const Img& im, const std::string& path) {
    std::vector<uint8_t> out(size_t(im.w) * size_t(im.h) * 4);
    for (size_t i = 0; i < im.px.size(); ++i)
        for (int c = 0; c < 4; ++c) out[i * 4 + size_t(c)] = uint8_t(std::lround(clampf(im.px[i][c], 0.0f, 1.0f) * 255.0f));
    return stbi_write_png(path.c_str(), im.w, im.h, 4, out.data(), im.w * 4) != 0;
}

// periodic value noise (period p cells) so textures tile
float hash2(int x, int y, int seed) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + uint32_t(seed) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float(h ^ (h >> 16)) / 4294967295.0f;
}
float vnoise(float x, float y, int period, int seed) {
    int xi = int(std::floor(x)), yi = int(std::floor(y));
    float fx = x - float(xi), fy = y - float(yi);
    auto wrap = [&](int v) { return ((v % period) + period) % period; };
    float a = hash2(wrap(xi), wrap(yi), seed), b = hash2(wrap(xi + 1), wrap(yi), seed);
    float c = hash2(wrap(xi), wrap(yi + 1), seed), d = hash2(wrap(xi + 1), wrap(yi + 1), seed);
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}
float fbm(float x, float y, int period, int seed, int oct = 5) {
    float s = 0, a = 0.5f, norm = 0;
    for (int o = 0; o < oct; ++o) {
        s += a * vnoise(x, y, period, seed + o * 17);
        norm += a;
        x *= 2;
        y *= 2;
        period *= 2;
        a *= 0.5f;
    }
    return s / norm;
}

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 7) {}
    float uni() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return float((s >> 11) & 0xFFFFFF) / float(0x1000000);
    }
    float range(float a, float b) { return a + (b - a) * uni(); }
};

void disc(Img& im, float cx, float cy, float r, const Vec4& c, float soft = 1.0f) {
    int x0 = int(cx - r - 2), x1 = int(cx + r + 2), y0 = int(cy - r - 2), y1 = int(cy + r + 2);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            float d = std::sqrt(sqr(float(x) + 0.5f - cx) + sqr(float(y) + 0.5f - cy));
            float a = saturate((r - d) / soft + 0.5f);
            if (a > 0) im.blend(x, y, Vec4(c.xyz(), c.w * a));
        }
}
void line(Img& im, Vec2 a, Vec2 b, float r0, float r1, const Vec4& c) {
    float len = (b - a).length();
    int n = std::max(1, int(len / 0.7f));
    for (int i = 0; i <= n; ++i) {
        float t = float(i) / float(n);
        Vec2 p = a + (b - a) * t;
        disc(im, p.x, p.y, lerpf(r0, r1, t), Vec4(c.xyz(), c.w * 0.35f));
    }
}

// --- text (stb_truetype) ---------------------------------------------------------------------------
struct FontFile {
    std::vector<uint8_t> data;
    stbtt_fontinfo info{};
    bool ok = false;
    explicit FontFile(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        data.assign(std::istreambuf_iterator<char>(f), {});
        ok = !data.empty() && stbtt_InitFont(&info, data.data(), 0);
    }
};
// coverage mask of a string, fitted into (w, h) with padding; returns mask image of the same size
std::vector<float> textMask(FontFile& font, const std::string& text, int w, int h, float fill = 0.8f, float slant = 0.0f) {
    std::vector<float> m(size_t(w) * size_t(h), 0.0f);
    if (!font.ok) return m;
    float scale = stbtt_ScaleForPixelHeight(&font.info, float(h) * fill);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&font.info, &asc, &desc, &gap);
    float tw = 0;
    for (char ch : text) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&font.info, ch, &adv, &lsb);
        tw += float(adv) * scale;
    }
    if (tw > float(w) * 0.92f) {
        scale *= float(w) * 0.92f / tw;
        tw = float(w) * 0.92f;
    }
    float x = (float(w) - tw) * 0.5f;
    float baseline = float(h) * 0.5f + (float(asc) + float(desc)) * 0.5f * scale;
    for (char ch : text) {
        int adv, lsb, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(&font.info, ch, &adv, &lsb);
        stbtt_GetCodepointBitmapBox(&font.info, ch, scale, scale, &x0, &y0, &x1, &y1);
        int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0) {
            std::vector<uint8_t> bm(size_t(gw) * size_t(gh));
            stbtt_MakeCodepointBitmap(&font.info, bm.data(), gw, gh, gw, scale, scale, ch);
            for (int yy = 0; yy < gh; ++yy)
                for (int xx = 0; xx < gw; ++xx) {
                    int px = int(x) + x0 + xx + int(float(gh - yy) * slant), py = int(baseline) + y0 + yy;
                    if (px < 0 || py < 0 || px >= w || py >= h) continue;
                    float& d = m[size_t(py) * size_t(w) + size_t(px)];
                    d = std::max(d, float(bm[size_t(yy) * size_t(gw) + size_t(xx)]) / 255.0f);
                }
        }
        x += float(adv) * scale;
    }
    return m;
}
std::vector<float> dilate(const std::vector<float>& m, int w, int h, int r) {
    std::vector<float> o(m.size(), 0.0f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float v = 0;
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    if (dx * dx + dy * dy > r * r) continue;
                    int xx = x + dx, yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                    v = std::max(v, m[size_t(yy) * size_t(w) + size_t(xx)]);
                }
            o[size_t(y) * size_t(w) + size_t(x)] = v;
        }
    return o;
}

// --- decals ------------------------------------------------------------------------------------------
Img crack(int seed) {
    Img im(512, 512);
    Rng r{uint64_t(seed)};
    std::function<void(Vec2, float, float, int)> walk = [&](Vec2 p, float ang, float width, int depth) {
        int steps = int(r.range(30, 70)) / (depth + 1);
        for (int i = 0; i < steps; ++i) {
            ang += r.range(-0.45f, 0.45f);
            Vec2 q = p + Vec2(std::cos(ang), std::sin(ang)) * r.range(4, 10);
            if (q.x < 8 || q.y < 8 || q.x > 504 || q.y > 504) return;
            line(im, p, q, width * 2.6f, width * 2.4f, Vec4(0.22f, 0.21f, 0.2f, 0.12f));  // dusty halo
            line(im, p, q, width, width * 0.97f, Vec4(0.02f, 0.02f, 0.02f, 0.95f));
            width *= 0.985f;
            p = q;
            if (depth < 3 && r.uni() < 0.07f) walk(p, ang + r.range(-1.2f, 1.2f), width * 0.6f, depth + 1);
        }
    };
    walk(Vec2(r.range(40, 90), r.range(180, 330)), r.range(-0.3f, 0.3f), r.range(1.6f, 2.6f), 0);
    return im;
}

Img blob(int seed, Vec3 color, float alpha, float ringiness, float size) {
    Img im(512, 512);
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 512; ++x) {
            float u = (float(x) - 256) / 256, v = (float(y) - 256) / 256;
            float d = std::sqrt(u * u + v * v) / size;
            float n = fbm(float(x) / 64.0f, float(y) / 64.0f, 8, seed);
            float edge = d + (n - 0.5f) * 0.55f;
            float mask = smoothstep(1.0f, 0.75f, edge);
            float ring = smoothstep(0.12f, 0.0f, std::fabs(edge - 0.85f));
            float a = alpha * (mask * (1.0f - ringiness) + ring * ringiness) * (0.75f + 0.5f * fbm(float(x) / 16.0f, float(y) / 16.0f, 32, seed + 3));
            im.at(x, y) = Vec4(color, saturate(a));
        }
    return im;
}

Img tire(int seed) {
    Img im(1024, 256);
    Rng r{uint64_t(seed)};
    for (int band = 0; band < 2; ++band) {
        float cy = 70.0f + float(band) * 116.0f + r.range(-6, 6);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 1024; ++x) {
                float t = float(x) / 1024.0f;
                float fade = smoothstep(0.0f, 0.25f, t) * smoothstep(1.0f, 0.55f, t);
                float dy = std::fabs(float(y) - cy - std::sin(t * 3.0f + float(band)) * 6.0f);
                float w = 26.0f;
                float m = smoothstep(w, w - 6.0f, dy);
                float tread = 0.7f + 0.3f * vnoise(float(x) / 5.0f, float(y) / 3.0f, 1024, seed + band);
                float a = 0.55f * m * fade * tread * (0.6f + 0.6f * fbm(float(x) / 90.0f, float(y) / 40.0f, 16, seed + 7));
                im.blend(x, y, Vec4(0.015f, 0.015f, 0.015f, saturate(a)));
            }
    }
    return im;
}

Img grime(int seed) {
    Img im(512, 512);
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 512; ++x) {
            float v = float(y) / 512.0f;  // 1 at the bottom
            float n = fbm(float(x) / 48.0f, float(y) / 48.0f, 16, seed);
            float streak = fbm(float(x) / 6.0f, float(y) / 160.0f, 128, seed + 9);
            float a = std::pow(v, 2.2f) * (0.35f + 0.65f * n) + std::pow(v, 5.0f) * 0.3f + (streak - 0.55f) * 0.25f * v;
            im.at(x, y) = Vec4(0.09f, 0.075f, 0.06f, saturate(a * 0.85f) * smoothstep(0.0f, 0.08f, std::min(float(x), 511.0f - float(x)) / 512.0f));
        }
    return im;
}

Img graffiti(FontFile& font, const std::string& text, Vec3 fill, Vec3 fill2, Vec3 outline, int seed) {
    const int W = 1024, H = 512;
    Img im(W, H);
    std::vector<float> core = textMask(font, text, W, H, 0.62f, 0.18f);
    std::vector<float> out1 = dilate(core, W, H, 9);
    std::vector<float> out2 = dilate(out1, W, H, 4);
    Rng r{uint64_t(seed)};
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            size_t i = size_t(y) * W + size_t(x);
            float spray = 0.85f + 0.15f * vnoise(float(x) / 2.0f, float(y) / 2.0f, 512, seed);
            if (out2[i] > 0.01f) im.blend(x, y, Vec4(Vec3(0.97f), out2[i] * 0.9f));  // light halo
            if (out1[i] > 0.01f) im.blend(x, y, Vec4(outline, out1[i] * spray));
            if (core[i] > 0.01f) {
                float g = saturate(float(y) / float(H) * 1.6f - 0.3f + (fbm(float(x) / 80.0f, float(y) / 80.0f, 16, seed) - 0.5f) * 0.4f);
                im.blend(x, y, Vec4(lerp(fill, fill2, g), core[i] * spray));
            }
        }
    // drips below letters
    for (int k = 0; k < 14; ++k) {
        int x = int(r.range(120, 900));
        int y = 0;
        for (int yy = H - 1; yy > 0; --yy)
            if (core[size_t(yy) * W + size_t(x)] > 0.5f) {
                y = yy;
                break;
            }
        if (y == 0) continue;
        float len = r.range(20, 110);
        line(im, Vec2(float(x), float(y)), Vec2(float(x) + r.range(-2, 2), float(y) + len), 3.2f, 1.2f, Vec4(fill2, 0.9f));
    }
    // overspray dots
    for (int k = 0; k < 2500; ++k) {
        float x = r.range(0, W), y = r.range(0, H);
        size_t i = size_t(y) * W + size_t(x);
        if (out2[i] > 0.0f || r.uni() < 0.97f) continue;
        disc(im, x, y, r.range(0.6f, 1.6f), Vec4(outline, 0.6f));
    }
    return im;
}

Img panel(FontFile& font, const std::string& text, int w, int h, Vec3 bg, Vec3 fg, Vec3 border, float fill = 0.62f, bool rounded = true) {
    Img im(w, h);
    std::vector<float> m = textMask(font, text, w, h, fill);
    float rad = rounded ? float(h) * 0.12f : 2.0f;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float dx = std::max(0.0f, std::max(rad - float(x), float(x) - (float(w) - 1 - rad)));
            float dy = std::max(0.0f, std::max(rad - float(y), float(y) - (float(h) - 1 - rad)));
            float d = std::sqrt(dx * dx + dy * dy);
            float inside = saturate(rad - d + 0.5f);
            if (inside <= 0) continue;
            float edge = std::min(std::min(float(x), float(w - 1 - x)), std::min(float(y), float(h - 1 - y)));
            Vec3 c = edge < float(h) * 0.05f ? border : bg;
            float wear = 0.92f + 0.08f * fbm(float(x) / 30.0f, float(y) / 30.0f, 64, 5);
            c = lerp(c, fg, m[size_t(y) * size_t(w) + size_t(x)]) * wear;
            im.at(x, y) = Vec4(c, inside);
        }
    return im;
}

Img octagonSign(FontFile& font) {
    const int S = 512;
    Img im(S, S);
    std::vector<float> m = textMask(font, "STOP", S, S / 2, 0.75f);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            float u = (float(x) - 255.5f) / 256.0f, v = (float(y) - 255.5f) / 256.0f;
            float oct = std::max(std::max(std::fabs(u), std::fabs(v)), (std::fabs(u) + std::fabs(v)) * 0.7071f);
            float a = saturate((0.98f - oct) * 200.0f);
            if (a <= 0) continue;
            Vec3 c = oct > 0.9f ? Vec3(0.9f) : Vec3(0.62f, 0.02f, 0.02f);
            int ty = y - S / 4;
            if (ty >= 0 && ty < S / 2) c = lerp(c, Vec3(0.92f), m[size_t(ty) * S + size_t(x)]);
            im.at(x, y) = Vec4(c, a);
        }
    return im;
}

Img noParking() {
    const int S = 512;
    Img im(S, S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            float u = (float(x) - 255.5f) / 256.0f, v = (float(y) - 255.5f) / 256.0f;
            float d = std::sqrt(u * u + v * v);
            float a = saturate((0.99f - d) * 200.0f);
            if (a <= 0) continue;
            Vec3 c = d > 0.8f ? Vec3(0.7f, 0.02f, 0.03f) : Vec3(0.02f, 0.12f, 0.55f);
            float diag = std::fabs(u + v) * 0.7071f;
            if (d < 0.8f && diag < 0.09f) c = Vec3(0.7f, 0.02f, 0.03f);
            im.at(x, y) = Vec4(c, a);
        }
    return im;
}

Img arrow() {
    Img im(256, 512);
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 256; ++x) {
            float u = (float(x) - 128) / 128, v = float(y) / 512;
            bool shaft = std::fabs(u) < 0.18f && v > 0.35f && v < 0.97f;
            bool head = v > 0.03f && v <= 0.38f && std::fabs(u) < (v - 0.03f) / 0.35f * 0.8f;
            if (!shaft && !head) continue;
            float wear = smoothstep(0.25f, 0.6f, fbm(float(x) / 12.0f, float(y) / 12.0f, 64, 4));
            im.at(x, y) = Vec4(Vec3(0.9f, 0.9f, 0.86f), 0.92f * (1.0f - wear * 0.6f));
        }
    return im;
}

Img drain() {
    Img im(512, 256);
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 512; ++x) {
            bool frame = x < 18 || x > 493 || y < 18 || y > 237;
            bool slot = !frame && (x % 34) > 14;
            Vec3 c = frame ? Vec3(0.13f, 0.12f, 0.11f) : slot ? Vec3(0.01f) : Vec3(0.16f, 0.15f, 0.14f);
            c *= 0.8f + 0.4f * fbm(float(x) / 20.0f, float(y) / 20.0f, 32, 8);
            im.at(x, y) = Vec4(c, 1.0f);
        }
    return im;
}

Img rust() {
    Img im(256, 1024);
    for (int y = 0; y < 1024; ++y)
        for (int x = 0; x < 256; ++x) {
            float u = (float(x) - 128) / 128;
            float n = fbm(float(x) / 10.0f, float(y) / 140.0f, 32, 11);
            float a = (1.0f - float(y) / 1024.0f) * smoothstep(0.7f, 0.1f, std::fabs(u) + (n - 0.5f) * 0.7f) * 0.7f;
            im.at(x, y) = Vec4(Vec3(0.32f, 0.13f, 0.05f) * (0.7f + 0.6f * n), saturate(a));
        }
    return im;
}

Img patchDecal(int seed) {
    Img im(512, 512);
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 512; ++x) {
            float u = std::fabs(float(x) - 256) / 256, v = std::fabs(float(y) - 256) / 256;
            float n = fbm(float(x) / 40.0f, float(y) / 40.0f, 16, seed);
            float d = std::max(u, v) + (n - 0.5f) * 0.12f;
            float a = smoothstep(0.93f, 0.9f, d);
            float seal = smoothstep(0.035f, 0.0f, std::fabs(d - 0.9f));
            Vec3 c = Vec3(0.035f, 0.035f, 0.037f) * (0.8f + 0.4f * fbm(float(x) / 6.0f, float(y) / 6.0f, 128, seed + 2));
            c = lerp(c, Vec3(0.01f), seal);
            im.at(x, y) = Vec4(c, saturate(std::max(a * 0.92f, seal)));
        }
    return im;
}

Img sticker(FontFile& font, const std::string& text, Vec3 bg, Vec3 fg) {
    Img im = panel(font, text, 256, 128, bg, fg, bg * 0.8f, 0.6f, true);
    // scuffs
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x)
            if (fbm(float(x) / 10.0f, float(y) / 10.0f, 64, 3) > 0.72f) im.at(x, y).w *= 0.2f;
    return im;
}

// tileable normal map from a height function
Img normalFromHeight(int size, const std::function<float(int, int)>& height, float strength) {
    std::vector<float> hgt(size_t(size) * size_t(size));
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) hgt[size_t(y) * size_t(size) + size_t(x)] = height(x, y);
    Img im(size, size);
    auto H = [&](int x, int y) { return hgt[size_t((y + size) % size) * size_t(size) + size_t((x + size) % size)]; };
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float dx = (H(x + 1, y) - H(x - 1, y)) * strength, dy = (H(x, y + 1) - H(x, y - 1)) * strength;
            Vec3 n = Vec3(-dx, -dy, 1.0f).normalized();
            im.at(x, y) = Vec4(n.x * 0.5f + 0.5f, n.y * 0.5f + 0.5f, n.z * 0.5f + 0.5f, 1.0f);
        }
    return im;
}

// foliage atlas: 2 x 2 twig clusters (leaves along a curved stem with side shoots). Straight alpha
// colour + a height field (leaf blades folded along the midrib, veins) for the normal map.
struct LeafAtlas {
    Img color, height;
    LeafAtlas(int s) : color(s, s), height(s, s, Vec4(0.5f, 0.5f, 0.5f, 0.0f)) {}
};

void leaf(LeafAtlas& at, Vec2 base, Vec2 dir, float len, float wid, Vec3 col, Rng& r, int x0, int y0, int x1, int y1) {
    Vec2 side(-dir.y, dir.x);
    float bend = r.range(-0.12f, 0.12f);
    int bx0 = std::max(x0, int(std::min(base.x, base.x + dir.x * len) - wid - 4)), bx1 = std::min(x1, int(std::max(base.x, base.x + dir.x * len) + wid + 4));
    int by0 = std::max(y0, int(std::min(base.y, base.y + dir.y * len) - wid - 4)), by1 = std::min(y1, int(std::max(base.y, base.y + dir.y * len) + wid + 4));
    float shade = r.range(0.8f, 1.15f);
    for (int y = by0; y <= by1; ++y)
        for (int x = bx0; x <= bx1; ++x) {
            Vec2 d = Vec2(float(x) + 0.5f, float(y) + 0.5f) - base;
            float u = dot(d, dir) / len;
            if (u < 0.0f || u > 1.0f) continue;
            float v = dot(d, side) - bend * std::sin(u * kPi) * len;  // curved midrib
            float half = wid * std::pow(std::sin(u * kPi), 0.75f) * (1.0f - 0.35f * u) + 0.4f;
            float edge = (half - std::fabs(v));
            if (edge <= -0.5f) continue;
            float a = saturate(edge + 0.5f);
            float vn = std::fabs(v) / std::max(half, 0.5f);
            // colour: lighter midrib, darker serrated rim, fine noise, faint veins
            float veins = std::pow(std::fabs(std::sin((u * 9.0f + vn * 3.0f) * kPi)), 18.0f) * 0.12f;
            float n = fbm(float(x) / 7.0f, float(y) / 7.0f, 256, 5, 3);
            Vec3 c = col * shade * (0.82f + 0.3f * n) * (1.0f - 0.25f * smoothstep(0.7f, 1.0f, vn));
            c = lerp(c, col * 1.35f + Vec3(0.02f, 0.03f, 0.0f), std::max(0.0f, 1.0f - vn * 9.0f) * 0.6f + veins);
            at.color.blend(x, y, Vec4(c, a));
            // height: V fold along the midrib + veins, higher towards the stem
            float hgt = 0.55f + 0.25f * (1.0f - vn) - veins * 0.8f + 0.1f * (1.0f - u);
            Vec4& hp = at.height.at(x, y);
            hp = Vec4(lerpf(hp.x, hgt, a), 0, 0, std::max(hp.w, a));
        }
}

LeafAtlas foliageAtlas(int size, int seed) {
    LeafAtlas at(size);
    int cell = size / 2;
    Rng r{uint64_t(seed)};
    const Vec3 greens[] = {{0.11f, 0.2f, 0.045f}, {0.14f, 0.24f, 0.05f}, {0.09f, 0.17f, 0.05f}, {0.18f, 0.25f, 0.06f}, {0.12f, 0.22f, 0.07f}};
    for (int cy = 0; cy < 2; ++cy)
        for (int cx = 0; cx < 2; ++cx) {
            int x0 = cx * cell + 4, y0 = cy * cell + 4, x1 = (cx + 1) * cell - 5, y1 = (cy + 1) * cell - 5;
            float cs = float(cell);
            // main stem from the bottom centre curving up, 3-4 side shoots
            Vec2 start(float(cx * cell) + cs * 0.5f + r.range(-0.08f, 0.08f) * cs, float((cy + 1) * cell) - cs * 0.04f);
            std::vector<std::pair<Vec2, Vec2>> stems;  // (point, direction)
            Vec2 p = start;
            Vec2 dir = Vec2(r.range(-0.2f, 0.2f), -1.0f).normalized();
            float curve = r.range(-0.012f, 0.012f);
            int steps = 60;
            for (int i = 0; i < steps; ++i) {
                float step = cs * 0.86f / float(steps);
                Vec2 np = p + dir * step;
                line(at.color, p, np, 2.2f - 1.6f * float(i) / float(steps), 2.2f - 1.6f * float(i + 1) / float(steps), Vec4(0.16f, 0.12f, 0.06f, 1.0f));
                p = np;
                float a = curve;
                dir = Vec2(dir.x * std::cos(a) - dir.y * std::sin(a), dir.x * std::sin(a) + dir.y * std::cos(a));
                stems.push_back({p, dir});
            }
            // side shoots
            std::vector<std::pair<Vec2, Vec2>> shoots;
            for (int k = 0; k < 4; ++k) {
                auto& s = stems[size_t(10 + k * 11)];
                float sgn = (k % 2) ? 1.0f : -1.0f;
                float ang = sgn * r.range(0.5f, 0.9f);
                Vec2 d(s.second.x * std::cos(ang) - s.second.y * std::sin(ang), s.second.x * std::sin(ang) + s.second.y * std::cos(ang));
                Vec2 q = s.first;
                int n = 22 - k * 3;
                for (int i = 0; i < n; ++i) {
                    Vec2 nq = q + d * (cs * 0.4f / 22.0f);
                    line(at.color, q, nq, 1.2f, 0.8f, Vec4(0.16f, 0.12f, 0.06f, 1.0f));
                    q = nq;
                    shoots.push_back({q, d});
                }
            }
            // leaves along stem + shoots, alternating sides, pointing forward-outwards
            auto place = [&](const std::vector<std::pair<Vec2, Vec2>>& path, int every, float scale) {
                for (size_t i = 3; i < path.size(); i += size_t(every)) {
                    for (float sgn : {-1.0f, 1.0f}) {
                        const auto& s = path[i];
                        float ang = sgn * r.range(0.45f, 1.05f);
                        Vec2 d(s.second.x * std::cos(ang) - s.second.y * std::sin(ang), s.second.x * std::sin(ang) + s.second.y * std::cos(ang));
                        float len = cs * r.range(0.13f, 0.19f) * scale;
                        Vec3 col = greens[size_t(r.uni() * 4.99f)];
                        leaf(at, s.first, d.normalized(), len, len * r.range(0.22f, 0.3f), col, r, x0, y0, x1, y1);
                    }
                }
                // terminal leaf
                const auto& e = path.back();
                leaf(at, e.first, e.second, cs * 0.16f * scale, cs * 0.045f * scale, greens[1], r, x0, y0, x1, y1);
            };
            place(stems, 5, 1.0f);
            place(shoots, 4, 0.85f);
        }
    return at;
}

}  // namespace

bool generateTextures(const std::string& root) {
    std::string dir = root + "/assets/textures/gen";
    FontFile display(root + "/assets/fonts/BarlowCondensed-ExtraBoldItalic.ttf");
    FontFile bold(root + "/assets/fonts/Barlow-Bold.ttf");
    int ok = 0, total = 0;
    auto save = [&](const Img& im, const std::string& name, bool linear = false) {
        ++total;
        std::string p = dir + "/" + name + ".png";
        if (linear ? savePngLinear(im, p) : savePng(im, p)) ++ok;
        else std::printf("textures: cannot write %s\n", p.c_str());
    };
    for (int i = 1; i <= 3; ++i) save(crack(i * 31), "decal_crack_" + std::to_string(i));
    save(blob(3, Vec3(0.012f, 0.012f, 0.014f), 0.75f, 0.2f, 0.85f), "decal_oil_1");
    save(blob(8, Vec3(0.015f, 0.014f, 0.012f), 0.6f, 0.35f, 0.7f), "decal_oil_2");
    save(blob(13, Vec3(0.06f, 0.06f, 0.055f), 0.35f, 0.75f, 0.9f), "decal_water_1");
    save(blob(21, Vec3(0.08f, 0.07f, 0.06f), 0.3f, 0.55f, 0.8f), "decal_water_2");
    save(tire(5), "decal_tire");
    save(grime(7), "decal_grime");
    save(rust(), "decal_rust");
    save(patchDecal(17), "decal_patch");
    save(arrow(), "decal_arrow");
    save(drain(), "decal_drain");
    save(graffiti(display, "SCOOT", Vec3(0.95f, 0.72f, 0.05f), Vec3(0.9f, 0.25f, 0.05f), Vec3(0.02f), 1), "decal_graffiti_1");
    save(graffiti(display, "FLOW", Vec3(0.2f, 0.6f, 0.95f), Vec3(0.55f, 0.15f, 0.75f), Vec3(0.02f), 2), "decal_graffiti_2");
    save(graffiti(display, "WOULD", Vec3(0.92f, 0.3f, 0.45f), Vec3(0.95f, 0.85f, 0.9f), Vec3(0.05f, 0.04f, 0.1f), 3), "decal_graffiti_3");
    save(graffiti(display, "KDX", Vec3(0.3f, 0.85f, 0.4f), Vec3(0.05f, 0.35f, 0.15f), Vec3(0.02f), 4), "decal_graffiti_4");
    save(sticker(bold, "SW", Vec3(0.95f, 0.75f, 0.1f), Vec3(0.05f)), "decal_sticker_1");
    save(sticker(display, "FLOW", Vec3(0.1f, 0.12f, 0.15f), Vec3(0.95f)), "decal_sticker_2");
    // signs
    save(octagonSign(bold), "sign_stop");
    save(noParking(), "sign_noparking");
    save(panel(bold, "ONE WAY", 1024, 256, Vec3(0.02f), Vec3(0.95f), Vec3(0.95f), 0.5f, false), "sign_oneway");
    save(panel(bold, "SPEED 25", 512, 512, Vec3(0.92f), Vec3(0.02f), Vec3(0.02f), 0.3f, false), "sign_speed");
    save(panel(bold, "SCOOT AVE", 1024, 256, Vec3(0.02f, 0.3f, 0.12f), Vec3(0.95f), Vec3(0.95f), 0.55f, true), "sign_street_1");
    save(panel(bold, "FLOW ST", 1024, 256, Vec3(0.02f, 0.3f, 0.12f), Vec3(0.95f), Vec3(0.95f), 0.55f, true), "sign_street_2");
    // shop fronts
    struct Shop {
        const char* name;
        const char* text;
        Vec3 bg, fg;
    };
    const Shop shops[] = {{"shop_coffee", "COFFEE", {0.08f, 0.05f, 0.03f}, {0.95f, 0.85f, 0.65f}},
                          {"shop_laundry", "LAUNDROMAT", {0.9f, 0.92f, 0.95f}, {0.05f, 0.25f, 0.6f}},
                          {"shop_pizza", "PIZZA", {0.6f, 0.05f, 0.03f}, {0.98f, 0.9f, 0.6f}},
                          {"shop_supply", "SCOOT SUPPLY", {0.05f, 0.05f, 0.06f}, {0.98f, 0.76f, 0.08f}},
                          {"shop_pharmacy", "PHARMACY", {0.05f, 0.35f, 0.15f}, {0.95f, 0.95f, 0.95f}},
                          {"shop_market", "MINI MART", {0.95f, 0.55f, 0.05f}, {0.1f, 0.05f, 0.02f}},
                          {"shop_barber", "BARBER", {0.1f, 0.12f, 0.2f}, {0.9f, 0.9f, 0.9f}},
                          {"shop_records", "RECORDS", {0.85f, 0.2f, 0.4f}, {0.98f, 0.95f, 0.95f}}};
    for (auto& s : shops) save(panel(display, s.text, 1024, 256, s.bg, s.fg, s.bg * 0.6f, 0.6f, false), s.name);
    // tileable detail normals
    save(normalFromHeight(512, [](int x, int y) { return fbm(float(x) / 16.0f, float(y) / 16.0f, 32, 41, 4) + 0.35f * vnoise(float(x) / 2.0f, float(y) / 2.0f, 256, 43); },
                          2.0f),
         "detail_normal", true);
    save(normalFromHeight(512,
                          [](int x, int y) {
                              // skin: fine pores (cell centres) + creases
                              float cells = 0.0f;
                              float fx = float(x) / 8.0f, fy = float(y) / 8.0f;
                              int cx = int(std::floor(fx)), cy = int(std::floor(fy));
                              float best = 9.0f;
                              for (int j = -1; j <= 1; ++j)
                                  for (int i = -1; i <= 1; ++i) {
                                      int gx = ((cx + i) % 64 + 64) % 64, gy = ((cy + j) % 64 + 64) % 64;
                                      float px = float(cx + i) + hash2(gx, gy, 7), py = float(cy + j) + hash2(gx, gy, 9);
                                      best = std::min(best, sqr(px - fx) + sqr(py - fy));
                                  }
                              cells = smoothstep(0.0f, 0.08f, best);
                              return cells * 0.6f + fbm(float(x) / 40.0f, float(y) / 40.0f, 13, 5, 3) * 0.4f;
                          },
                          3.0f),
         "skin_detail_normal", true);
    // scooter shop graphics (fictional brands only)
    {
        // display panel: hexagon mark + wordmark on dark brushed board
        const int W = 1536, Hh = 768;
        Img im(W, Hh, Vec4(0.02f, 0.022f, 0.026f, 1.0f));
        for (int y = 0; y < Hh; ++y)
            for (int x = 0; x < W; ++x) {
                float b = 0.018f + 0.01f * fbm(float(x) / 400.0f, float(y) / 3.0f, 4, 17, 3);
                im.at(x, y) = Vec4(b, b * 1.05f, b * 1.12f, 1.0f);
            }
        Vec2 hc(float(W) * 0.26f, float(Hh) * 0.5f);
        float R = float(Hh) * 0.28f;
        for (int y = 0; y < Hh; ++y)
            for (int x = 0; x < W; ++x) {
                Vec2 p = Vec2(float(x), float(y)) - hc;
                // hexagon distance (pointy top)
                float ax = std::fabs(p.x), ay = std::fabs(p.y);
                float d = std::max(ax * 0.866f + ay * 0.5f, ay) - R;
                float ring = 1.0f - smoothstep(0.0f, 2.0f, std::fabs(d + R * 0.06f) - R * 0.06f);
                // inner triangle mark
                Vec2 q = p / (R * 0.55f);
                float tri = std::max(std::fabs(q.x) * 0.866f + q.y * 0.5f, -q.y) - 0.5f;
                float triA = 1.0f - smoothstep(-0.02f, 0.02f, tri);
                float triB = 1.0f - smoothstep(-0.02f, 0.02f, std::max(std::fabs(q.x) * 0.866f + q.y * 0.5f, -q.y) - 0.3f);
                float mark = std::max(ring, triA - triB);
                if (mark > 0.0f) im.blend(x, y, Vec4(0.86f, 0.9f, 0.92f, mark));
            }
        std::vector<float> word = textMask(display, "SCOOT WOULD", W / 2, Hh / 3, 0.72f);
        for (int y = 0; y < Hh / 3; ++y)
            for (int x = 0; x < W / 2; ++x) {
                float a = word[size_t(y) * size_t(W / 2) + size_t(x)];
                if (a > 0.0f) im.blend(x + W * 44 / 100, y + Hh / 3, Vec4(0.9f, 0.92f, 0.93f, a));
            }
        save(im, "shop_panel");
    }
    save(panel(display, "FLOWLAB", 1024, 384, Vec3(0.95f, 0.95f, 0.94f), Vec3(0.03f), Vec3(0.95f), 0.62f, false), "shop_banner_1");
    save(panel(display, "KDX PRO", 1024, 384, Vec3(0.97f, 0.97f, 0.96f), Vec3(0.75f, 0.06f, 0.05f), Vec3(0.97f), 0.62f, false), "shop_banner_2");
    save(panel(bold, "AXLE CO.", 1024, 384, Vec3(0.05f, 0.06f, 0.08f), Vec3(0.2f, 0.85f, 0.4f), Vec3(0.05f), 0.55f, false), "shop_banner_3");
    {
        // laptop screen: a web shop grid of product tiles
        const int W = 512, Hh = 320;
        Img im(W, Hh, Vec4(0.93f, 0.94f, 0.95f, 1.0f));
        Rng r{uint64_t(91)};
        for (int y = 0; y < 22; ++y)
            for (int x = 0; x < W; ++x) im.at(x, y) = Vec4(0.16f, 0.18f, 0.22f, 1.0f);
        for (int ty = 0; ty < 3; ++ty)
            for (int tx = 0; tx < 6; ++tx) {
                int x0 = 18 + tx * 80, y0 = 40 + ty * 90;
                Vec3 c(r.range(0.2f, 0.8f), r.range(0.3f, 0.7f), r.range(0.3f, 0.8f));
                for (int y = y0; y < y0 + 62; ++y)
                    for (int x = x0; x < x0 + 70; ++x) {
                        float sky = float(y - y0) / 62.0f;
                        im.at(x, y) = Vec4(lerp(c, c * 0.45f, sky), 1.0f);
                    }
                for (int y = y0 + 66; y < y0 + 72; ++y)
                    for (int x = x0; x < x0 + 50; ++x) im.at(x, y) = Vec4(0.35f, 0.36f, 0.38f, 1.0f);
            }
        save(im, "shop_laptop");
    }
    {
        // poster: rider silhouette over a sunset gradient (original artwork)
        const int W = 512, Hh = 768;
        Img im(W, Hh);
        for (int y = 0; y < Hh; ++y)
            for (int x = 0; x < W; ++x) {
                float t = float(y) / float(Hh);
                Vec3 c = lerp(Vec3(0.95f, 0.55f, 0.2f), Vec3(0.25f, 0.1f, 0.35f), t);
                im.at(x, y) = Vec4(c, 1.0f);
            }
        // ground ramp + silhouette shapes (body, arms, bars, deck, wheels)
        auto dark = Vec4(0.03f, 0.02f, 0.04f, 1.0f);
        for (int y = int(float(Hh) * 0.78f); y < Hh; ++y)
            for (int x = 0; x < W; ++x)
                if (float(y) > float(Hh) * 0.78f + float(x) * 0.12f) im.at(x, y) = dark;
        disc(im, 250, 250, 26, dark);                               // head
        line(im, {250, 270}, {280, 420}, 30, 24, dark);              // torso
        line(im, {262, 300}, {335, 360}, 11, 9, dark);               // arm
        line(im, {280, 420}, {330, 520}, 20, 14, dark);              // leg
        line(im, {280, 420}, {230, 500}, 18, 12, dark);              // leg
        line(im, {335, 360}, {300, 560}, 6, 6, dark);                // bar stem
        line(im, {310, 355}, {365, 365}, 6, 6, dark);                // bar
        line(im, {215, 525}, {330, 560}, 7, 7, dark);                // deck
        disc(im, 215, 540, 16, dark);
        disc(im, 312, 570, 16, dark);
        std::vector<float> word = textMask(display, "SEND IT", W, 160, 0.7f);
        for (int y = 0; y < 160; ++y)
            for (int x = 0; x < W; ++x)
                if (word[size_t(y) * size_t(W) + size_t(x)] > 0.0f) im.blend(x, y + 40, Vec4(0.98f, 0.97f, 0.94f, word[size_t(y) * size_t(W) + size_t(x)]));
        save(im, "shop_poster");
    }
    // scooter griptape: silicon carbide grit (dense sharp grains on a black backing); tileable, ~5 cm per
    // repeat. Uniform on purpose: wear patches inside a 5 cm tile repeat visibly across a 50 cm deck
    {
        const int N = 512;
        auto grain = [](int x, int y) {
            // worley style grit: distance to the nearest grain centre on a 128 cell periodic grid
            float fx = float(x) / 4.0f, fy = float(y) / 4.0f;
            int cx = int(std::floor(fx)), cy = int(std::floor(fy));
            float best = 9.0f, second = 9.0f;
            for (int j = -1; j <= 1; ++j)
                for (int i = -1; i <= 1; ++i) {
                    int gx = ((cx + i) % 128 + 128) % 128, gy = ((cy + j) % 128 + 128) % 128;
                    float px = float(cx + i) + hash2(gx, gy, 71), py = float(cy + j) + hash2(gx, gy, 73);
                    float dd = sqr(px - fx) + sqr(py - fy);
                    if (dd < best) {
                        second = best;
                        best = dd;
                    } else if (dd < second) {
                        second = dd;
                    }
                }
            return std::sqrt(second) - std::sqrt(best);  // ridge between grains ~ 0
        };
        Img col(N, N);
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                float g = saturate(grain(x, y) * 1.8f);
                float sparkle = hash2(x, y, 5) > 0.992f ? 0.06f : 0.0f;
                float v = 0.018f + 0.03f * g + sparkle * g;
                col.at(x, y) = Vec4(v, v, v * 1.02f, 1.0f);
            }
        save(col, "griptape");
        save(normalFromHeight(N, [&](int x, int y) { return saturate(grain(x, y) * 1.8f); }, 5.0f), "griptape_nrm", true);
    }
    // foliage atlas for the leaf card trees (colour with alpha + normal map)
    {
        LeafAtlas at = foliageAtlas(1024, 77);
        // bleed leaf colours into the transparent texels so mip maps do not get dark halos
        Img col = at.color;
        for (int pass = 0; pass < 8; ++pass) {
            Img next = col;
            for (int y = 0; y < col.h; ++y)
                for (int x = 0; x < col.w; ++x) {
                    if (col.at(x, y).w > 0.0f) continue;
                    Vec3 sum(0);
                    int n = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            int xx = x + dx, yy = y + dy;
                            if (xx < 0 || yy < 0 || xx >= col.w || yy >= col.h) continue;
                            const Vec4& q = col.at(xx, yy);
                            if (q.w > 0.0f) {
                                sum += q.xyz();
                                ++n;
                            }
                        }
                    if (n) next.at(x, y) = Vec4(sum / float(n), 0.0f);
                }
            col = next;
        }
        for (auto& p : col.px)
            if (p.w <= 0.0f && p.x == 0.0f && p.y == 0.0f && p.z == 0.0f) p = Vec4(0.1f, 0.17f, 0.05f, 0.0f);
        save(col, "foliage_leaves");
        Img nrm = normalFromHeight(1024, [&](int x, int y) { return at.height.at(x, y).x; }, 6.0f);
        save(nrm, "foliage_leaves_nrm", true);
    }
    std::printf("textures: %d/%d -> %s\n", ok, total, dir.c_str());
    return ok == total;
}

}  // namespace sw::tools
