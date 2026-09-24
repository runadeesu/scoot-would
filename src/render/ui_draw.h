// scoot would - 2D draw list produced by the game UI and consumed by the renderer
#pragma once

#include "core/math.h"
#include "render/texture.h"

#include <vector>

namespace sw {

struct UIVertex {
    Vec2 pos;
    Vec2 uv;
    uint32_t color;  // RGBA8 (sRGB)
};

struct UIDrawCmd {
    Texture* texture = nullptr;  // nullptr = font atlas / white
    bool rgbaImage = false;      // false: texture red channel = coverage
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int clipX = 0, clipY = 0, clipW = 0, clipH = 0;  // 0 size = no clip
};

struct UIDrawList {
    std::vector<UIVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<UIDrawCmd> commands;
    Texture* defaultAtlas = nullptr;
    void clear() {
        vertices.clear();
        indices.clear();
        commands.clear();
    }
};

inline uint32_t packColor(const Vec4& c) {
    auto b = [](float v) { return uint32_t(clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return b(c.x) | (b(c.y) << 8) | (b(c.z) << 16) | (b(c.w) << 24);
}

}  // namespace sw
