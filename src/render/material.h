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
};

MaterialUniforms materialUniforms(const Material& m);

}  // namespace sw
