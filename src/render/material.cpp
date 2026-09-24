#include "render/material.h"

namespace sw {

MaterialUniforms materialUniforms(const Material& m) {
    MaterialUniforms u;
    u.baseColorFactor = m.baseColorFactor;
    u.emissive = Vec4(m.emissiveFactor * m.emissiveStrength, 0.0f);
    u.params = Vec4(m.metallic, m.roughness, m.normalScale, m.aoStrength);
    u.params2 = Vec4(m.uvScale, m.alphaMode == AlphaMode::Mask ? m.alphaCutoff : -1.0f, m.wear, m.tintable ? 1.0f : 0.0f);
    return u;
}

}  // namespace sw
