// scoot would - PBR material (glTF metallic-roughness compatible), data driven from JSON
#pragma once

#include "core/math.h"
#include "render/texture.h"

#include <memory>
#include <string>

namespace sw {

enum class AlphaMode { Opaque = 0, Mask, Blend };

struct Material {
    std::string name;
    Vec4 baseColorFactor{1, 1, 1, 1};
    Vec3 emissiveFactor{0, 0, 0};
    float emissiveStrength = 1.0f;
    float metallic = 0.0f;
    float roughness = 0.8f;
    float normalScale = 1.0f;
    float aoStrength = 1.0f;
    float uvScale = 1.0f;
    float alphaCutoff = 0.5f;
    float wear = 0.0f;         // procedural grime / edge dirt amount
    float specularBoost = 1.0f;
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;
    bool tintable = false;     // instance tint color replaces base color factor (customization)
    bool decal = false;        // blended surface detail laid onto other geometry (drawn after opaque)
    // shading model: 0 standard, 1 skin (wrap + subsurface tint), 2 cloth (sheen), 3 interior mapped glass
    int shading = 0;
    float roomDepth = 3.2f, interiorLight = 0.6f;
    bool shop = false;
    bool antiTile = false;       // break visible texture repetition (two offset samples blended by noise)
    float detailScale = 0.0f;    // metres per repeat of the tiling detail normal (0 = off)
    float detailStrength = 0.5f;
    TexturePtr detail;           // detail normal (default: shared micro detail)
    TexturePtr baseColor, normal, orm, emissive;
    std::string surface = "concrete";  // physics + audio surface type
    uint32_t id = 0;
    std::string sourcePath;
};
using MaterialPtr = std::shared_ptr<Material>;

// std140 layout, pushed as fragment uniform slot 1
struct MaterialUniforms {
    Vec4 baseColorFactor;
    Vec4 emissive;  // rgb * strength
    Vec4 params;    // metallic, roughness, normalScale, aoStrength
    Vec4 params2;   // uvScale, alphaCutoff, wear, tintable
    Vec4 params3;   // shading model, room depth, interior light, shop
    Vec4 params4;   // detail scale (0 = off), detail strength, anti tiling, unused
};

MaterialUniforms materialUniforms(const Material& m);

}  // namespace sw
