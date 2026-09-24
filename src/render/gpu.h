// scoot would - GPU device wrapper around the SDL3 GPU API.
// Windows uses Direct3D 12 by default (DXBC compiled at runtime from our GLSL via
// SPIR-V -> HLSL), Vulkan is the fallback / Linux backend.
#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <string>
#include <vector>
#include <cstdint>

namespace sw {

struct GpuBuffer {
    SDL_GPUBuffer* handle = nullptr;
    uint32_t size = 0;
    SDL_GPUBufferUsageFlags usage = 0;
};

struct GpuTexture {
    SDL_GPUTexture* handle = nullptr;
    uint32_t width = 0, height = 0, depth = 1, layers = 1, mips = 1;
    SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUTextureType type = SDL_GPU_TEXTURETYPE_2D;
    explicit operator bool() const { return handle != nullptr; }
};

enum class SamplerKind {
    LinearRepeat = 0,
    LinearClamp,
    PointClamp,
    AnisoRepeat,
    ShadowCompare,
    PointRepeat,
    Count
};

class Gpu {
public:
    bool init(SDL_Window* window, const std::string& preferredDriver, bool vsync, bool debug);
    void shutdown();

    SDL_GPUDevice* device() const { return device_; }
    SDL_Window* window() const { return window_; }
    const std::string& driverName() const { return driverName_; }
    bool isD3D12() const { return driverName_ == "direct3d12"; }
    SDL_GPUShaderFormat shaderFormats() const { return formats_; }
    SDL_GPUTextureFormat swapchainFormat() const;
    void setPresentMode(bool vsync);
    bool vsync() const { return vsync_; }

    // resources ------------------------------------------------------------
    GpuBuffer createBuffer(SDL_GPUBufferUsageFlags usage, uint32_t size, const char* name = nullptr);
    // create + upload immediately (blocking-free, uses a copy pass on an internal command buffer)
    GpuBuffer createBufferWithData(SDL_GPUBufferUsageFlags usage, const void* data, uint32_t size, const char* name = nullptr);
    void uploadBuffer(const GpuBuffer& buf, const void* data, uint32_t size, uint32_t offset = 0, bool cycle = false);
    void release(GpuBuffer& b);

    GpuTexture createTexture(const SDL_GPUTextureCreateInfo& info, const char* name = nullptr);
    GpuTexture createTexture2D(uint32_t w, uint32_t h, SDL_GPUTextureFormat fmt, SDL_GPUTextureUsageFlags usage,
                               uint32_t mips = 1, const char* name = nullptr);
    // upload one mip/layer. rowPitch in texels (0 = tightly packed)
    void uploadTexture(const GpuTexture& tex, const void* data, uint32_t dataSize, uint32_t mip = 0, uint32_t layer = 0,
                       uint32_t w = 0, uint32_t h = 0);
    void generateMips(const GpuTexture& tex);
    void release(GpuTexture& t);

    SDL_GPUSampler* sampler(SamplerKind k) const { return samplers_[int(k)]; }
    void setAnisotropy(float aniso);

    // uploads are recorded into a dedicated command buffer, submitted before the frame
    SDL_GPUCommandBuffer* uploadCommandBuffer();
    void flushUploads();

    // frame
    SDL_GPUCommandBuffer* beginFrame();
    // acquire swapchain; returns nullptr if minimized
    SDL_GPUTexture* acquireSwapchain(SDL_GPUCommandBuffer* cmd, uint32_t* w, uint32_t* h);
    void endFrame(SDL_GPUCommandBuffer* cmd);
    void waitIdle();

    // whether texture format supports given usage
    bool supports(SDL_GPUTextureFormat fmt, SDL_GPUTextureType type, SDL_GPUTextureUsageFlags usage) const;

private:
    void createSamplers();

    SDL_GPUDevice* device_ = nullptr;
    SDL_Window* window_ = nullptr;
    std::string driverName_;
    SDL_GPUShaderFormat formats_ = 0;
    bool vsync_ = true;
    float anisotropy_ = 8.0f;
    SDL_GPUSampler* samplers_[int(SamplerKind::Count)] = {};
    SDL_GPUCommandBuffer* uploadCmd_ = nullptr;
    SDL_GPUCopyPass* uploadPass_ = nullptr;
    std::vector<SDL_GPUTransferBuffer*> pendingTransfers_;
};

Gpu& gpu();

uint32_t formatBytesPerPixel(SDL_GPUTextureFormat fmt);
bool formatIsBlockCompressed(SDL_GPUTextureFormat fmt);

}  // namespace sw
