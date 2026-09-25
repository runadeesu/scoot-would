// scoot would - main renderer (SDL3 GPU; D3D12 / Vulkan)
//
// Frame: upload -> shadow cascades -> depth/normal prepass -> SSAO -> forward PBR opaque
//        -> sky -> transparent + particles -> debug lines -> TAA resolve -> motion blur (opt.) -> bloom
//        -> tone map / grading -> FXAA (opt.) + upscale + sharpen -> UI + ImGui -> present
#pragma once

#include "core/math.h"
#include "render/gpu.h"
#include "render/render_scene.h"
#include "render/shader.h"
#include "render/ui_draw.h"

#include <functional>
#include <string>
#include <vector>

namespace sw {

class Window;

struct RenderView {
    Mat4 view;
    Mat4 proj;
    Mat4 viewProj;
    Vec3 position;
    Vec3 forward{0, 0, -1};
    Vec3 right{1, 0, 0};
    Vec3 up{0, 1, 0};
    float fovY = 1.0f;
    float aspect = 16.0f / 9.0f;
    float nearZ = 0.1f;
    static RenderView lookAt(const Vec3& eye, const Vec3& target, const Vec3& up, float fovY, float aspect, float nearZ);
    static RenderView fromTransform(const Vec3& pos, const Quat& rot, float fovY, float aspect, float nearZ);
};

struct RenderSettings {
    int shadowQuality = 3;  // 0 off, 1 low, 2 medium, 3 high, 4 ultra
    bool ssao = true;
    bool bloom = true;
    int antiAliasing = 2;  // 0 off, 1 FXAA, 2 TAA
    bool motionBlur = false;
    float motionBlurStrength = 0.35f;
    float renderScale = 1.0f;
    float anisotropy = 8.0f;
    float drawDistance = 700.0f;
    float lodBias = 1.0f;
    bool sharpen = true;
    int debugView = 0;  // 0 lit, 1 albedo, 2 normals, 3 roughness, 4 AO, 5 shadow
    bool particles = true;
    float brightness = 1.0f;  // exposure multiplier (settings)
};

struct RenderCallbacks {
    std::function<void(SDL_GPUCommandBuffer*)> prepareOverlay;                 // before any render pass (ImGui upload)
    std::function<void(SDL_GPUCommandBuffer*, SDL_GPURenderPass*)> drawOverlay;  // inside the final pass
};

class Renderer {
public:
    bool init(Window& window);
    void shutdown();

    void applySettings(const RenderSettings& s);
    const RenderSettings& settings() const { return settings_; }

    // renders one frame and presents it. Returns false if the frame was skipped (minimized)
    bool renderFrame(RenderScene& scene, const RenderView& view, const UIDrawList* ui, const RenderCallbacks& cb, float dt);

    void requestScreenshot(const std::string& absPath) { screenshotPath_ = absPath; }
    // camera cut: the next frame starts without temporal history (TAA, motion blur)
    void resetHistory() { taaValid_ = false; hasPrev_ = false; }
    void setFlash(const Vec4& rgbAmount) { flash_ = rgbAmount; }
    SDL_GPUTextureFormat backbufferFormat() const;
    int outputWidth() const { return outW_; }
    int outputHeight() const { return outH_; }
    Vec3 sunDirection() const { return sunDir_; }
    const std::string& lastEnvironment() const { return loadedHdri_; }

private:
    struct DynBuffer {
        GpuBuffer buffer;
        SDL_GPUTransferBuffer* transfer = nullptr;
        uint32_t capacity = 0;
        SDL_GPUBufferUsageFlags usage = 0;
        const char* name = "";
    };
    struct DrawBatch {
        GpuMesh* mesh = nullptr;
        Material* material = nullptr;
        uint32_t firstIndex = 0, indexCount = 0;  // mesh index range
        uint32_t firstVisible = 0, instanceCount = 0;
        int pipe = 0;
        float distance = 0.0f;
    };
    enum Pipe { PipeOpaque = 0, PipeDoubleSided, PipeSkinned, PipeBlend, PipeDecal, PipeCount };

    void createTargets(int w, int h);
    void releaseTargets();
    void createPipelines();
    void loadEnvironment(RenderScene& scene);
    void generateBrdfLut();
    void computeShadowCascades(const RenderView& view, int cascades, int size);
    void cullAndBatch(RenderScene& scene, const RenderView& view);
    void collectView(RenderScene& scene, const Frustum& fr, const Vec3& camPos, float maxDist, bool shadow, uint32_t layerMask,
                     std::vector<uint32_t>& out);
    void buildBatches(RenderScene& scene, const std::vector<uint32_t>& objs, const Vec3& camPos, bool shadow,
                      std::vector<DrawBatch>& opaque, std::vector<DrawBatch>* transparent);
    void uploadDynamic(SDL_GPUCopyPass* pass, DynBuffer& db, const void* data, uint32_t size);
    void ensureDyn(DynBuffer& db, uint32_t size);
    void drawBatches(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd, const std::vector<DrawBatch>& batches, int mode,
                     int cascade);
    void bindMaterial(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd, Material* m, int mode);
    void fullscreen(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, uint32_t mip, GfxPipeline* pipe,
                    const std::vector<SDL_GPUTextureSamplerBinding>& samplers, const void* uniforms, uint32_t uniformSize,
                    bool clear, bool pushFrame = false, uint32_t layer = 0);
    void takeScreenshot(SDL_GPUCommandBuffer*& cmd);

    Window* window_ = nullptr;
    RenderSettings settings_;
    int outW_ = 0, outH_ = 0;  // backbuffer
    int rw_ = 0, rh_ = 0;      // internal render resolution

    // targets
    GpuTexture depth_, normals_, hdr_, hdrTemp_, ldr_, backbuffer_, ssao_, ssaoTemp_;
    GpuTexture taaHistory_[2];  // ping-pong: resolved result of this frame / previous frame
    int taaIndex_ = 0;
    bool taaValid_ = false;
    uint32_t taaFrame_ = 0, taaPhase_ = 0;  // phase 1..8 while TAA runs (jitter / noise index), 0 = off
    std::vector<GpuTexture> bloom_;
    GpuTexture shadowMap_;
    int shadowSize_ = 0, shadowLayers_ = 0;
    GpuTexture envEquirect_, envCube_, brdfLut_;
    int envMips_ = 1;
    std::string loadedHdri_;
    // recently used environments stay resident so switching back (menu shop <-> street) is instant
    struct EnvCacheEntry {
        std::string key, hdri;
        GpuTexture equirect, cube;
        Vec4 sh[9];
        Vec3 sunDir;
        float skyUpLum = 1.0f;
        int mips = 1;
    };
    std::vector<EnvCacheEntry> envCache_;
    std::string envKey_;
    // UI backdrop blur (frosted panels): 1/2, 1/4, 1/8 downsamples of the final image
    GpuTexture backdrop_[3];
    uint64_t loadedEnvVersion_ = ~0ull;
    Vec4 sh_[9];
    Vec3 sunDir_{0.4f, 0.8f, 0.3f};
    float skyUpLum_ = 1.0f;  // luminance of the sky irradiance on an upward facing white surface

    // pipelines
    GfxPipeline* pbr_[PipeCount] = {};
    TexturePtr detailNormal_;
    GfxPipeline* depthPipe_[PipeCount] = {};
    GfxPipeline* shadowPipe_[3] = {};  // static, skinned, masked
    GfxPipeline *sky_ = nullptr, *ssaoPipe_ = nullptr, *ssaoBlur_ = nullptr, *bloomDown_ = nullptr, *bloomUp_ = nullptr;
    GfxPipeline *tonemap_ = nullptr, *fxaa_ = nullptr, *motionBlur_ = nullptr, *debugLines_ = nullptr, *debugLinesNoDepth_ = nullptr;
    GfxPipeline *particlesAlpha_ = nullptr, *particlesAdd_ = nullptr, *ui_ = nullptr, *present_ = nullptr, *prefilter_ = nullptr, *brdfPipe_ = nullptr;
    GfxPipeline* taa_ = nullptr;

    // buffers
    DynBuffer instanceBuf_, visibleBuf_, boneBuf_, lightBuf_, lineBuf_, particleBuf_, uiVertBuf_, uiIndexBuf_;
    uint32_t instanceCapacity_ = 0;

    // per frame
    struct FrameUniforms {
        Mat4 view, proj, viewProj, invViewProj, prevViewProj, invView, invProj;
        Mat4 shadowMatrices[4];
        Vec4 cameraPos, sunDir, sunColor, viewport, cascadeSplits, shadowParams, fogParams, fogColor, misc, envParams,
            cascadeTexel;
        Vec4 sh[9];
        Vec4 extra;  // x = night, y = contact shadows, z = GI volume, w = urban reflection
        Vec4 taa;    // xy = jitter (NDC), z = previous bone palette offset, w = TAA phase (1..8, 0 = off)
    } frame_{};
    Mat4 prevViewProj_;  // unjittered
    Vec3 prevCamPos_, prevCamFwd_{0, 0, -1};
    bool hasPrev_ = false;
    std::vector<Mat4> prevBones_, boneUpload_;  // skinning palette of the previous frame (motion vectors)
    std::vector<uint32_t> visibleIndices_;
    std::vector<DrawBatch> mainBatches_, transparentBatches_, shadowBatches_[4];
    std::vector<uint32_t> scratchObjs_;
    uint32_t frameStamp_ = 1;
    int cascadeCount_ = 0;
    float time_ = 0.0f;
    Vec4 flash_{0, 0, 0, 0};
    std::string screenshotPath_;
    uint32_t lightCount_ = 0;
};

Renderer& renderer();

}  // namespace sw
