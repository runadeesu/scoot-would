#include "render/gpu.h"
#include "core/log.h"

#include <cstring>

namespace sw {

Gpu& gpu() {
    static Gpu g;
    return g;
}

bool Gpu::init(SDL_Window* window, const std::string& preferredDriver, bool vsync, bool debug) {
    window_ = window;
    vsync_ = vsync;

    // Our shaders are authored in GLSL and delivered as SPIR-V (Vulkan) or as DXBC
    // (D3D12, produced at runtime by SPIRV-Cross + d3dcompiler_47).
    auto tryCreate = [&](const char* name) -> SDL_GPUDevice* {
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, debug);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXBC_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN, false);
        if (name) SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, name);
        SDL_GPUDevice* d = SDL_CreateGPUDeviceWithProperties(props);
        SDL_DestroyProperties(props);
        if (!d) LOG_WARN("gpu: could not create '%s' device: %s", name ? name : "default", SDL_GetError());
        return d;
    };

    std::vector<std::string> order;
    if (!preferredDriver.empty() && preferredDriver != "auto") order.push_back(preferredDriver);
#if defined(_WIN32)
    order.push_back("direct3d12");
    order.push_back("vulkan");
#else
    order.push_back("vulkan");
#endif
    for (const std::string& name : order) {
        device_ = tryCreate(name.c_str());
        if (device_) break;
    }
    if (!device_) device_ = tryCreate(nullptr);
    if (!device_) {
        LOG_CRITICAL("gpu: no supported GPU device (Direct3D 12 or Vulkan required)");
        return false;
    }
    driverName_ = SDL_GetGPUDeviceDriver(device_);
    formats_ = SDL_GetGPUShaderFormats(device_);
    LOG_INFO("gpu: using backend '%s' (shader formats 0x%x)", driverName_.c_str(), formats_);

    if (!SDL_ClaimWindowForGPUDevice(device_, window_)) {
        LOG_CRITICAL("gpu: cannot claim window: %s", SDL_GetError());
        return false;
    }
    setPresentMode(vsync_);
    createSamplers();
    return true;
}

void Gpu::shutdown() {
    if (!device_) return;
    flushUploads();
    SDL_WaitForGPUIdle(device_);
    for (auto*& s : samplers_) {
        if (s) SDL_ReleaseGPUSampler(device_, s);
        s = nullptr;
    }
    SDL_ReleaseWindowFromGPUDevice(device_, window_);
    SDL_DestroyGPUDevice(device_);
    device_ = nullptr;
}

SDL_GPUTextureFormat Gpu::swapchainFormat() const { return SDL_GetGPUSwapchainTextureFormat(device_, window_); }

void Gpu::setPresentMode(bool vsync) {
    vsync_ = vsync;
    SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
    if (!vsync) {
        if (SDL_WindowSupportsGPUPresentMode(device_, window_, SDL_GPU_PRESENTMODE_MAILBOX))
            mode = SDL_GPU_PRESENTMODE_MAILBOX;
        else if (SDL_WindowSupportsGPUPresentMode(device_, window_, SDL_GPU_PRESENTMODE_IMMEDIATE))
            mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
    }
    if (!SDL_SetGPUSwapchainParameters(device_, window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode))
        LOG_WARN("gpu: SDL_SetGPUSwapchainParameters failed: %s", SDL_GetError());
    // allow up to 2 frames in flight for latency / throughput balance
    SDL_SetGPUAllowedFramesInFlight(device_, 2);
}

void Gpu::createSamplers() {
    for (auto*& s : samplers_) {
        if (s) SDL_ReleaseGPUSampler(device_, s);
        s = nullptr;
    }
    SDL_GPUSamplerCreateInfo ci{};
    ci.min_filter = SDL_GPU_FILTER_LINEAR;
    ci.mag_filter = SDL_GPU_FILTER_LINEAR;
    ci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    ci.address_mode_u = ci.address_mode_v = ci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    ci.max_lod = 1000.0f;
    samplers_[int(SamplerKind::LinearRepeat)] = SDL_CreateGPUSampler(device_, &ci);

    ci.address_mode_u = ci.address_mode_v = ci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplers_[int(SamplerKind::LinearClamp)] = SDL_CreateGPUSampler(device_, &ci);

    SDL_GPUSamplerCreateInfo pc = ci;
    pc.min_filter = pc.mag_filter = SDL_GPU_FILTER_NEAREST;
    pc.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplers_[int(SamplerKind::PointClamp)] = SDL_CreateGPUSampler(device_, &pc);
    pc.address_mode_u = pc.address_mode_v = pc.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplers_[int(SamplerKind::PointRepeat)] = SDL_CreateGPUSampler(device_, &pc);

    SDL_GPUSamplerCreateInfo an = ci;
    an.address_mode_u = an.address_mode_v = an.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    an.enable_anisotropy = anisotropy_ > 1.0f;
    an.max_anisotropy = anisotropy_;
    samplers_[int(SamplerKind::AnisoRepeat)] = SDL_CreateGPUSampler(device_, &an);

    SDL_GPUSamplerCreateInfo sh = ci;
    sh.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sh.enable_compare = true;
    sh.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    sh.max_lod = 0.0f;
    samplers_[int(SamplerKind::ShadowCompare)] = SDL_CreateGPUSampler(device_, &sh);

    for (int i = 0; i < int(SamplerKind::Count); ++i)
        if (!samplers_[i]) LOG_ERROR("gpu: sampler %d creation failed: %s", i, SDL_GetError());
}

void Gpu::setAnisotropy(float aniso) {
    if (aniso == anisotropy_) return;
    anisotropy_ = aniso;
    waitIdle();
    createSamplers();
}

GpuBuffer Gpu::createBuffer(SDL_GPUBufferUsageFlags usage, uint32_t size, const char* name) {
    GpuBuffer b;
    SDL_GPUBufferCreateInfo ci{};
    ci.usage = usage;
    ci.size = size > 0 ? size : 16;
    SDL_PropertiesID props = 0;
    if (name) {
        props = SDL_CreateProperties();
        SDL_SetStringProperty(props, SDL_PROP_GPU_BUFFER_CREATE_NAME_STRING, name);
        ci.props = props;
    }
    b.handle = SDL_CreateGPUBuffer(device_, &ci);
    if (props) SDL_DestroyProperties(props);
    if (!b.handle) LOG_ERROR("gpu: buffer creation failed (%u bytes): %s", size, SDL_GetError());
    b.size = ci.size;
    b.usage = usage;
    return b;
}

GpuBuffer Gpu::createBufferWithData(SDL_GPUBufferUsageFlags usage, const void* data, uint32_t size, const char* name) {
    GpuBuffer b = createBuffer(usage, size, name);
    if (b.handle && data && size) uploadBuffer(b, data, size);
    return b;
}

SDL_GPUCommandBuffer* Gpu::uploadCommandBuffer() {
    if (!uploadCmd_) {
        uploadCmd_ = SDL_AcquireGPUCommandBuffer(device_);
        uploadPass_ = SDL_BeginGPUCopyPass(uploadCmd_);
    }
    return uploadCmd_;
}

void Gpu::uploadBuffer(const GpuBuffer& buf, const void* data, uint32_t size, uint32_t offset, bool cycle) {
    if (!buf.handle || !size) return;
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device_, &ti);
    if (!tb) {
        LOG_ERROR("gpu: transfer buffer failed: %s", SDL_GetError());
        return;
    }
    void* map = SDL_MapGPUTransferBuffer(device_, tb, false);
    std::memcpy(map, data, size);
    SDL_UnmapGPUTransferBuffer(device_, tb);
    uploadCommandBuffer();
    SDL_GPUTransferBufferLocation src{tb, 0};
    SDL_GPUBufferRegion dst{buf.handle, offset, size};
    SDL_UploadToGPUBuffer(uploadPass_, &src, &dst, cycle);
    pendingTransfers_.push_back(tb);
}

void Gpu::release(GpuBuffer& b) {
    if (b.handle) SDL_ReleaseGPUBuffer(device_, b.handle);
    b = GpuBuffer{};
}

GpuTexture Gpu::createTexture(const SDL_GPUTextureCreateInfo& info, const char* name) {
    SDL_GPUTextureCreateInfo ci = info;
    SDL_PropertiesID props = 0;
    if (name) {
        props = SDL_CreateProperties();
        SDL_SetStringProperty(props, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, name);
        ci.props = props;
    }
    GpuTexture t;
    t.handle = SDL_CreateGPUTexture(device_, &ci);
    if (props) SDL_DestroyProperties(props);
    if (!t.handle) {
        LOG_ERROR("gpu: texture creation failed (%s %ux%u fmt %d): %s", name ? name : "", info.width, info.height,
                  int(info.format), SDL_GetError());
        return t;
    }
    t.width = info.width;
    t.height = info.height;
    t.depth = info.type == SDL_GPU_TEXTURETYPE_3D ? info.layer_count_or_depth : 1;
    t.layers = info.type == SDL_GPU_TEXTURETYPE_3D ? 1 : info.layer_count_or_depth;
    t.mips = info.num_levels;
    t.format = info.format;
    t.type = info.type;
    return t;
}

GpuTexture Gpu::createTexture2D(uint32_t w, uint32_t h, SDL_GPUTextureFormat fmt, SDL_GPUTextureUsageFlags usage,
                                uint32_t mips, const char* name) {
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = fmt;
    ci.usage = usage;
    ci.width = w;
    ci.height = h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = mips;
    ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    return createTexture(ci, name);
}

void Gpu::uploadTexture(const GpuTexture& tex, const void* data, uint32_t dataSize, uint32_t mip, uint32_t layer,
                        uint32_t w, uint32_t h) {
    if (!tex.handle || !data) return;
    if (w == 0) w = std::max(1u, tex.width >> mip);
    if (h == 0) h = std::max(1u, tex.height >> mip);
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = dataSize;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device_, &ti);
    if (!tb) {
        LOG_ERROR("gpu: texture transfer buffer failed: %s", SDL_GetError());
        return;
    }
    void* map = SDL_MapGPUTransferBuffer(device_, tb, false);
    std::memcpy(map, data, dataSize);
    SDL_UnmapGPUTransferBuffer(device_, tb);
    uploadCommandBuffer();
    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = tb;
    src.offset = 0;
    SDL_GPUTextureRegion dst{};
    dst.texture = tex.handle;
    dst.mip_level = mip;
    dst.layer = layer;
    dst.w = w;
    dst.h = h;
    dst.d = 1;
    SDL_UploadToGPUTexture(uploadPass_, &src, &dst, false);
    pendingTransfers_.push_back(tb);
}

void Gpu::generateMips(const GpuTexture& tex) {
    if (!tex.handle || tex.mips <= 1) return;
    // mip generation happens outside of a copy pass
    SDL_GPUCommandBuffer* cmd = uploadCommandBuffer();
    SDL_EndGPUCopyPass(uploadPass_);
    SDL_GenerateMipmapsForGPUTexture(cmd, tex.handle);
    uploadPass_ = SDL_BeginGPUCopyPass(cmd);
}

void Gpu::release(GpuTexture& t) {
    if (t.handle) SDL_ReleaseGPUTexture(device_, t.handle);
    t = GpuTexture{};
}

void Gpu::flushUploads() {
    if (!uploadCmd_) return;
    SDL_EndGPUCopyPass(uploadPass_);
    if (!SDL_SubmitGPUCommandBuffer(uploadCmd_)) LOG_ERROR("gpu: upload submit failed: %s", SDL_GetError());
    uploadCmd_ = nullptr;
    uploadPass_ = nullptr;
    // transfer buffers can be released right away; SDL keeps them alive until the GPU is done
    for (auto* tb : pendingTransfers_) SDL_ReleaseGPUTransferBuffer(device_, tb);
    pendingTransfers_.clear();
}

SDL_GPUCommandBuffer* Gpu::beginFrame() {
    flushUploads();
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd) LOG_ERROR("gpu: acquire command buffer failed: %s", SDL_GetError());
    return cmd;
}

SDL_GPUTexture* Gpu::acquireSwapchain(SDL_GPUCommandBuffer* cmd, uint32_t* w, uint32_t* h) {
    SDL_GPUTexture* tex = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_, &tex, w, h)) {
        LOG_ERROR("gpu: swapchain acquire failed: %s", SDL_GetError());
        return nullptr;
    }
    return tex;
}

void Gpu::endFrame(SDL_GPUCommandBuffer* cmd) {
    if (!SDL_SubmitGPUCommandBuffer(cmd)) LOG_ERROR("gpu: submit failed: %s", SDL_GetError());
}

void Gpu::waitIdle() {
    flushUploads();
    SDL_WaitForGPUIdle(device_);
}

bool Gpu::supports(SDL_GPUTextureFormat fmt, SDL_GPUTextureType type, SDL_GPUTextureUsageFlags usage) const {
    return SDL_GPUTextureSupportsFormat(device_, fmt, type, usage);
}

uint32_t formatBytesPerPixel(SDL_GPUTextureFormat fmt) {
    switch (fmt) {
        case SDL_GPU_TEXTUREFORMAT_R8_UNORM: return 1;
        case SDL_GPU_TEXTUREFORMAT_R8G8_UNORM: return 2;
        case SDL_GPU_TEXTUREFORMAT_R16_FLOAT: return 2;
        case SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT: return 4;
        case SDL_GPU_TEXTUREFORMAT_R32_FLOAT: return 4;
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB:
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
        case SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT:
        case SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM: return 4;
        case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT: return 8;
        case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT: return 16;
        default: return 4;
    }
}

bool formatIsBlockCompressed(SDL_GPUTextureFormat fmt) {
    return fmt >= SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM && fmt <= SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM;
}

}  // namespace sw
