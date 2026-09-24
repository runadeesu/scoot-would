#include "ui/font.h"

#include "assets/asset_manager.h"
#include "core/filesystem.h"
#include "core/log.h"
#include "render/gpu.h"

#include <stb_truetype.h>

#include <cstring>

namespace sw {

struct Font::Info {
    stbtt_fontinfo f;
};

namespace {
FontAtlas g_atlas;
}
FontAtlas& fontAtlas() { return g_atlas; }

uint32_t utf8Next(const std::string& s, size_t& i) {
    unsigned char c = static_cast<unsigned char>(s[i++]);
    if (c < 0x80) return c;
    int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
    uint32_t cp = c & (0x3F >> extra);
    for (int k = 0; k < extra && i < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
    return cp;
}

bool Font::load(const std::string& relPath) {
    auto bin = fs::readBinary(fs::resolve(relPath));
    if (!bin) {
        LOG_ERROR("font: cannot read %s", relPath.c_str());
        return false;
    }
    data_ = std::move(*bin);
    info_ = std::make_shared<Info>();
    if (!stbtt_InitFont(&info_->f, data_.data(), stbtt_GetFontOffsetForIndex(data_.data(), 0))) {
        LOG_ERROR("font: invalid font %s", relPath.c_str());
        info_.reset();
        return false;
    }
    path_ = relPath;
    scale_ = stbtt_ScaleForMappingEmToPixels(&info_->f, kBakeSize);
    int a, d, g;
    stbtt_GetFontVMetrics(&info_->f, &a, &d, &g);
    ascent_ = float(a) * scale_;
    descent_ = float(d) * scale_;
    lineGap_ = float(g) * scale_;
    return true;
}

const Glyph& Font::glyph(uint32_t cp) {
    auto it = glyphs_.find(cp);
    if (it != glyphs_.end()) return it->second;
    Glyph g;
    if (info_) {
        int gi = stbtt_FindGlyphIndex(&info_->f, int(cp));
        if (gi == 0 && cp != ' ') gi = stbtt_FindGlyphIndex(&info_->f, '?');
        int adv, lsb;
        stbtt_GetGlyphHMetrics(&info_->f, gi, &adv, &lsb);
        g.advance = float(adv) * scale_;
        int w = 0, h = 0, xo = 0, yo = 0;
        const float distScale = 128.0f / float(FontAtlas::kPad);
        unsigned char* sdf = stbtt_GetGlyphSDF(&info_->f, scale_, gi, FontAtlas::kPad, 128, distScale, &w, &h, &xo, &yo);
        if (sdf && w > 0 && h > 0) {
            if (fontAtlas().add(w, h, sdf, g.uv0, g.uv1)) {
                g.offset = Vec2(float(xo), float(yo));
                g.size = Vec2(float(w), float(h));
                g.valid = true;
            }
        } else {
            g.valid = true;  // whitespace: advance only
            g.size = Vec2(0);
        }
        if (sdf) stbtt_FreeSDF(sdf, nullptr);
    }
    return glyphs_[cp] = g;
}

float Font::kerning(uint32_t a, uint32_t b) const {
    if (!info_) return 0.0f;
    return float(stbtt_GetCodepointKernAdvance(&info_->f, int(a), int(b))) * scale_;
}

float Font::measure(const std::string& text, float px) {
    float s = px / kBakeSize, w = 0.0f, best = 0.0f;
    uint32_t prev = 0;
    for (size_t i = 0; i < text.size();) {
        uint32_t cp = utf8Next(text, i);
        if (cp == '\n') {
            best = std::max(best, w);
            w = 0;
            prev = 0;
            continue;
        }
        if (prev) w += kerning(prev, cp) * s;
        w += glyph(cp).advance * s;
        prev = cp;
    }
    return std::max(best, w);
}

// asset manager hook (declared in assets/asset_manager.h): faces are cached by path, any size
FontPtr AssetManager::font(const std::string& relPath, float) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto key = std::make_pair(relPath, 0);
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return it->second;
    auto f = std::make_shared<Font>();
    if (!f->load(relPath)) f.reset();
    fonts_[key] = f;
    return f;
}

void FontAtlas::init() {
    pixels_.assign(size_t(kSize) * kSize, 0);
    // solid block for untextured shapes (SDF value 1 = fully inside)
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) pixels_[size_t(y * kSize + x)] = 255;
    penX_ = 8;
    penY_ = 0;
    rowH_ = 4;
    texture_ = std::make_shared<Texture>();
    texture_->path = "ui:font_atlas";
    texture_->srgb = false;
    texture_->gpuTex = gpu().createTexture2D(kSize, kSize, SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, 1, "font atlas");
    dirty_ = true;
    full_ = false;
}

void FontAtlas::shutdown() {
    texture_.reset();
    pixels_.clear();
}

bool FontAtlas::add(int w, int h, const uint8_t* px, Vec2& uv0, Vec2& uv1) {
    if (pixels_.empty()) init();
    if (penX_ + w + 1 > kSize) {
        penX_ = 0;
        penY_ += rowH_ + 1;
        rowH_ = 0;
    }
    if (penY_ + h + 1 > kSize) {
        if (!full_) LOG_WARN("font: glyph atlas full");
        full_ = true;
        return false;
    }
    for (int y = 0; y < h; ++y) std::memcpy(&pixels_[size_t((penY_ + y) * kSize + penX_)], px + size_t(y * w), size_t(w));
    uv0 = Vec2(float(penX_) / kSize, float(penY_) / kSize);
    uv1 = Vec2(float(penX_ + w) / kSize, float(penY_ + h) / kSize);
    penX_ += w + 1;
    rowH_ = std::max(rowH_, h);
    dirty_ = true;
    return true;
}

void FontAtlas::flush() {
    if (!dirty_ || !texture_ || !texture_->gpuTex) return;
    // upload only the used rows
    int rows = std::min(kSize, penY_ + rowH_ + 1);
    gpu().uploadTexture(texture_->gpuTex, pixels_.data(), uint32_t(size_t(rows) * kSize), 0, 0, kSize, uint32_t(rows));
    dirty_ = false;
}

}  // namespace sw
