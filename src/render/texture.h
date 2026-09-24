// scoot would - texture loading (stb_image) with GPU mip generation
#pragma once

#include "render/gpu.h"

#include <memory>
#include <string>
#include <vector>

namespace sw {

struct Texture {
    std::string path;
    GpuTexture gpuTex;
    bool srgb = true;
    bool isHdr = false;
    ~Texture();
};
using TexturePtr = std::shared_ptr<Texture>;

enum class TextureQuality { Low = 0, Medium, High, Ultra };

struct TextureLoadOptions {
    bool srgb = true;
    bool mips = true;
    int maxSize = 2048;
};

// load LDR image (png/jpg/tga/bmp) or HDR (.hdr) from absolute path
TexturePtr loadTexture(const std::string& absPath, const TextureLoadOptions& opt);
// create from raw RGBA8 pixels
TexturePtr createTextureRGBA8(const std::string& name, int w, int h, const uint8_t* rgba, bool srgb, bool mips);
TexturePtr createTextureRGBA16F(const std::string& name, int w, int h, const uint16_t* rgba);

// CPU side float image (for IBL preprocessing)
struct ImageF {
    int width = 0, height = 0;
    std::vector<float> rgb;  // 3 floats per pixel
};
bool loadHdrImage(const std::string& absPath, ImageF& out, int maxWidth = 2048);

uint16_t floatToHalf(float f);
float halfToFloat(uint16_t h);
// upload level 0 and a CPU generated mip chain (texture created with tex.mips levels)
void uploadMipChainRGBA16F(const GpuTexture& tex, const uint16_t* rgba, int w, int h);

}  // namespace sw
