#include "render/renderer.h"
#include "assets/asset_manager.h"
#include "core/filesystem.h"
#include "core/log.h"
#include "core/timer.h"
#include "platform/window.h"
#include "render/debug_draw.h"
#include "render/particles.h"
#include "render/texture.h"

#include <stb_image_write.h>

#include <algorithm>
#include <cstring>

namespace sw {

Renderer& renderer() {
    static Renderer r;
    return r;
}

RenderView RenderView::lookAt(const Vec3& eye, const Vec3& target, const Vec3& upHint, float fovY, float aspect, float nearZ) {
    RenderView v;
    v.position = eye;
    v.forward = (target - eye).normalized();
    v.right = cross(v.forward, upHint).normalized();
    if (v.right.lengthSq() < 1e-6f) v.right = anyPerpendicular(v.forward);
    v.up = cross(v.right, v.forward);
    v.view = Mat4::lookAt(eye, target, upHint);
    v.fovY = fovY;
    v.aspect = aspect;
    v.nearZ = nearZ;
    v.proj = Mat4::perspectiveReverseZ(fovY, aspect, nearZ);
    v.viewProj = v.proj * v.view;
    return v;
}

RenderView RenderView::fromTransform(const Vec3& pos, const Quat& rot, float fovY, float aspect, float nearZ) {
    Vec3 f = rot * Vec3(0, 0, -1);
    Vec3 u = rot * Vec3(0, 1, 0);
    return lookAt(pos, pos + f, u, fovY, aspect, nearZ);
}

namespace {

constexpr SDL_GPUTextureFormat kDepthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
constexpr SDL_GPUTextureFormat kHdrFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
constexpr SDL_GPUTextureFormat kNormalFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;  // oct normal + motion
constexpr SDL_GPUTextureFormat kAoFormat = SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;  // ambient occlusion, contact shadow
constexpr SDL_GPUTextureFormat kLdrFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
constexpr int kMaxLights = 64;
constexpr int kBloomLevels = 6;
constexpr int kEnvSize = 128;

struct LightGpu {
    Vec4 posRadius, colorIntensity, dirType, spot;
};

VertexLayout meshLayout(bool skinned) {
    VertexLayout l;
    l.buffers.push_back({0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0});
    l.attributes.push_back({0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0});
    l.attributes.push_back({1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12});
    l.attributes.push_back({2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 24});
    l.attributes.push_back({3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 40});
    if (skinned) {
        l.buffers.push_back({1, sizeof(SkinVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0});
        l.attributes.push_back({4, 1, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, 0});
        l.attributes.push_back({5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 4});
    }
    return l;
}

float luminance(const float* c) { return c[0] * 0.2126f + c[1] * 0.7152f + c[2] * 0.0722f; }

}  // namespace

// ---------------------------------------------------------------------------
bool Renderer::init(Window& window) {
    window_ = &window;
    int w, h;
    window.pixelSize(w, h);
    applySettings(settings_);
    createTargets(std::max(w, 64), std::max(h, 64));
    createPipelines();
    generateBrdfLut();
    // small placeholder buffers so every binding is always valid
    ensureDyn(boneBuf_, 64 * sizeof(Mat4));
    ensureDyn(lightBuf_, kMaxLights * sizeof(LightGpu));
    ensureDyn(visibleBuf_, 4096 * 4);
    return true;
}

void Renderer::shutdown() {
    gpu().waitIdle();
    detailNormal_.reset();
    releaseTargets();
    auto rel = [](DynBuffer& d) {
        gpu().release(d.buffer);
        if (d.transfer) SDL_ReleaseGPUTransferBuffer(gpu().device(), d.transfer);
        d = DynBuffer{};
    };
    rel(instanceBuf_);
    rel(visibleBuf_);
    rel(boneBuf_);
    rel(lightBuf_);
    rel(lineBuf_);
    rel(particleBuf_);
    rel(uiVertBuf_);
    rel(uiIndexBuf_);
    gpu().release(shadowMap_);
    gpu().release(envEquirect_);
    gpu().release(envCube_);
    for (auto& e : envCache_) {
        gpu().release(e.equirect);
        gpu().release(e.cube);
    }
    envCache_.clear();
    for (auto& b : backdrop_) gpu().release(b);
    gpu().release(brdfLut_);
}

SDL_GPUTextureFormat Renderer::backbufferFormat() const { return gpu().swapchainFormat(); }

void Renderer::applySettings(const RenderSettings& s) {
    bool scaleChanged = s.renderScale != settings_.renderScale;
    settings_ = s;
    settings_.renderScale = clampf(s.renderScale, 0.5f, 1.0f);
    gpu().setAnisotropy(settings_.anisotropy);
    int size = 0, layers = 0;
    switch (settings_.shadowQuality) {
        case 0: break;
        case 1: size = 1024; layers = 2; break;
        case 2: size = 2048; layers = 3; break;
        case 3: size = 2048; layers = 4; break;
        default: size = 4096; layers = 4; break;
    }
    if (size != shadowSize_ || layers != shadowLayers_) {
        gpu().waitIdle();
        gpu().release(shadowMap_);
        shadowSize_ = size;
        shadowLayers_ = layers;
        if (size > 0) {
            SDL_GPUTextureCreateInfo ci{};
            ci.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
            ci.format = kDepthFormat;
            ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
            ci.width = ci.height = uint32_t(size);
            ci.layer_count_or_depth = uint32_t(layers);
            ci.num_levels = 1;
            shadowMap_ = gpu().createTexture(ci, "shadow cascades");
        } else {
            // 1x1 dummy so the sampler binding stays valid
            SDL_GPUTextureCreateInfo ci{};
            ci.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
            ci.format = kDepthFormat;
            ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
            ci.width = ci.height = 4;
            ci.layer_count_or_depth = 1;
            ci.num_levels = 1;
            shadowMap_ = gpu().createTexture(ci, "shadow dummy");
            SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu().device());
            SDL_GPUDepthStencilTargetInfo dt{};
            dt.texture = shadowMap_.handle;
            dt.clear_depth = 1.0f;
            dt.load_op = SDL_GPU_LOADOP_CLEAR;
            dt.store_op = SDL_GPU_STOREOP_STORE;
            SDL_EndGPURenderPass(SDL_BeginGPURenderPass(cmd, nullptr, 0, &dt));
            SDL_SubmitGPUCommandBuffer(cmd);
        }
    }
    if (scaleChanged && outW_ > 0) {
        gpu().waitIdle();
        createTargets(outW_, outH_);
    }
}

void Renderer::releaseTargets() {
    for (GpuTexture* t : {&depth_, &normals_, &hdr_, &hdrTemp_, &ldr_, &backbuffer_, &ssao_, &ssaoTemp_, &taaHistory_[0], &taaHistory_[1]})
        gpu().release(*t);
    for (auto& b : bloom_) gpu().release(b);
    bloom_.clear();
    for (auto& b : backdrop_) gpu().release(b);
}

void Renderer::createTargets(int w, int h) {
    releaseTargets();
    outW_ = w;
    outH_ = h;
    rw_ = std::max(64, int(float(w) * settings_.renderScale));
    rh_ = std::max(64, int(float(h) * settings_.renderScale));
    const auto rt = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    depth_ = gpu().createTexture2D(uint32_t(rw_), uint32_t(rh_), kDepthFormat,
                                   SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, 1, "depth");
    normals_ = gpu().createTexture2D(uint32_t(rw_), uint32_t(rh_), kNormalFormat, rt, 1, "normals");
    hdr_ = gpu().createTexture2D(uint32_t(rw_), uint32_t(rh_), kHdrFormat, rt, 1, "hdr");
    hdrTemp_ = gpu().createTexture2D(uint32_t(rw_), uint32_t(rh_), kHdrFormat, rt, 1, "hdr temp");
    for (auto& t : taaHistory_) t = gpu().createTexture2D(uint32_t(rw_), uint32_t(rh_), kHdrFormat, rt, 1, "taa history");
    taaValid_ = false;
    ldr_ = gpu().createTexture2D(uint32_t(rw_), uint32_t(rh_), kLdrFormat, rt, 1, "ldr");
    int hw = std::max(1, rw_ / 2), hh = std::max(1, rh_ / 2);
    ssao_ = gpu().createTexture2D(uint32_t(hw), uint32_t(hh), kAoFormat, rt, 1, "ssao");
    ssaoTemp_ = gpu().createTexture2D(uint32_t(hw), uint32_t(hh), kAoFormat, rt, 1, "ssao temp");
    for (int i = 0; i < kBloomLevels; ++i) {
        int bw = std::max(1, rw_ >> (i + 1)), bh = std::max(1, rh_ >> (i + 1));
        bloom_.push_back(gpu().createTexture2D(uint32_t(bw), uint32_t(bh), kHdrFormat, rt, 1, "bloom"));
    }
    backbuffer_ = gpu().createTexture2D(uint32_t(w), uint32_t(h), gpu().swapchainFormat(), rt, 1, "backbuffer");
    for (int i = 0; i < 3; ++i)
        backdrop_[i] = gpu().createTexture2D(uint32_t(std::max(1, w >> (i + 1))), uint32_t(std::max(1, h >> (i + 1))), kHdrFormat, rt, 1,
                                             "ui backdrop");
    hasPrev_ = false;
    LOG_INFO("renderer: output %dx%d, internal %dx%d", w, h, rw_, rh_);
}

void Renderer::createPipelines() {
    auto& lib = shaders();
    PipelineDesc base;
    base.depthFormat = kDepthFormat;

    // forward PBR
    for (int p = 0; p < PipeCount; ++p) {
        PipelineDesc d = base;
        d.vertex = "pbr.vert";
        d.fragment = "pbr.frag";
        d.colorFormats = {kHdrFormat};
        d.depthWrite = false;
        d.depthCompare = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
        d.layout = meshLayout(p == PipeSkinned);
        if (p == PipeSkinned) d.defines = "SKINNED";
        if (p == PipeDoubleSided) d.cull = SDL_GPU_CULLMODE_NONE;
        if (p == PipeBlend || p == PipeDecal) {
            d.blend = PipelineDesc::Blend::Alpha;
            d.cull = SDL_GPU_CULLMODE_NONE;
        }
        if (p == PipeDecal) d.defines = "DECAL";
        d.name = std::string("pbr") + std::to_string(p);
        pbr_[p] = lib.createPipeline(d);
        if (p == PipeBlend || p == PipeDecal) continue;
        PipelineDesc z = d;
        z.name = std::string("prepass") + std::to_string(p);
        z.fragment = "depth.frag";
        z.defines = d.defines.empty() ? "PREPASS" : d.defines + ";PREPASS";
        z.colorFormats = {kNormalFormat};
        z.depthWrite = true;
        z.depthCompare = SDL_GPU_COMPAREOP_GREATER;
        z.blend = PipelineDesc::Blend::Opaque;
        depthPipe_[p] = lib.createPipeline(z);
    }
    // shadows (normal Z: near 0, far 1)
    for (int s = 0; s < 3; ++s) {
        PipelineDesc d = base;
        d.name = std::string("shadow") + std::to_string(s);
        d.vertex = "shadow.vert";
        d.fragment = s == 2 ? "shadow.frag" : "";
        d.defines = s == 1 ? "SKINNED" : "";
        d.layout = meshLayout(s == 1);
        d.cull = SDL_GPU_CULLMODE_NONE;
        d.depthCompare = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        d.depthBias = true;
        d.depthBiasConstant = 1.5f;
        d.depthBiasSlope = 2.0f;
        d.depthClip = false;
        shadowPipe_[s] = lib.createPipeline(d);
    }
    // sky
    {
        PipelineDesc d = base;
        d.name = "sky";
        d.vertex = "sky.vert";
        d.fragment = "sky.frag";
        d.colorFormats = {kHdrFormat};
        d.cull = SDL_GPU_CULLMODE_NONE;
        d.depthWrite = false;
        d.depthCompare = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
        sky_ = lib.createPipeline(d);
    }
    // debug lines
    {
        PipelineDesc d = base;
        d.name = "debug lines";
        d.vertex = "debug_line.vert";
        d.fragment = "debug_line.frag";
        d.colorFormats = {kHdrFormat};
        d.primitive = SDL_GPU_PRIMITIVETYPE_LINELIST;
        d.cull = SDL_GPU_CULLMODE_NONE;
        d.depthWrite = false;
        d.blend = PipelineDesc::Blend::Alpha;
        d.layout.buffers.push_back({0, sizeof(DebugVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0});
        d.layout.attributes.push_back({0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0});
        d.layout.attributes.push_back({1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 12});
        debugLines_ = lib.createPipeline(d);
        d.name = "debug lines overlay";
        d.depthTest = false;
        debugLinesNoDepth_ = lib.createPipeline(d);
    }
    // particles
    {
        PipelineDesc d = base;
        d.name = "particles";
        d.vertex = "particle.vert";
        d.fragment = "particle.frag";
        d.colorFormats = {kHdrFormat};
        d.cull = SDL_GPU_CULLMODE_NONE;
        d.depthWrite = false;
        d.blend = PipelineDesc::Blend::Premultiplied;
        d.layout.buffers.push_back({0, sizeof(ParticleVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0});
        d.layout.attributes.push_back({0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0});
        d.layout.attributes.push_back({1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 12});
        d.layout.attributes.push_back({2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 20});
        particlesAlpha_ = lib.createPipeline(d);
        d.name = "particles add";
        d.blend = PipelineDesc::Blend::Additive;
        particlesAdd_ = lib.createPipeline(d);
    }
    // fullscreen passes
    auto post = [&](const char* name, const char* frag, SDL_GPUTextureFormat fmt, PipelineDesc::Blend blend = PipelineDesc::Blend::Opaque) {
        PipelineDesc d;
        d.name = name;
        d.vertex = "postprocess.vert";
        d.fragment = frag;
        d.colorFormats = {fmt};
        d.cull = SDL_GPU_CULLMODE_NONE;
        d.depthTest = false;
        d.depthWrite = false;
        d.blend = blend;
        return lib.createPipeline(d);
    };
    ssaoPipe_ = post("ssao", "ssao.frag", kAoFormat);
    ssaoBlur_ = post("ssao blur", "ssao_blur.frag", kAoFormat);
    bloomDown_ = post("bloom down", "bloom_down.frag", kHdrFormat);
    bloomUp_ = post("bloom up", "bloom_up.frag", kHdrFormat, PipelineDesc::Blend::Additive);
    motionBlur_ = post("motion blur", "motion_blur.frag", kHdrFormat);
    taa_ = post("taa", "taa.frag", kHdrFormat);
    tonemap_ = post("tonemap", "postprocess.frag", kLdrFormat);
    fxaa_ = post("fxaa", "fxaa.frag", gpu().swapchainFormat());
    present_ = post("present", "present.frag", gpu().swapchainFormat());
    prefilter_ = post("ibl prefilter", "ibl_prefilter.frag", kHdrFormat);
    brdfPipe_ = post("brdf lut", "brdf_lut.frag", SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT);
    // UI
    {
        PipelineDesc d;
        d.name = "ui";
        d.vertex = "ui.vert";
        d.fragment = "ui.frag";
        d.colorFormats = {gpu().swapchainFormat()};
        d.cull = SDL_GPU_CULLMODE_NONE;
        d.depthTest = false;
        d.depthWrite = false;
        d.blend = PipelineDesc::Blend::Premultiplied;
        d.layout.buffers.push_back({0, sizeof(UIVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0});
        d.layout.attributes.push_back({0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0});
        d.layout.attributes.push_back({1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 8});
        d.layout.attributes.push_back({2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 16});
        ui_ = lib.createPipeline(d);
    }
}

// ---------------------------------------------------------------------------
void Renderer::ensureDyn(DynBuffer& db, uint32_t size) {
    size = std::max<uint32_t>(size, 256);
    if (db.buffer.handle && db.capacity >= size) return;
    uint32_t cap = std::max(size, db.capacity * 3 / 2);
    gpu().release(db.buffer);
    if (db.transfer) SDL_ReleaseGPUTransferBuffer(gpu().device(), db.transfer);
    db.buffer = gpu().createBuffer(db.usage ? db.usage : SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, cap, db.name);
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = cap;
    db.transfer = SDL_CreateGPUTransferBuffer(gpu().device(), &ti);
    db.capacity = cap;
}

void Renderer::uploadDynamic(SDL_GPUCopyPass* pass, DynBuffer& db, const void* data, uint32_t size) {
    if (size == 0) return;
    ensureDyn(db, size);
    void* map = SDL_MapGPUTransferBuffer(gpu().device(), db.transfer, true);
    std::memcpy(map, data, size);
    SDL_UnmapGPUTransferBuffer(gpu().device(), db.transfer);
    SDL_GPUTransferBufferLocation src{db.transfer, 0};
    SDL_GPUBufferRegion dst{db.buffer.handle, 0, size};
    SDL_UploadToGPUBuffer(pass, &src, &dst, true);
}

// ---------------------------------------------------------------------------
void Renderer::fullscreen(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, uint32_t mip, GfxPipeline* pipe,
                          const std::vector<SDL_GPUTextureSamplerBinding>& samplers, const void* uniforms,
                          uint32_t uniformSize, bool clear, bool pushFrame, uint32_t layer) {
    if (!pipe || !pipe->valid()) return;
    SDL_GPUColorTargetInfo ct{};
    ct.texture = target;
    ct.mip_level = mip;
    ct.layer_or_depth_plane = layer;
    ct.load_op = clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.clear_color = {0, 0, 0, 0};
    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(rp, pipe->handle);
    if (!samplers.empty()) SDL_BindGPUFragmentSamplers(rp, 0, samplers.data(), uint32_t(samplers.size()));
    uint32_t slot = 0;
    if (pushFrame) SDL_PushGPUFragmentUniformData(cmd, slot++, &frame_, sizeof(frame_));
    if (uniforms && uniformSize) SDL_PushGPUFragmentUniformData(cmd, slot, uniforms, uniformSize);
    SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
    SDL_EndGPURenderPass(rp);
    Profiler::renderStats().drawCalls++;
}

void Renderer::generateBrdfLut() {
    brdfLut_ = gpu().createTexture2D(128, 128, SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT,
                                     SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, 1, "brdf lut");
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu().device());
    fullscreen(cmd, brdfLut_.handle, 0, brdfPipe_, {}, nullptr, 0, true);
    SDL_SubmitGPUCommandBuffer(cmd);
}

void Renderer::loadEnvironment(RenderScene& scene) {
    if (scene.environmentVersion == loadedEnvVersion_ && envCube_.handle) return;
    loadedEnvVersion_ = scene.environmentVersion;
    const Environment& env = scene.environment;
    // everything the IBL preprocessing depends on
    char keyBuf[256];
    std::snprintf(keyBuf, sizeof(keyBuf), "|%.4f|%d|%.3f,%.3f,%.3f|%.3f|%d", env.rotation, int(env.autoSun), env.sunDirection.x,
                  env.sunDirection.y, env.sunDirection.z, env.sunIntensity, int(env.groundFill));
    std::string key = env.hdri + keyBuf;
    if (key == envKey_ && envCube_.handle) return;
    if (envCube_.handle) {
        // park the current environment, it is likely needed again soon
        gpu().waitIdle();
        EnvCacheEntry e;
        e.key = envKey_;
        e.hdri = loadedHdri_;
        e.equirect = envEquirect_;
        e.cube = envCube_;
        for (int i = 0; i < 9; ++i) e.sh[i] = sh_[i];
        e.sunDir = sunDir_;
        e.skyUpLum = skyUpLum_;
        e.mips = envMips_;
        envCache_.push_back(e);
        envEquirect_ = GpuTexture{};
        envCube_ = GpuTexture{};
    }
    envKey_ = key;
    for (size_t i = 0; i < envCache_.size(); ++i) {
        if (envCache_[i].key != key) continue;
        EnvCacheEntry e = envCache_[i];
        envCache_.erase(envCache_.begin() + long(i));
        envEquirect_ = e.equirect;
        envCube_ = e.cube;
        for (int k = 0; k < 9; ++k) sh_[k] = e.sh[k];
        sunDir_ = e.sunDir;
        skyUpLum_ = e.skyUpLum;
        envMips_ = e.mips;
        loadedHdri_ = e.hdri;
        LOG_INFO("renderer: environment '%s' restored from cache", loadedHdri_.c_str());
        return;
    }
    while (envCache_.size() > 2) {
        gpu().release(envCache_.front().equirect);
        gpu().release(envCache_.front().cube);
        envCache_.erase(envCache_.begin());
    }
    Timer t;
    ImageF img;
    bool ok = !env.hdri.empty() && loadHdrImage(fs::resolve(env.hdri), img, 2048);
    if (!ok) {
        if (!env.hdri.empty()) LOG_WARN("renderer: HDRI '%s' missing, using procedural sky", env.hdri.c_str());
        img.width = 256;
        img.height = 128;
        img.rgb.resize(size_t(img.width) * img.height * 3);
        for (int y = 0; y < img.height; ++y)
            for (int x = 0; x < img.width; ++x) {
                float v = float(y) / float(img.height - 1);
                float up = 1.0f - v * 2.0f;
                Vec3 zenith(0.25f, 0.45f, 0.9f), horizon(0.75f, 0.85f, 1.0f), ground(0.25f, 0.23f, 0.2f);
                Vec3 c = up > 0 ? lerp(horizon, zenith, std::pow(up, 0.6f)) : lerp(horizon * 0.6f, ground, std::min(1.0f, -up * 4.0f));
                c *= 1.4f;
                float* px = &img.rgb[(size_t(y) * img.width + x) * 3];
                px[0] = c.x;
                px[1] = c.y;
                px[2] = c.z;
            }
    }
    loadedHdri_ = ok ? env.hdri : "procedural";

    // Pure sky HDRIs have an empty lower hemisphere. Replace it with ground bounce light
    // (ground albedo * incoming light) so reflections and the ambient term below the horizon
    // are plausible, blended smoothly at the horizon.
    if (env.groundFill) {
        double up[3] = {0, 0, 0};
        int W = img.width, H = img.height;
        int step = std::max(1, W / 256);
        for (int y = 0; y < H / 2; y += step) {
            float v = (float(y) + 0.5f) / float(H);
            float theta = v * kPi;
            float w = std::cos(theta) * std::sin(theta);
            for (int x = 0; x < W; x += step) {
                const float* p = &img.rgb[(size_t(y) * W + x) * 3];
                float col[3] = {p[0], p[1], p[2]};
                float l = luminance(col);
                if (l > 8.0f)
                    for (float& cc : col) cc *= 8.0f / l;
                for (int k = 0; k < 3; ++k) up[k] += double(col[k] * w);
            }
        }
        // integral of cos*sin dtheta dphi over the hemisphere = pi ; normalize the discrete sum
        double norm = 0;
        for (int y = 0; y < H / 2; y += step) {
            float theta = (float(y) + 0.5f) / float(H) * kPi;
            norm += double(std::cos(theta) * std::sin(theta)) * double((W + step - 1) / step);
        }
        float sunBoost = 1.0f + env.sunIntensity * std::max(env.sunDirection.normalized().y, 0.3f);
        Vec3 ground(float(up[0] / norm), float(up[1] / norm), float(up[2] / norm));
        ground = ground * (0.22f * sunBoost);
        for (int y = H / 2 - H / 64; y < H; ++y) {
            float v = (float(y) + 0.5f) / float(H);
            float t = smoothstep(0.49f, 0.53f, v);
            if (t <= 0.0f) continue;
            for (int x = 0; x < W; ++x) {
                float* p = &img.rgb[(size_t(y) * W + x) * 3];
                p[0] = lerpf(p[0], ground.x, t);
                p[1] = lerpf(p[1], ground.y, t);
                p[2] = lerpf(p[2], ground.z, t);
            }
        }
    }

    // equirect texture with mips (sky + prefilter source)
    std::vector<uint16_t> half(size_t(img.width) * img.height * 4);
    for (size_t i = 0; i < size_t(img.width) * img.height; ++i) {
        half[i * 4 + 0] = floatToHalf(img.rgb[i * 3 + 0]);
        half[i * 4 + 1] = floatToHalf(img.rgb[i * 3 + 1]);
        half[i * 4 + 2] = floatToHalf(img.rgb[i * 3 + 2]);
        half[i * 4 + 3] = floatToHalf(1.0f);
    }
    gpu().waitIdle();
    {
        uint32_t levels = 1;
        for (int s = std::max(img.width, img.height); s > 1; s >>= 1) ++levels;
        envEquirect_ = gpu().createTexture2D(uint32_t(img.width), uint32_t(img.height), kHdrFormat, SDL_GPU_TEXTUREUSAGE_SAMPLER, levels, "sky equirect");
        uploadMipChainRGBA16F(envEquirect_, half.data(), img.width, img.height);
        gpu().flushUploads();
    }

    // CPU: downsample to 256x128 for SH + sun search
    int sw_ = 256, sh_h = 128;
    std::vector<float> small(size_t(sw_) * sh_h * 3, 0.0f);
    for (int y = 0; y < sh_h; ++y)
        for (int x = 0; x < sw_; ++x) {
            int x0 = x * img.width / sw_, x1 = std::max(x0 + 1, (x + 1) * img.width / sw_);
            int y0 = y * img.height / sh_h, y1 = std::max(y0 + 1, (y + 1) * img.height / sh_h);
            float acc[3] = {0, 0, 0};
            int n = 0;
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx) {
                    const float* p = &img.rgb[(size_t(yy) * img.width + xx) * 3];
                    acc[0] += p[0];
                    acc[1] += p[1];
                    acc[2] += p[2];
                    ++n;
                }
            for (int k = 0; k < 3; ++k) small[(size_t(y) * sw_ + x) * 3 + k] = acc[k] / float(n);
        }
    auto pixelDir = [&](float u, float v) {
        float phi = (u - 0.5f) * kTwoPi;
        float theta = v * kPi;
        return Vec3(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
    };
    // sun: brightest pixel in the upper hemisphere
    float best = 0;
    Vec3 bestDir = env.sunDirection.normalized();
    for (int y = 0; y < sh_h / 2; ++y)
        for (int x = 0; x < sw_; ++x) {
            float l = luminance(&small[(size_t(y) * sw_ + x) * 3]);
            if (l > best) {
                best = l;
                bestDir = pixelDir((float(x) + 0.5f) / float(sw_), (float(y) + 0.5f) / float(sh_h));
            }
        }
    Vec3 envSun = bestDir;
    // env space -> world space (inverse of envDir rotation)
    float c = std::cos(-env.rotation), s = std::sin(-env.rotation);
    Vec3 worldSun(envSun.x * c - envSun.z * s, envSun.y, envSun.x * s + envSun.z * c);
    sunDir_ = (env.autoSun && ok && best > 2.0f) ? worldSun.normalized() : env.sunDirection.normalized();
    if (sunDir_.y < 0.08f) sunDir_ = Vec3(sunDir_.x, 0.08f, sunDir_.z).normalized();

    // SH projection (clamped so the sun does not dominate the ambient term)
    double shAcc[9][3] = {};
    for (int y = 0; y < sh_h; ++y) {
        float v = (float(y) + 0.5f) / float(sh_h);
        float theta = v * kPi;
        float dOmega = (kTwoPi / float(sw_)) * (kPi / float(sh_h)) * std::sin(theta);
        for (int x = 0; x < sw_; ++x) {
            Vec3 d = pixelDir((float(x) + 0.5f) / float(sw_), v);
            const float* p = &small[(size_t(y) * sw_ + x) * 3];
            float col[3] = {p[0], p[1], p[2]};
            float l = luminance(col);
            if (l > 8.0f)
                for (float& cc : col) cc *= 8.0f / l;
            float Y[9] = {0.282095f,
                          0.488603f * d.y,
                          0.488603f * d.z,
                          0.488603f * d.x,
                          1.092548f * d.x * d.y,
                          1.092548f * d.y * d.z,
                          0.315392f * (3.0f * d.z * d.z - 1.0f),
                          1.092548f * d.x * d.z,
                          0.546274f * (d.x * d.x - d.y * d.y)};
            for (int k = 0; k < 9; ++k)
                for (int ch = 0; ch < 3; ++ch) shAcc[k][ch] += double(col[ch] * Y[k] * dOmega);
        }
    }
    const float band[9] = {kPi, 2.0f * kPi / 3.0f, 2.0f * kPi / 3.0f, 2.0f * kPi / 3.0f, kPi / 4.0f, kPi / 4.0f, kPi / 4.0f, kPi / 4.0f, kPi / 4.0f};
    // SH coefficients are rotated with the environment by evaluating in env space: we store them in env space
    // and the shader evaluates with world normals, so rotate the basis: approximate by rotating L1/L2 via re-projection.
    // Simplest exact approach: re-project using rotated directions.
    if (std::fabs(env.rotation) > 1e-4f) {
        double rotAcc[9][3] = {};
        for (int y = 0; y < sh_h; ++y) {
            float v = (float(y) + 0.5f) / float(sh_h);
            float theta = v * kPi;
            float dOmega = (kTwoPi / float(sw_)) * (kPi / float(sh_h)) * std::sin(theta);
            for (int x = 0; x < sw_; ++x) {
                Vec3 e = pixelDir((float(x) + 0.5f) / float(sw_), v);
                Vec3 d(e.x * c - e.z * s, e.y, e.x * s + e.z * c);  // world direction of this env texel
                const float* p = &small[(size_t(y) * sw_ + x) * 3];
                float col[3] = {p[0], p[1], p[2]};
                float l = luminance(col);
                if (l > 8.0f)
                    for (float& cc : col) cc *= 8.0f / l;
                float Y[9] = {0.282095f, 0.488603f * d.y, 0.488603f * d.z, 0.488603f * d.x, 1.092548f * d.x * d.y,
                              1.092548f * d.y * d.z, 0.315392f * (3.0f * d.z * d.z - 1.0f), 1.092548f * d.x * d.z,
                              0.546274f * (d.x * d.x - d.y * d.y)};
                for (int k = 0; k < 9; ++k)
                    for (int ch = 0; ch < 3; ++ch) rotAcc[k][ch] += double(col[ch] * Y[k] * dOmega);
            }
        }
        std::memcpy(shAcc, rotAcc, sizeof(shAcc));
    }
    for (int k = 0; k < 9; ++k)
        sh_[k] = Vec4(float(shAcc[k][0]) * band[k] / kPi, float(shAcc[k][1]) * band[k] / kPi, float(shAcc[k][2]) * band[k] / kPi, 0.0f);
    {
        // irradiance/pi on an upward facing surface (Y00, Y10 terms), used for exposure + sun calibration
        float up[3];
        for (int ch = 0; ch < 3; ++ch) up[ch] = sh_[0][ch] * 0.282095f + sh_[1][ch] * 0.488603f - sh_[6][ch] * 0.315392f - sh_[8][ch] * 0.546274f;
        skyUpLum_ = std::max(1e-4f, luminance(up));
    }

    // prefiltered specular cube
    envMips_ = 6;
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_CUBE;
    ci.format = kHdrFormat;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    ci.width = ci.height = kEnvSize;
    ci.layer_count_or_depth = 6;
    ci.num_levels = uint32_t(envMips_);
    envCube_ = gpu().createTexture(ci, "env specular");
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu().device());
    for (int mip = 0; mip < envMips_; ++mip) {
        float rough = float(mip) / float(envMips_ - 1);
        for (int face = 0; face < 6; ++face) {
            Vec4 params(float(face), rough, float(img.width), 6.0f);  // the sun is lit analytically: keep it out of the IBL
            fullscreen(cmd, envCube_.handle, uint32_t(mip), prefilter_,
                       {{envEquirect_.handle, gpu().sampler(SamplerKind::LinearRepeat)}}, &params, sizeof(params), true, false,
                       uint32_t(face));
        }
    }
    SDL_SubmitGPUCommandBuffer(cmd);
    LOG_INFO("renderer: environment '%s' (%dx%d) processed in %.0f ms, sun dir (%.2f %.2f %.2f)", loadedHdri_.c_str(), img.width,
             img.height, t.milliseconds(), sunDir_.x, sunDir_.y, sunDir_.z);
}

// ---------------------------------------------------------------------------
void Renderer::computeShadowCascades(const RenderView& view, int cascades, int size) {
    cascadeCount_ = cascades;
    if (cascades == 0) return;
    float maxDist = settings_.shadowQuality >= 4 ? 220.0f : settings_.shadowQuality == 3 ? 160.0f : settings_.shadowQuality == 2 ? 110.0f : 70.0f;
    float nearZ = view.nearZ;
    float splits[4] = {};
    for (int i = 0; i < cascades; ++i) {
        float p = float(i + 1) / float(cascades);
        float uni = nearZ + (maxDist - nearZ) * p;
        float lg = nearZ * std::pow(maxDist / nearZ, p);
        splits[i] = lerpf(uni, lg, 0.88f);
    }
    Vec3 L = sunDir_.normalized();
    Vec3 up = std::fabs(L.y) > 0.99f ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
    float tanY = std::tan(view.fovY * 0.5f), tanX = tanY * view.aspect;
    float prev = nearZ;
    for (int i = 0; i < 4; ++i) {
        if (i >= cascades) {
            frame_.shadowMatrices[i] = Mat4::identity();
            continue;
        }
        float n = prev, f = splits[i];
        prev = f;
        Vec3 corners[8];
        int k = 0;
        for (float d : {n, f})
            for (int sy = -1; sy <= 1; sy += 2)
                for (int sx = -1; sx <= 1; sx += 2)
                    corners[k++] = view.position + view.forward * d + view.right * (float(sx) * tanX * d) + view.up * (float(sy) * tanY * d);
        Vec3 center(0);
        for (auto& c : corners) center += c;
        center /= 8.0f;
        float radius = 0;
        for (auto& c : corners) radius = std::max(radius, distance(c, center));
        radius = std::ceil(radius * 8.0f) / 8.0f;
        float back = 250.0f;
        Mat4 lightView = Mat4::lookAt(center + L * (radius + back), center, up);
        Mat4 proj = Mat4::ortho(-radius, radius, -radius, radius, 0.0f, back + radius * 2.0f);
        Mat4 m = proj * lightView;
        // stabilize: snap the projected world origin to whole texels
        Vec4 o = m * Vec4(0, 0, 0, 1);
        float half = float(size) * 0.5f;
        float ox = o.x * half, oy = o.y * half;
        float dx = (std::round(ox) - ox) / half, dy = (std::round(oy) - oy) / half;
        proj.m[12] += dx;
        proj.m[13] += dy;
        frame_.shadowMatrices[i] = proj * lightView;
        frame_.cascadeSplits[i] = f;
        frame_.cascadeTexel[i] = radius * 2.0f / float(size);
    }
}

void Renderer::collectView(RenderScene& scene, const Frustum& fr, const Vec3& camPos, float maxDist, bool shadow,
                           uint32_t layerMask, std::vector<uint32_t>& out) {
    out.clear();
    uint32_t stamp = ++frameStamp_;
    auto& visit = scene.visitStamp();
    float maxDist2 = maxDist * maxDist;
    auto consider = [&](uint32_t id) {
        if (visit[id] == stamp) return;
        visit[id] = stamp;
        const RenderObject* o = scene.get(id);
        if (!o || !o->visible || !o->mesh || !(o->layer & layerMask)) return;
        if (shadow && !o->castShadows) return;
        float cd = o->cullDistance > 0.0f ? o->cullDistance : maxDist;
        if (o->worldBounds.distanceSq(camPos) > std::min(maxDist2, cd * cd)) return;
        if (!fr.testAABB(o->worldBounds)) {
            Profiler::renderStats().culledObjects += shadow ? 0 : 1;
            return;
        }
        out.push_back(id);
    };
    const SpatialGrid& grid = scene.grid();
    if (grid.valid()) {
        AABB region = AABB::fromCenterExtents(camPos, Vec3(maxDist, 10000.0f, maxDist));
        grid.query(region, [&](int, int, const std::vector<uint32_t>& ids, const AABB& cellBox) {
            if (ids.empty() || !cellBox.valid() || !fr.testAABB(cellBox)) return;
            for (uint32_t id : ids) consider(id);
        });
        for (uint32_t id : grid.large()) consider(id);
    } else {
        for (uint32_t id = 0; id < scene.capacity(); ++id)
            if (scene.valid(id) && scene.get(id)->isStatic) consider(id);
    }
    for (uint32_t id : scene.dynamicObjects()) consider(id);
}

void Renderer::buildBatches(RenderScene& scene, const std::vector<uint32_t>& objs, const Vec3& camPos, bool shadow,
                            std::vector<DrawBatch>& opaque, std::vector<DrawBatch>* transparent) {
    opaque.clear();
    if (transparent) transparent->clear();
    struct Item {
        uint64_t key;
        uint32_t obj;
        GpuMesh* mesh;
        Material* mat;
        uint32_t firstIndex, indexCount;
        int pipe;
    };
    static thread_local std::vector<Item> items;
    items.clear();
    float tanHalf = std::tan(frame_.proj.m[5] > 0 ? std::atan(1.0f / frame_.proj.m[5]) : 0.5f);
    Material* defMat = assets().defaultMaterial().get();
    for (uint32_t id : objs) {
        const RenderObject* o = scene.get(id);
        GpuMesh* mesh = o->mesh.get();
        if (mesh->lods.empty()) continue;
        Vec3 c = o->worldBounds.center();
        float r = o->worldBounds.extents().length();
        float dist = std::max(0.1f, distance(camPos, c) - r);
        float screen = r / (dist * tanHalf) * o->lodBias / settings_.lodBias;
        if (shadow) screen *= 0.6f;
        if (screen < 0.004f && r < 30.0f) continue;  // "culled" LOD level: too small to matter
        int lod = 0;
        for (int l = 1; l < int(mesh->lods.size()); ++l)
            if (screen < mesh->lods[size_t(l)].screenSize) lod = l;
        const MeshLod& ml = mesh->lods[size_t(lod)];
        bool skinned = mesh->skinned();
        for (size_t si = 0; si < ml.submeshes.size(); ++si) {
            const SubMesh& sm = ml.submeshes[si];
            if (sm.indexCount == 0) continue;
            Material* mat = size_t(sm.material) < o->materials.size() && o->materials[size_t(sm.material)] ? o->materials[size_t(sm.material)].get() : defMat;
            int pipe;
            if (shadow) {
                if (mat->alphaMode == AlphaMode::Blend) continue;
                pipe = skinned ? 1 : (mat->alphaMode == AlphaMode::Mask ? 2 : 0);
            } else if (mat->alphaMode == AlphaMode::Blend) {
                // decals are drawn right after the opaque geometry, batched like it
                pipe = mat->decal ? PipeDecal : PipeBlend;
            } else if (skinned) {
                pipe = PipeSkinned;
            } else if (mat->doubleSided || mat->alphaMode == AlphaMode::Mask) {
                pipe = PipeDoubleSided;
            } else {
                pipe = PipeOpaque;
            }
            uint64_t matKey = shadow && pipe != 2 ? 0 : (mat->id & 0xFFFFF);
            uint64_t key = (uint64_t(pipe) << 60) | (matKey << 40) | (uint64_t(mesh->id & 0xFFFFFF) << 16) | (uint64_t(lod) << 8) | uint64_t(si & 0xFF);
            items.push_back({key, id, mesh, mat, sm.firstIndex, sm.indexCount, pipe});
        }
    }
    if (transparent) {
        // transparent items are drawn individually, back to front
        for (const Item& it : items) {
            if (it.pipe != PipeBlend) continue;
            DrawBatch b;
            b.mesh = it.mesh;
            b.material = it.mat;
            b.firstIndex = it.firstIndex;
            b.indexCount = it.indexCount;
            b.firstVisible = uint32_t(visibleIndices_.size());
            b.instanceCount = 1;
            b.pipe = PipeBlend;
            b.distance = (scene.get(it.obj)->worldBounds.center() - camPos).lengthSq();
            visibleIndices_.push_back(it.obj);
            transparent->push_back(b);
        }
        std::sort(transparent->begin(), transparent->end(), [](const DrawBatch& a, const DrawBatch& b) { return a.distance > b.distance; });
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.key < b.key; });
    for (size_t i = 0; i < items.size();) {
        const Item& it = items[i];
        if (!shadow && it.pipe == PipeBlend) {
            ++i;
            continue;
        }
        DrawBatch b;
        b.mesh = it.mesh;
        b.material = it.mat;
        b.firstIndex = it.firstIndex;
        b.indexCount = it.indexCount;
        b.firstVisible = uint32_t(visibleIndices_.size());
        b.pipe = it.pipe;
        size_t j = i;
        while (j < items.size() && items[j].key == it.key) {
            visibleIndices_.push_back(items[j].obj);
            ++j;
        }
        b.instanceCount = uint32_t(j - i);
        opaque.push_back(b);
        i = j;
    }
}

void Renderer::cullAndBatch(RenderScene& scene, const RenderView& view) {
    visibleIndices_.clear();
    Frustum fr = Frustum::fromMatrix(view.viewProj);
    collectView(scene, fr, view.position, settings_.drawDistance, false, 0xffffffffu, scratchObjs_);
    Profiler::renderStats().visibleObjects = uint32_t(scratchObjs_.size());
    buildBatches(scene, scratchObjs_, view.position, false, mainBatches_, &transparentBatches_);
    for (int c = 0; c < 4; ++c) {
        shadowBatches_[c].clear();
        if (c >= cascadeCount_) continue;
        Frustum sf = Frustum::fromMatrix(frame_.shadowMatrices[c]);
        collectView(scene, sf, view.position, frame_.cascadeSplits[c] + 60.0f, true, ~LayerEditor, scratchObjs_);
        buildBatches(scene, scratchObjs_, view.position, true, shadowBatches_[c], nullptr);
    }
}

void Renderer::bindMaterial(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd, Material* m, int mode) {
    AssetManager& am = assets();
    auto tex = [&](const TexturePtr& t, const TexturePtr& fallback) { return (t && t->gpuTex.handle) ? t->gpuTex.handle : fallback->gpuTex.handle; };
    SDL_GPUSampler* aniso = gpu().sampler(SamplerKind::AnisoRepeat);
    MaterialUniforms mu = materialUniforms(*m);
    if (mode == 0) {
        if (!detailNormal_) detailNormal_ = am.texture("assets/textures/gen/detail_normal.png", false);
        const TexturePtr& det = m->detail ? m->detail : (detailNormal_ ? detailNormal_ : am.flatNormal());
        SDL_GPUTextureSamplerBinding b[9] = {
            {tex(m->baseColor, am.white()), aniso},
            {tex(m->normal, am.flatNormal()), aniso},
            {tex(m->orm, am.white()), aniso},
            {tex(m->emissive, am.white()), aniso},
            {shadowMap_.handle, gpu().sampler(SamplerKind::ShadowCompare)},
            {ssao_.handle, gpu().sampler(SamplerKind::LinearClamp)},
            {envCube_.handle, gpu().sampler(SamplerKind::LinearClamp)},
            {brdfLut_.handle, gpu().sampler(SamplerKind::LinearClamp)},
            {tex(det, am.flatNormal()), aniso},
        };
        SDL_BindGPUFragmentSamplers(rp, 0, b, 9);
    } else {
        SDL_GPUTextureSamplerBinding b[1] = {{tex(m->baseColor, am.white()), aniso}};
        SDL_BindGPUFragmentSamplers(rp, 0, b, 1);
    }
    SDL_PushGPUFragmentUniformData(cmd, 1, &mu, sizeof(mu));
}

void Renderer::drawBatches(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd, const std::vector<DrawBatch>& batches, int mode,
                           int cascade) {
    // mode: 0 = forward, 1 = depth prepass, 2 = shadow
    GfxPipeline* lastPipe = nullptr;
    Material* lastMat = nullptr;
    GpuMesh* lastMesh = nullptr;
    auto& stats = Profiler::renderStats();
    for (const DrawBatch& b : batches) {
        GfxPipeline* pipe = mode == 0 ? pbr_[b.pipe] : mode == 1 ? depthPipe_[b.pipe] : shadowPipe_[b.pipe];
        if (!pipe || !pipe->valid()) continue;
        if (pipe != lastPipe) {
            SDL_BindGPUGraphicsPipeline(rp, pipe->handle);
            lastPipe = pipe;
            lastMat = nullptr;
            lastMesh = nullptr;
        }
        bool needsMaterial = mode != 2 || b.pipe == 2;
        if (needsMaterial && b.material != lastMat) {
            bindMaterial(rp, cmd, b.material, mode);
            lastMat = b.material;
        }
        if (b.mesh != lastMesh) {
            SDL_GPUBufferBinding vb[2] = {{b.mesh->vertexBuffer.handle, 0}, {b.mesh->skinBuffer.handle, 0}};
            SDL_BindGPUVertexBuffers(rp, 0, vb, b.mesh->skinned() ? 2 : 1);
            SDL_GPUBufferBinding ib{b.mesh->indexBuffer.handle, 0};
            SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            lastMesh = b.mesh;
        }
        uint32_t draw[4] = {b.firstVisible, uint32_t(cascade), 0, 0};
        SDL_PushGPUVertexUniformData(cmd, 1, draw, sizeof(draw));
        SDL_DrawGPUIndexedPrimitives(rp, b.indexCount, b.instanceCount, b.firstIndex, 0, 0);
        if (mode == 2)
            stats.shadowDrawCalls++;
        else
            stats.drawCalls++;
        if (mode == 0) {
            stats.triangles += b.indexCount / 3 * b.instanceCount;
            stats.instances += b.instanceCount;
        }
    }
}

// ---------------------------------------------------------------------------
bool Renderer::renderFrame(RenderScene& scene, const RenderView& viewIn, const UIDrawList* ui, const RenderCallbacks& cb, float dt) {
    time_ += dt;
    loadEnvironment(scene);
    int w, h;
    window_->pixelSize(w, h);
    if (window_->minimized() || w <= 0 || h <= 0) return false;
    if (w != outW_ || h != outH_) {
        gpu().waitIdle();
        createTargets(w, h);
    }
    RenderView view = viewIn;
    if (std::fabs(view.aspect - float(rw_) / float(rh_)) > 1e-3f) {
        view.aspect = float(rw_) / float(rh_);
        view.proj = Mat4::perspectiveReverseZ(view.fovY, view.aspect, view.nearZ);
        view.viewProj = view.proj * view.view;
    }
    const Environment& env = scene.environment;
    // a camera cut (respawn, menu -> shop) cannot be reprojected: start the temporal effects over
    if (hasPrev_ && (distance(view.position, prevCamPos_) > 6.0f || dot(view.forward, prevCamFwd_) < 0.5f)) resetHistory();
    const Mat4 viewProjNoJitter = view.viewProj;
    const bool taa = settings_.antiAliasing == 2 && taa_ && taa_->valid();
    Vec2 jitter(0.0f, 0.0f);
    if (taa) {
        // Halton (2, 3) sub pixel offsets, 8 frame cycle
        auto halton = [](uint32_t i, uint32_t b) {
            float f = 1.0f, r = 0.0f;
            for (; i > 0; i /= b) {
                f /= float(b);
                r += f * float(i % b);
            }
            return r;
        };
        taaPhase_ = taaFrame_++ % 8u + 1u;
        jitter = Vec2((halton(taaPhase_, 2) - 0.5f) * 2.0f / float(rw_), (halton(taaPhase_, 3) - 0.5f) * 2.0f / float(rh_));
        view.proj = Mat4::translation(Vec3(jitter.x, jitter.y, 0.0f)) * view.proj;
        view.viewProj = view.proj * view.view;
    } else {
        taaValid_ = false;
        taaPhase_ = 0;
    }

    Profiler::begin(ProfileSection::RenderPrep);
    // frame constants
    frame_.view = view.view;
    frame_.proj = view.proj;
    frame_.viewProj = view.viewProj;
    frame_.invViewProj = view.viewProj.inverse();
    frame_.prevViewProj = hasPrev_ ? prevViewProj_ : viewProjNoJitter;
    frame_.invView = view.view.affineInverse();
    frame_.invProj = view.proj.inverse();
    frame_.cameraPos = Vec4(view.position, time_);
    // env.sunIntensity is the ratio of direct sun to sky illuminance; exposure is derived from the
    // lighting level so every HDRI ends up correctly exposed (env.exposure = EV style compensation)
    float sunI = env.sunIntensity * kPi * skyUpLum_ * env.iblIntensity;
    float whiteLum = skyUpLum_ * env.iblIntensity * (1.0f + env.sunIntensity * std::max(sunDir_.y, 0.15f));
    // key: a sunlit 18% grey surface ends up at ~0.23 before tone mapping (mid grey after ACES)
    float exposure = clampf(1.25f / std::max(whiteLum, 1e-4f), 0.0005f, 5000.0f) * env.exposure * settings_.brightness;
    frame_.sunDir = Vec4(sunDir_, sunI);
    frame_.sunColor = Vec4(env.sunColor, env.iblIntensity);
    frame_.viewport = Vec4(float(rw_), float(rh_), 1.0f / float(rw_), 1.0f / float(rh_));
    int cascades = settings_.shadowQuality > 0 ? shadowLayers_ : 0;
    computeShadowCascades(view, cascades, std::max(shadowSize_, 1));
    frame_.shadowParams = Vec4(1.0f / float(std::max(shadowSize_, 1)), 0.0004f, 1.5f, float(cascades));
    frame_.fogParams = Vec4(env.fogDensity, env.fogFalloff, env.fogStart, env.fogMax);
    frame_.fogColor = Vec4(env.fogTint, 1.0f);
    frame_.envParams = Vec4(env.rotation, float(envMips_ - 1), env.skyIntensity, 0.0f);
    for (int i = 0; i < 9; ++i) frame_.sh[i] = sh_[i];

    // lights: nearest enabled lights inside the view
    std::vector<LightGpu> lights;
    {
        Frustum fr = Frustum::fromMatrix(view.viewProj);
        struct Cand {
            float d;
            const LightProxy* l;
        };
        std::vector<Cand> cands;
        const auto& all = scene.lights();
        const auto& alive = scene.lightAlive();
        for (size_t i = 0; i < all.size(); ++i) {
            const LightProxy& l = all[i];
            if (!alive[i] || !l.enabled || l.intensity <= 0.0f) continue;
            float d = distance(l.position, view.position);
            if (d > settings_.drawDistance * 0.4f + l.radius) continue;
            if (!fr.testSphere(l.position, l.radius)) continue;
            cands.push_back({d, &l});
        }
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
        for (size_t i = 0; i < cands.size() && i < size_t(kMaxLights); ++i) {
            const LightProxy& l = *cands[i].l;
            LightGpu g;
            g.posRadius = Vec4(l.position, l.radius);
            g.colorIntensity = Vec4(l.color, l.intensity);
            g.dirType = Vec4(l.direction.normalized(), l.type == LightProxy::Type::Spot ? 1.0f : 0.0f);
            g.spot = Vec4(std::cos(l.innerAngle), std::cos(l.outerAngle), 0, 0);
            lights.push_back(g);
        }
        lightCount_ = uint32_t(lights.size());
        Profiler::renderStats().lights = lightCount_;
    }
    frame_.misc = Vec4(exposure, float(lightCount_), settings_.ssao ? 1.0f : 0.0f, float(settings_.debugView));
    frame_.extra = Vec4(env.lampsOn ? 1.0f : 0.0f, 0.0f, 0.0f, env.urbanReflection);
    frame_.taa = Vec4(jitter.x, jitter.y, float(scene.bones().size()), float(taaPhase_));

    cullAndBatch(scene, view);

    // particles + debug lines vertex data
    std::vector<ParticleVertex> pAlpha, pAdd;
    if (settings_.particles) particles().buildVertices(view.right, view.up, view.position, pAlpha, pAdd);
    const auto& lines = debugDraw().lines();
    Profiler::end(ProfileSection::RenderPrep);

    Profiler::begin(ProfileSection::Render);
    SDL_GPUCommandBuffer* cmd = gpu().beginFrame();
    if (!cmd) {
        Profiler::end(ProfileSection::Render);
        return false;
    }

    // uploads -----------------------------------------------------------------
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    {
        const auto& inst = scene.instances();
        uint32_t needed = uint32_t(std::max<size_t>(inst.size(), 1) * sizeof(InstanceGpu));
        if (!instanceBuf_.buffer.handle || instanceBuf_.capacity < needed) {
            instanceBuf_.name = "instances";
            instanceBuf_.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
            gpu().release(instanceBuf_.buffer);
            if (instanceBuf_.transfer) SDL_ReleaseGPUTransferBuffer(gpu().device(), instanceBuf_.transfer);
            instanceBuf_.transfer = nullptr;
            instanceBuf_.capacity = 0;
            uint32_t cap = std::max(needed, uint32_t(1024 * sizeof(InstanceGpu))) * 3 / 2;
            instanceBuf_.buffer = gpu().createBuffer(SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, cap, "instances");
            instanceBuf_.capacity = cap;
            scene.markAllDirty();
        }
        uint32_t first, count;
        if (!inst.empty() && scene.takeDirtyRange(first, count)) {
            uint32_t bytes = count * uint32_t(sizeof(InstanceGpu));
            SDL_GPUTransferBufferCreateInfo ti{};
            ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            ti.size = bytes;
            SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(gpu().device(), &ti);
            void* map = SDL_MapGPUTransferBuffer(gpu().device(), tb, false);
            std::memcpy(map, &inst[first], bytes);
            SDL_UnmapGPUTransferBuffer(gpu().device(), tb);
            SDL_GPUTransferBufferLocation src{tb, 0};
            SDL_GPUBufferRegion dst{instanceBuf_.buffer.handle, first * uint32_t(sizeof(InstanceGpu)), bytes};
            SDL_UploadToGPUBuffer(copy, &src, &dst, false);
            SDL_ReleaseGPUTransferBuffer(gpu().device(), tb);
        }
    }
    visibleBuf_.name = "visible";
    if (!visibleIndices_.empty()) uploadDynamic(copy, visibleBuf_, visibleIndices_.data(), uint32_t(visibleIndices_.size() * 4));
    boneBuf_.name = "bones";
    if (!scene.bones().empty()) {
        // current palette followed by the previous one (motion vectors of the skinned rider)
        const auto& bones = scene.bones();
        if (prevBones_.size() != bones.size() || !hasPrev_) prevBones_ = bones;
        boneUpload_.assign(bones.begin(), bones.end());
        boneUpload_.insert(boneUpload_.end(), prevBones_.begin(), prevBones_.end());
        uploadDynamic(copy, boneBuf_, boneUpload_.data(), uint32_t(boneUpload_.size() * sizeof(Mat4)));
    }
    lightBuf_.name = "lights";
    if (!lights.empty()) uploadDynamic(copy, lightBuf_, lights.data(), uint32_t(lights.size() * sizeof(LightGpu)));
    lineBuf_.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    lineBuf_.name = "debug lines";
    if (!lines.empty()) uploadDynamic(copy, lineBuf_, lines.data(), uint32_t(lines.size() * sizeof(DebugVertex)));
    particleBuf_.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    particleBuf_.name = "particles";
    std::vector<ParticleVertex> pAll = pAlpha;
    pAll.insert(pAll.end(), pAdd.begin(), pAdd.end());
    if (!pAll.empty()) uploadDynamic(copy, particleBuf_, pAll.data(), uint32_t(pAll.size() * sizeof(ParticleVertex)));
    uiVertBuf_.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    uiVertBuf_.name = "ui vertices";
    uiIndexBuf_.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    uiIndexBuf_.name = "ui indices";
    if (ui && !ui->vertices.empty()) {
        uploadDynamic(copy, uiVertBuf_, ui->vertices.data(), uint32_t(ui->vertices.size() * sizeof(UIVertex)));
        uploadDynamic(copy, uiIndexBuf_, ui->indices.data(), uint32_t(ui->indices.size() * 4));
    }
    SDL_EndGPUCopyPass(copy);
    if (cb.prepareOverlay) cb.prepareOverlay(cmd);

    SDL_GPUBuffer* vstorage[3] = {instanceBuf_.buffer.handle, visibleBuf_.buffer.handle, boneBuf_.buffer.handle};

    // shadow cascades ------------------------------------------------------------
    for (int c = 0; c < cascadeCount_; ++c) {
        SDL_GPUDepthStencilTargetInfo dt{};
        dt.texture = shadowMap_.handle;
        dt.clear_depth = 1.0f;
        dt.load_op = SDL_GPU_LOADOP_CLEAR;
        dt.store_op = SDL_GPU_STOREOP_STORE;
        dt.layer = uint8_t(c);
        SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, nullptr, 0, &dt);
        SDL_PushGPUVertexUniformData(cmd, 0, &frame_, sizeof(frame_));
        SDL_PushGPUFragmentUniformData(cmd, 0, &frame_, sizeof(frame_));
        SDL_BindGPUVertexStorageBuffers(rp, 0, vstorage, 3);
        drawBatches(rp, cmd, shadowBatches_[c], 2, c);
        SDL_EndGPURenderPass(rp);
    }

    // depth + normal prepass ------------------------------------------------------
    {
        SDL_GPUColorTargetInfo ct{};
        ct.texture = normals_.handle;
        ct.load_op = SDL_GPU_LOADOP_CLEAR;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        ct.clear_color = {0, 0, 1, 0};
        SDL_GPUDepthStencilTargetInfo dt{};
        dt.texture = depth_.handle;
        dt.clear_depth = 0.0f;
        dt.load_op = SDL_GPU_LOADOP_CLEAR;
        dt.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
        SDL_PushGPUVertexUniformData(cmd, 0, &frame_, sizeof(frame_));
        SDL_PushGPUFragmentUniformData(cmd, 0, &frame_, sizeof(frame_));
        SDL_BindGPUVertexStorageBuffers(rp, 0, vstorage, 3);
        drawBatches(rp, cmd, mainBatches_, 1, 0);
        SDL_EndGPURenderPass(rp);
    }

    // SSAO -------------------------------------------------------------------------
    if (settings_.ssao) {
        Vec4 params(0.55f, 1.1f, 0.03f, 1.4f);
        fullscreen(cmd, ssao_.handle, 0, ssaoPipe_,
                   {{depth_.handle, gpu().sampler(SamplerKind::PointClamp)}, {normals_.handle, gpu().sampler(SamplerKind::PointClamp)}},
                   &params, sizeof(params), true, true);
        float hw = float(std::max(1, rw_ / 2)), hh = float(std::max(1, rh_ / 2));
        Vec4 bh(1.0f / hw, 0.0f, view.nearZ, 0.0f), bv(0.0f, 1.0f / hh, view.nearZ, 0.0f);
        fullscreen(cmd, ssaoTemp_.handle, 0, ssaoBlur_,
                   {{ssao_.handle, gpu().sampler(SamplerKind::LinearClamp)}, {depth_.handle, gpu().sampler(SamplerKind::PointClamp)}},
                   &bh, sizeof(bh), true, true);
        fullscreen(cmd, ssao_.handle, 0, ssaoBlur_,
                   {{ssaoTemp_.handle, gpu().sampler(SamplerKind::LinearClamp)}, {depth_.handle, gpu().sampler(SamplerKind::PointClamp)}},
                   &bv, sizeof(bv), true, true);
    }

    // forward opaque + sky + transparent + particles + debug ---------------------------------
    {
        SDL_GPUColorTargetInfo ct{};
        ct.texture = hdr_.handle;
        ct.load_op = SDL_GPU_LOADOP_CLEAR;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        ct.clear_color = {0, 0, 0, 1};
        SDL_GPUDepthStencilTargetInfo dt{};
        dt.texture = depth_.handle;
        dt.load_op = SDL_GPU_LOADOP_LOAD;
        dt.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
        SDL_PushGPUVertexUniformData(cmd, 0, &frame_, sizeof(frame_));
        SDL_PushGPUFragmentUniformData(cmd, 0, &frame_, sizeof(frame_));
        SDL_BindGPUVertexStorageBuffers(rp, 0, vstorage, 3);
        SDL_GPUBuffer* fstorage[1] = {lightBuf_.buffer.handle};
        SDL_BindGPUFragmentStorageBuffers(rp, 0, fstorage, 1);
        drawBatches(rp, cmd, mainBatches_, 0, 0);

        if (sky_ && sky_->valid()) {
            SDL_BindGPUGraphicsPipeline(rp, sky_->handle);
            SDL_GPUTextureSamplerBinding sb{envEquirect_.handle, gpu().sampler(SamplerKind::LinearRepeat)};
            SDL_BindGPUFragmentSamplers(rp, 0, &sb, 1);
            SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
        }
        if (!transparentBatches_.empty()) {
            SDL_BindGPUFragmentStorageBuffers(rp, 0, fstorage, 1);
            drawBatches(rp, cmd, transparentBatches_, 0, 0);
        }
        if (!pAll.empty()) {
            SDL_GPUBufferBinding vb{particleBuf_.buffer.handle, 0};
            if (!pAlpha.empty() && particlesAlpha_->valid()) {
                SDL_BindGPUGraphicsPipeline(rp, particlesAlpha_->handle);
                SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
                SDL_DrawGPUPrimitives(rp, uint32_t(pAlpha.size()), 1, 0, 0);
            }
            if (!pAdd.empty() && particlesAdd_->valid()) {
                SDL_BindGPUGraphicsPipeline(rp, particlesAdd_->handle);
                SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
                SDL_DrawGPUPrimitives(rp, uint32_t(pAdd.size()), 1, uint32_t(pAlpha.size()), 0);
            }
        }
        if (!lines.empty()) {
            GfxPipeline* lp = debugDraw().depthTested ? debugLines_ : debugLinesNoDepth_;
            if (lp && lp->valid()) {
                SDL_BindGPUGraphicsPipeline(rp, lp->handle);
                SDL_GPUBufferBinding vb{lineBuf_.buffer.handle, 0};
                SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
                SDL_DrawGPUPrimitives(rp, uint32_t(lines.size()), 1, 0, 0);
            }
        }
        SDL_EndGPURenderPass(rp);
    }

    // post processing ------------------------------------------------------------------
    GpuTexture* src = &hdr_;
    if (taa) {
        GpuTexture& out = taaHistory_[taaIndex_];
        GpuTexture& history = taaHistory_[taaIndex_ ^ 1];
        Vec4 params(taaValid_ ? 1.0f : 0.0f, 0.08f, 0.22f, 0.0f);
        fullscreen(cmd, out.handle, 0, taa_,
                   {{hdr_.handle, gpu().sampler(SamplerKind::PointClamp)},
                    {history.handle, gpu().sampler(SamplerKind::LinearClamp)},
                    {depth_.handle, gpu().sampler(SamplerKind::PointClamp)},
                    {normals_.handle, gpu().sampler(SamplerKind::PointClamp)}},
                   &params, sizeof(params), true, true);
        src = &out;
        taaIndex_ ^= 1;
        taaValid_ = true;
    }
    if (settings_.motionBlur && hasPrev_) {
        Vec4 params(settings_.motionBlurStrength, 0.035f, 0, 0);
        fullscreen(cmd, hdrTemp_.handle, 0, motionBlur_,
                   {{src->handle, gpu().sampler(SamplerKind::LinearClamp)}, {depth_.handle, gpu().sampler(SamplerKind::PointClamp)}},
                   &params, sizeof(params), true, true);
        src = &hdrTemp_;
    }
    if (settings_.bloom) {
        for (int i = 0; i < kBloomLevels; ++i) {
            GpuTexture& from = i == 0 ? *src : bloom_[size_t(i - 1)];
            Vec4 params(1.0f / float(from.width), 1.0f / float(from.height), i == 0 ? 1.0f : 0.0f, env.bloomThreshold / std::max(frame_.misc.x, 1e-6f));
            fullscreen(cmd, bloom_[size_t(i)].handle, 0, bloomDown_, {{from.handle, gpu().sampler(SamplerKind::LinearClamp)}},
                       &params, sizeof(params), true);
        }
        for (int i = kBloomLevels - 1; i > 0; --i) {
            GpuTexture& from = bloom_[size_t(i)];
            Vec4 params(1.0f / float(from.width), 1.0f / float(from.height), 1.0f, 1.0f);
            fullscreen(cmd, bloom_[size_t(i - 1)].handle, 0, bloomUp_, {{from.handle, gpu().sampler(SamplerKind::LinearClamp)}},
                       &params, sizeof(params), false);
        }
    }
    {
        struct {
            Vec4 exposure, grading, flash;
        } pp;
        pp.exposure = Vec4(frame_.misc.x, settings_.bloom ? env.bloomStrength : 0.0f, env.vignette, env.saturation);
        pp.grading = Vec4(env.contrast, env.temperature, 0.0f, 0.0f);
        pp.flash = flash_;
        GpuTexture& bl = settings_.bloom ? bloom_[0] : *src;
        fullscreen(cmd, ldr_.handle, 0, tonemap_,
                   {{src->handle, gpu().sampler(SamplerKind::LinearClamp)}, {bl.handle, gpu().sampler(SamplerKind::LinearClamp)}},
                   &pp, sizeof(pp), true);
        if (!settings_.bloom) {
            // bloom texture bound as a dummy: strength is zero
        }
    }
    {
        // TAA softens a little: sharpen a bit more to keep the texture detail
        float sharpen = settings_.sharpen ? (settings_.renderScale < 0.99f ? 1.0f : taa ? 0.6f : 0.35f) : 0.0f;
        Vec4 params(1.0f / float(rw_), 1.0f / float(rh_), settings_.antiAliasing == 1 ? 1.0f : 0.0f, sharpen);
        fullscreen(cmd, backbuffer_.handle, 0, fxaa_, {{ldr_.handle, gpu().sampler(SamplerKind::LinearClamp)}}, &params,
                   sizeof(params), true);
    }
    // blurred copy of the final image behind frosted UI panels
    if (ui && ui->usesBackdrop) {
        for (int i = 0; i < 3; ++i) {
            const GpuTexture& from = i == 0 ? backbuffer_ : backdrop_[i - 1];
            Vec4 params(1.0f / float(from.width), 1.0f / float(from.height), 0.0f, 1.0f);
            fullscreen(cmd, backdrop_[i].handle, 0, bloomDown_, {{from.handle, gpu().sampler(SamplerKind::LinearClamp)}}, &params,
                       sizeof(params), true);
        }
    }

    // UI + overlays ----------------------------------------------------------------------
    {
        SDL_GPUColorTargetInfo ct{};
        ct.texture = backbuffer_.handle;
        ct.load_op = SDL_GPU_LOADOP_LOAD;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
        if (ui && !ui->commands.empty() && ui_->valid()) {
            SDL_BindGPUGraphicsPipeline(rp, ui_->handle);
            SDL_GPUBufferBinding vb{uiVertBuf_.buffer.handle, 0};
            SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
            SDL_GPUBufferBinding ib{uiIndexBuf_.buffer.handle, 0};
            SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            Vec4 screen(2.0f / float(outW_), 2.0f / float(outH_), 0, 0);
            SDL_PushGPUVertexUniformData(cmd, 0, &screen, sizeof(screen));
            for (const UIDrawCmd& c : ui->commands) {
                if (c.indexCount == 0) continue;
                Texture* t = c.texture ? c.texture : ui->defaultAtlas;
                SDL_GPUTexture* th = t && t->gpuTex.handle ? t->gpuTex.handle : assets().white()->gpuTex.handle;
                if (c.backdrop && backdrop_[2].handle) th = backdrop_[2].handle;
                SDL_GPUTextureSamplerBinding sb{th, gpu().sampler(SamplerKind::LinearClamp)};
                SDL_BindGPUFragmentSamplers(rp, 0, &sb, 1);
                Vec4 mode(c.backdrop ? 2.0f : c.rgbaImage ? 1.0f : 0.0f, c.softness, 1.0f / float(std::max(1u, backdrop_[2].width)),
                          1.0f / float(std::max(1u, backdrop_[2].height)));
                SDL_PushGPUFragmentUniformData(cmd, 0, &mode, sizeof(mode));
                SDL_Rect sc;
                if (c.clipW > 0 && c.clipH > 0)
                    sc = {std::max(0, c.clipX), std::max(0, c.clipY), c.clipW, c.clipH};
                else
                    sc = {0, 0, outW_, outH_};
                SDL_SetGPUScissor(rp, &sc);
                SDL_DrawGPUIndexedPrimitives(rp, c.indexCount, 1, c.firstIndex, 0, 0);
                Profiler::renderStats().drawCalls++;
            }
            SDL_Rect full{0, 0, outW_, outH_};
            SDL_SetGPUScissor(rp, &full);
        }
        if (cb.drawOverlay) cb.drawOverlay(cmd, rp);
        SDL_EndGPURenderPass(rp);
    }

    if (!screenshotPath_.empty()) takeScreenshot(cmd);

    // present -------------------------------------------------------------------------------
    Profiler::end(ProfileSection::Render);
    Profiler::begin(ProfileSection::GpuWait);
    uint32_t swW = 0, swH = 0;
    SDL_GPUTexture* swap = gpu().acquireSwapchain(cmd, &swW, &swH);
    Profiler::end(ProfileSection::GpuWait);
    if (swap) {
        // own copy pass instead of SDL's blit (its internal pipelines are not available on every D3D12 runtime)
        (void)swW;
        (void)swH;
        fullscreen(cmd, swap, 0, present_, {{backbuffer_.handle, gpu().sampler(SamplerKind::LinearClamp)}}, nullptr, 0, false);
    }
    gpu().endFrame(cmd);
    prevViewProj_ = viewProjNoJitter;
    prevCamPos_ = view.position;
    prevCamFwd_ = view.forward;
    prevBones_ = scene.bones();
    scene.commitMotion();
    hasPrev_ = true;
    return true;
}

void Renderer::takeScreenshot(SDL_GPUCommandBuffer*& cmd) {
    // copy the backbuffer (scene + UI) to a readback buffer; waits for the GPU (debug feature)
    uint32_t w = uint32_t(outW_), h = uint32_t(outH_);
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ti.size = w * h * 4;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(gpu().device(), &ti);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion src{};
    src.texture = backbuffer_.handle;
    src.w = w;
    src.h = h;
    src.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = tb;
    SDL_DownloadFromGPUTexture(cp, &src, &dst);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(gpu().device(), true, &fence, 1);
    SDL_ReleaseGPUFence(gpu().device(), fence);
    // the command buffer was consumed: acquire a fresh one for the rest of the frame
    cmd = SDL_AcquireGPUCommandBuffer(gpu().device());
    uint8_t* px = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(gpu().device(), tb, false));
    std::vector<uint8_t> img(px, px + size_t(w) * h * 4);
    SDL_UnmapGPUTransferBuffer(gpu().device(), tb);
    SDL_ReleaseGPUTransferBuffer(gpu().device(), tb);
    if (gpu().swapchainFormat() == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM || gpu().swapchainFormat() == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB)
        for (size_t i = 0; i < img.size(); i += 4) std::swap(img[i], img[i + 2]);
    for (size_t i = 3; i < img.size(); i += 4) img[i] = 255;
    fs::createDirectories(fs::parentPath(screenshotPath_));
    if (stbi_write_png(screenshotPath_.c_str(), int(w), int(h), 4, img.data(), int(w * 4)))
        LOG_INFO("renderer: screenshot saved to %s", screenshotPath_.c_str());
    else
        LOG_ERROR("renderer: screenshot failed (%s)", screenshotPath_.c_str());
    screenshotPath_.clear();
}

}  // namespace sw
