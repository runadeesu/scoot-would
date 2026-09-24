// scoot would - shader system
// GLSL (#version 450, Vulkan flavour) sources in shaders/ are compiled at runtime
// with glslang to SPIR-V. For the D3D12 backend SPIR-V is transpiled to HLSL
// (SPIRV-Cross) and compiled to DXBC (d3dcompiler_47) through SDL_shadercross.
// Compiled bytecode is cached on disk; sources are watched for hot reload.
//
// Resource binding convention (SDL GPU):
//   vertex   : set 0 = sampled textures, storage textures, storage buffers ; set 1 = uniform buffers
//   fragment : set 2 = sampled textures, storage textures, storage buffers ; set 3 = uniform buffers
//   compute  : set 0 = sampled textures, ro storage ; set 1 = rw storage ; set 2 = uniforms
#pragma once

#include <SDL3/SDL_gpu.h>

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>

namespace sw {

enum class ShaderStage { Vertex, Fragment, Compute };

struct ShaderKey {
    std::string path;     // relative to shaders/, e.g. "pbr.frag"
    std::string defines;  // "A=1;B" (semicolon separated)
    bool operator==(const ShaderKey& o) const { return path == o.path && defines == o.defines; }
};

struct CompiledShader {
    SDL_GPUShader* shader = nullptr;
    std::vector<std::string> dependencies;  // absolute paths of source + includes
};

struct VertexLayout {
    std::vector<SDL_GPUVertexBufferDescription> buffers;
    std::vector<SDL_GPUVertexAttribute> attributes;
};

struct PipelineDesc {
    std::string name;
    std::string vertex;     // shader file
    std::string fragment;   // shader file ("" = depth only)
    std::string defines;
    VertexLayout layout;
    SDL_GPUPrimitiveType primitive = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    SDL_GPUCullMode cull = SDL_GPU_CULLMODE_BACK;
    SDL_GPUFrontFace frontFace = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    SDL_GPUFillMode fill = SDL_GPU_FILLMODE_FILL;
    bool depthTest = true;
    bool depthWrite = true;
    SDL_GPUCompareOp depthCompare = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;  // reverse Z
    bool depthBias = false;
    float depthBiasConstant = 0.0f, depthBiasSlope = 0.0f;
    bool depthClip = true;
    std::vector<SDL_GPUTextureFormat> colorFormats;
    SDL_GPUTextureFormat depthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    enum class Blend { Opaque, Alpha, Additive, Premultiplied } blend = Blend::Opaque;
    uint8_t colorWriteMask = 0xF;
};

struct GfxPipeline {
    PipelineDesc desc;
    SDL_GPUGraphicsPipeline* handle = nullptr;
    std::vector<std::string> dependencies;
    bool valid() const { return handle != nullptr; }
};

struct ComputePipeline {
    std::string path;
    std::string defines;
    SDL_GPUComputePipeline* handle = nullptr;
    std::vector<std::string> dependencies;
};

class ShaderLibrary {
public:
    bool init();
    void shutdown();

    // create a pipeline that is automatically rebuilt when its shader sources change
    GfxPipeline* createPipeline(const PipelineDesc& desc);
    ComputePipeline* createCompute(const std::string& path, const std::string& defines = "");

    // compile GLSL source file to SPIR-V (words). Returns false and fills error on failure.
    bool compileSpirv(const std::string& path, ShaderStage stage, const std::string& defines,
                      std::vector<uint32_t>& out, std::vector<std::string>& deps, std::string& error);

    // poll source timestamps and rebuild affected pipelines; returns number of reloaded pipelines
    int checkHotReload();
    int reloadAll();
    void setHotReload(bool enabled) { hotReload_ = enabled; }
    bool hotReload() const { return hotReload_; }
    const std::string& lastError() const { return lastError_; }
    size_t pipelineCount() const { return pipelines_.size(); }

private:
    SDL_GPUShader* buildShader(const std::string& path, ShaderStage stage, const std::string& defines,
                               std::vector<std::string>& deps);
    bool buildPipeline(GfxPipeline& p);
    bool buildCompute(ComputePipeline& p);

    std::vector<std::unique_ptr<GfxPipeline>> pipelines_;
    std::vector<std::unique_ptr<ComputePipeline>> computes_;
    std::unordered_map<std::string, int64_t> fileTimes_;
    bool hotReload_ = false;
    double lastPoll_ = 0;
    std::string lastError_;
};

ShaderLibrary& shaders();

}  // namespace sw
