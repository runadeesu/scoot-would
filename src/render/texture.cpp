#include "render/texture.h"
#include "core/filesystem.h"
#include "core/log.h"

#include <stb_image.h>

#include <cmath>
#include <cstring>

namespace sw {

Texture::~Texture() {
    if (gpuTex.handle && gpu().device()) gpu().release(gpuTex);
}

uint16_t floatToHalf(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000;
    int32_t exp = int32_t((x >> 23) & 0xff) - 127 + 15;
    uint32_t mant = x & 0x7fffff;
    if (exp <= 0) return uint16_t(sign);
    if (exp >= 31) return uint16_t(sign | 0x7c00);
    return uint16_t(sign | (uint32_t(exp) << 10) | (mant >> 13));
}

float halfToFloat(uint16_t h) {
    uint32_t sign = uint32_t(h & 0x8000) << 16;
    uint32_t exp = (h >> 10) & 0x1f;
    uint32_t mant = h & 0x3ff;
    uint32_t x;
    if (exp == 0) x = sign;  // flush denormals
    else if (exp == 31) x = sign | 0x7f800000 | (mant << 13);
    else x = sign | ((exp - 15 + 127) << 23) | (mant << 13);
    float f;
    std::memcpy(&f, &x, 4);
    return f;
}

// sRGB <-> linear for correct mip filtering of color textures
static const float* srgbToLinearLut() {
    static float lut[256];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; ++i) {
            float c = float(i) / 255.0f;
            lut[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        init = true;
    }
    return lut;
}
static uint8_t linearToSrgb8(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    float c = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return uint8_t(std::lround(c * 255.0f));
}

// next mip level (2x2 box filter, odd sizes clamp) of an RGBA8 image
static std::vector<uint8_t> downsampleRGBA8(const std::vector<uint8_t>& src, int w, int h, int ow, int oh, bool srgb) {
    std::vector<uint8_t> out(size_t(ow) * size_t(oh) * 4);
    const float* lut = srgbToLinearLut();
    for (int y = 0; y < oh; ++y)
        for (int x = 0; x < ow; ++x) {
            int x0 = std::min(x * 2, w - 1), x1 = std::min(x * 2 + 1, w - 1), y0 = std::min(y * 2, h - 1), y1 = std::min(y * 2 + 1, h - 1);
            const uint8_t* p[4] = {&src[(size_t(y0) * w + x0) * 4], &src[(size_t(y0) * w + x1) * 4], &src[(size_t(y1) * w + x0) * 4],
                                   &src[(size_t(y1) * w + x1) * 4]};
            uint8_t* o = &out[(size_t(y) * ow + x) * 4];
            for (int c = 0; c < 4; ++c) {
                if (srgb && c < 3) {
                    float sum = lut[p[0][c]] + lut[p[1][c]] + lut[p[2][c]] + lut[p[3][c]];
                    o[c] = linearToSrgb8(sum * 0.25f);
                } else {
                    o[c] = uint8_t((int(p[0][c]) + p[1][c] + p[2][c] + p[3][c] + 2) / 4);
                }
            }
        }
    return out;
}

static std::vector<uint16_t> downsampleRGBA16F(const std::vector<uint16_t>& src, int w, int h, int ow, int oh) {
    std::vector<uint16_t> out(size_t(ow) * size_t(oh) * 4);
    for (int y = 0; y < oh; ++y)
        for (int x = 0; x < ow; ++x) {
            int x0 = std::min(x * 2, w - 1), x1 = std::min(x * 2 + 1, w - 1), y0 = std::min(y * 2, h - 1), y1 = std::min(y * 2 + 1, h - 1);
            for (int c = 0; c < 4; ++c) {
                float sum = halfToFloat(src[(size_t(y0) * w + x0) * 4 + c]) + halfToFloat(src[(size_t(y0) * w + x1) * 4 + c]) +
                            halfToFloat(src[(size_t(y1) * w + x0) * 4 + c]) + halfToFloat(src[(size_t(y1) * w + x1) * 4 + c]);
                out[(size_t(y) * ow + x) * 4 + c] = floatToHalf(sum * 0.25f);
            }
        }
    return out;
}

void uploadMipChainRGBA16F(const GpuTexture& tex, const uint16_t* rgba, int w, int h) {
    std::vector<uint16_t> level(rgba, rgba + size_t(w) * size_t(h) * 4);
    gpu().uploadTexture(tex, level.data(), uint32_t(level.size() * 2), 0, 0);
    for (uint32_t m = 1; m < tex.mips; ++m) {
        int ow = std::max(1, w >> 1), oh = std::max(1, h >> 1);
        level = downsampleRGBA16F(level, w, h, ow, oh);
        w = ow;
        h = oh;
        gpu().uploadTexture(tex, level.data(), uint32_t(level.size() * 2), m, 0);
    }
}

static uint32_t mipCount(int w, int h) {
    uint32_t n = 1;
    int s = std::max(w, h);
    while (s > 1) {
        s >>= 1;
        ++n;
    }
    return n;
}

// box downscale RGBA8 by 2 (used when texture quality is reduced)
static std::vector<uint8_t> halve(const uint8_t* src, int w, int h, int& ow, int& oh) {
    ow = std::max(1, w / 2);
    oh = std::max(1, h / 2);
    std::vector<uint8_t> out(size_t(ow) * oh * 4);
    for (int y = 0; y < oh; ++y)
        for (int x = 0; x < ow; ++x)
            for (int c = 0; c < 4; ++c) {
                int sx = std::min(x * 2, w - 1), sy = std::min(y * 2, h - 1);
                int sx1 = std::min(sx + 1, w - 1), sy1 = std::min(sy + 1, h - 1);
                int sum = src[(sy * w + sx) * 4 + c] + src[(sy * w + sx1) * 4 + c] + src[(sy1 * w + sx) * 4 + c] +
                          src[(sy1 * w + sx1) * 4 + c];
                out[(y * ow + x) * 4 + c] = uint8_t((sum + 2) / 4);
            }
    return out;
}

TexturePtr createTextureRGBA8(const std::string& name, int w, int h, const uint8_t* rgba, bool srgb, bool mips) {
    auto tex = std::make_shared<Texture>();
    tex->path = name;
    tex->srgb = srgb;
    SDL_GPUTextureFormat fmt = srgb ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    uint32_t levels = mips ? mipCount(w, h) : 1;
    tex->gpuTex = gpu().createTexture2D(uint32_t(w), uint32_t(h), fmt, SDL_GPU_TEXTUREUSAGE_SAMPLER, levels, name.c_str());
    if (!tex->gpuTex) return nullptr;
    // mip chain on the CPU (gamma correct for sRGB): works on every backend without blit pipelines
    std::vector<uint8_t> level(rgba, rgba + size_t(w) * size_t(h) * 4);
    gpu().uploadTexture(tex->gpuTex, level.data(), uint32_t(level.size()), 0, 0);
    int lw = w, lh = h;
    for (uint32_t m = 1; m < levels; ++m) {
        int ow = std::max(1, lw >> 1), oh = std::max(1, lh >> 1);
        level = downsampleRGBA8(level, lw, lh, ow, oh, srgb);
        lw = ow;
        lh = oh;
        gpu().uploadTexture(tex->gpuTex, level.data(), uint32_t(level.size()), m, 0);
    }
    return tex;
}

TexturePtr createTextureRGBA16F(const std::string& name, int w, int h, const uint16_t* rgba) {
    auto tex = std::make_shared<Texture>();
    tex->path = name;
    tex->srgb = false;
    tex->isHdr = true;
    uint32_t levels = mipCount(w, h);
    tex->gpuTex = gpu().createTexture2D(uint32_t(w), uint32_t(h), SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, SDL_GPU_TEXTUREUSAGE_SAMPLER, levels,
                                        name.c_str());
    if (!tex->gpuTex) return nullptr;
    uploadMipChainRGBA16F(tex->gpuTex, rgba, w, h);
    return tex;
}

TexturePtr loadTexture(const std::string& absPath, const TextureLoadOptions& opt) {
    auto bytes = fs::readBinary(absPath);
    if (!bytes) {
        LOG_ERROR("texture: missing %s", absPath.c_str());
        return nullptr;
    }
    if (stbi_is_hdr_from_memory(bytes->data(), int(bytes->size()))) {
        int w, h, c;
        float* px = stbi_loadf_from_memory(bytes->data(), int(bytes->size()), &w, &h, &c, 4);
        if (!px) {
            LOG_ERROR("texture: cannot decode %s: %s", absPath.c_str(), stbi_failure_reason());
            return nullptr;
        }
        std::vector<uint16_t> half(size_t(w) * h * 4);
        for (size_t i = 0; i < half.size(); ++i) half[i] = floatToHalf(px[i]);
        stbi_image_free(px);
        auto t = createTextureRGBA16F(absPath, w, h, half.data());
        if (t) t->path = absPath;
        return t;
    }
    int w, h, c;
    stbi_uc* px = stbi_load_from_memory(bytes->data(), int(bytes->size()), &w, &h, &c, 4);
    if (!px) {
        LOG_ERROR("texture: cannot decode %s: %s", absPath.c_str(), stbi_failure_reason());
        return nullptr;
    }
    std::vector<uint8_t> data(px, px + size_t(w) * h * 4);
    stbi_image_free(px);
    while (std::max(w, h) > opt.maxSize && std::max(w, h) > 4) {
        int nw, nh;
        data = halve(data.data(), w, h, nw, nh);
        w = nw;
        h = nh;
    }
    auto t = createTextureRGBA8(absPath, w, h, data.data(), opt.srgb, opt.mips);
    return t;
}

bool loadHdrImage(const std::string& absPath, ImageF& out, int maxWidth) {
    auto bytes = fs::readBinary(absPath);
    if (!bytes) return false;
    int w, h, c;
    float* px = stbi_loadf_from_memory(bytes->data(), int(bytes->size()), &w, &h, &c, 3);
    if (!px) {
        LOG_ERROR("texture: cannot decode HDR %s: %s", absPath.c_str(), stbi_failure_reason());
        return false;
    }
    out.width = w;
    out.height = h;
    out.rgb.assign(px, px + size_t(w) * h * 3);
    stbi_image_free(px);
    while (out.width > maxWidth) {
        int nw = out.width / 2, nh = out.height / 2;
        std::vector<float> d(size_t(nw) * nh * 3);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
                for (int k = 0; k < 3; ++k) {
                    auto at = [&](int xx, int yy) { return out.rgb[(size_t(yy) * out.width + xx) * 3 + k]; };
                    d[(size_t(y) * nw + x) * 3 + k] = 0.25f * (at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1));
                }
        out.rgb = std::move(d);
        out.width = nw;
        out.height = nh;
    }
    return true;
}

}  // namespace sw
