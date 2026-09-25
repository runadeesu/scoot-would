// scoot would - customization thumbnails: the real scooter part meshes, rasterised on the CPU under a
// studio light rig. Lighting is solved once per part variant in a tint independent form
// (colour = tint * A + B), so every palette colour is only a cheap recolour of that render.
#pragma once

#include "core/math.h"
#include "render/texture.h"

#include <map>
#include <string>
#include <vector>

namespace sw {

class PartThumbnails {
public:
    enum Kind { Deck = 0, Bars, Clamp, Wheel, Urethane, Grips, KindCount };
    static constexpr int kSize = 160;

    // texture of the part in (variant, colour); nullptr while it waits for this frame's budget
    Texture* get(Kind kind, int variant, const Vec3& color);
    void beginFrame() {
        renderBudget_ = 1;
        uploadBudget_ = 12;
    }
    void clear();

private:
    struct Shade {
        std::vector<Vec3> a, b;  // premultiplied by coverage
        std::vector<float> alpha;
    };
    const Shade* shade(Kind kind, int variant);
    Shade render(Kind kind, int variant) const;

    std::map<int, Shade> shades_;
    std::map<std::string, TexturePtr> textures_;
    int renderBudget_ = 1, uploadBudget_ = 12;
};

}  // namespace sw
