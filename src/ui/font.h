// scoot would - fonts: TrueType faces (stb_truetype) rendered as signed distance fields into one
// shared, dynamically filled glyph atlas. SDF glyphs stay sharp at every UI scale.
#pragma once

#include "core/math.h"
#include "render/texture.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sw {

class Font;
using FontPtr = std::shared_ptr<Font>;

struct Glyph {
    Vec2 uv0, uv1;     // atlas coordinates
    Vec2 offset;       // quad top-left relative to the pen at baseline (in bake pixels)
    Vec2 size;         // quad size (bake pixels)
    float advance = 0; // bake pixels
    bool valid = false;
};

class Font {
public:
    static constexpr float kBakeSize = 44.0f;  // SDF bake em size
    bool load(const std::string& relPath);
    const std::string& path() const { return path_; }
    const Glyph& glyph(uint32_t codepoint);
    float kerning(uint32_t a, uint32_t b) const;
    float ascent() const { return ascent_; }    // bake pixels
    float descent() const { return descent_; }  // negative
    float lineGap() const { return lineGap_; }
    // width of a string at a pixel size (UTF-8)
    float measure(const std::string& text, float px);
    // code points this face does not have come from the fallback (Japanese text in the Latin UI faces)
    void setFallback(FontPtr f) { fallback_ = std::move(f); }
    bool hasGlyph(uint32_t codepoint) const;

private:
    friend class FontAtlas;
    std::string path_;
    std::vector<uint8_t> data_;
    struct Info;
    std::shared_ptr<Info> info_;
    float scale_ = 1.0f, ascent_ = 0, descent_ = 0, lineGap_ = 0;
    std::unordered_map<uint32_t, Glyph> glyphs_;
    FontPtr fallback_;
};

// shared R8 SDF atlas (value 0.5 = glyph edge); a solid white block serves untextured shapes
class FontAtlas {
public:
    static constexpr int kSize = 4096;  // room for the Latin faces plus a few thousand Japanese glyphs
    static constexpr int kPad = 5;
    void init();
    void shutdown();
    // allocate a glyph bitmap; returns false when full (the atlas is reset next frame)
    bool add(int w, int h, const uint8_t* pixels, Vec2& uv0, Vec2& uv1);
    Vec2 whiteUV() const { return Vec2((1.5f) / kSize, (1.5f) / kSize); }
    // uploads pending glyphs; call before rendering
    void flush();
    Texture* texture() { return texture_.get(); }
    bool full() const { return full_; }

private:
    std::vector<uint8_t> pixels_;
    TexturePtr texture_;
    int penX_ = 8, penY_ = 0, rowH_ = 0;
    bool dirty_ = false, full_ = false;
};

FontAtlas& fontAtlas();
// decode one UTF-8 code point, advancing i
uint32_t utf8Next(const std::string& s, size_t& i);

}  // namespace sw
