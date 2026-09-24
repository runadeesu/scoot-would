#include "render/shader.h"
#include "render/gpu.h"
#include "core/filesystem.h"
#include "core/log.h"
#include "core/timer.h"

#include <SDL3_shadercross/SDL_shadercross.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>

#include <cstring>
#include <sstream>
#include <set>
#include <unordered_map>
#include <algorithm>

namespace sw {

ShaderLibrary& shaders() {
    static ShaderLibrary lib;
    return lib;
}

namespace {

constexpr uint32_t kCacheVersion = 3;

class FileIncluder : public glslang::TShader::Includer {
public:
    std::vector<std::string>* deps = nullptr;

    IncludeResult* includeLocal(const char* headerName, const char* includerName, size_t depth) override {
        (void)includerName;
        (void)depth;
        std::string abs = fs::resolve(std::string("shaders/") + headerName);
        auto text = fs::readText(abs);
        if (!text) return nullptr;
        if (deps) deps->push_back(abs);
        auto* holder = new std::string(std::move(*text));
        return new IncludeResult(headerName, holder->data(), holder->size(), holder);
    }
    IncludeResult* includeSystem(const char* headerName, const char* includerName, size_t depth) override {
        return includeLocal(headerName, includerName, depth);
    }
    void releaseInclude(IncludeResult* result) override {
        if (!result) return;
        delete static_cast<std::string*>(result->userData);
        delete result;
    }
};

std::string definesToPreamble(const std::string& defines) {
    std::string out;
    std::stringstream ss(defines);
    std::string item;
    while (std::getline(ss, item, ';')) {
        if (item.empty()) continue;
        size_t eq = item.find('=');
        if (eq == std::string::npos)
            out += "#define " + item + " 1\n";
        else
            out += "#define " + item.substr(0, eq) + " " + item.substr(eq + 1) + "\n";
    }
    return out;
}

SDL_ShaderCross_ShaderStage toCrossStage(ShaderStage s) {
    switch (s) {
        case ShaderStage::Vertex: return SDL_SHADERCROSS_SHADERSTAGE_VERTEX;
        case ShaderStage::Fragment: return SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT;
        default: return SDL_SHADERCROSS_SHADERSTAGE_COMPUTE;
    }
}

// Highest uniform buffer binding declared in a descriptor set (+1). Reflection only reports
// *active* resources; SDL builds the pipeline layout from counts, so a shader that declares
// binding 1 but never reads binding 0 would otherwise get an invalid layout.
uint32_t uniformBufferSlots(const std::vector<uint32_t>& spv, uint32_t wantSet) {
    if (spv.size() < 5) return 0;
    std::unordered_map<uint32_t, uint32_t> setOf, bindingOf, pointee;
    std::unordered_map<uint32_t, bool> isBlock;
    std::vector<std::pair<uint32_t, uint32_t>> uniformVars;  // (var id, pointer type)
    for (size_t i = 5; i < spv.size();) {
        uint32_t w = spv[i];
        uint32_t op = w & 0xffff, count = w >> 16;
        if (count == 0) break;
        if (op == 71 && count >= 3) {  // OpDecorate
            uint32_t target = spv[i + 1], deco = spv[i + 2];
            if (deco == 34 && count >= 4) setOf[target] = spv[i + 3];
            if (deco == 33 && count >= 4) bindingOf[target] = spv[i + 3];
            if (deco == 2) isBlock[target] = true;
        } else if (op == 32 && count >= 4) {  // OpTypePointer
            pointee[spv[i + 1]] = spv[i + 3];
        } else if (op == 59 && count >= 4) {  // OpVariable
            if (spv[i + 3] == 2) uniformVars.push_back({spv[i + 2], spv[i + 1]});
        }
        i += count;
    }
    uint32_t slots = 0;
    for (auto& [var, ptr] : uniformVars) {
        auto pt = pointee.find(ptr);
        if (pt == pointee.end() || !isBlock.count(pt->second)) continue;
        auto st = setOf.find(var);
        auto bd = bindingOf.find(var);
        if (st == setOf.end() || bd == bindingOf.end() || st->second != wantSet) continue;
        slots = std::max(slots, bd->second + 1);
    }
    return slots;
}

}  // namespace

bool ShaderLibrary::init() {
    if (!glslang::InitializeProcess()) {
        LOG_ERROR("shader: glslang init failed");
        return false;
    }
    if (!SDL_ShaderCross_Init()) LOG_WARN("shader: SDL_shadercross init failed: %s", SDL_GetError());
    return true;
}

void ShaderLibrary::shutdown() {
    SDL_GPUDevice* dev = gpu().device();
    for (auto& p : pipelines_)
        if (p->handle) SDL_ReleaseGPUGraphicsPipeline(dev, p->handle);
    for (auto& c : computes_)
        if (c->handle) SDL_ReleaseGPUComputePipeline(dev, c->handle);
    pipelines_.clear();
    computes_.clear();
    SDL_ShaderCross_Quit();
    glslang::FinalizeProcess();
}

bool ShaderLibrary::compileSpirv(const std::string& path, ShaderStage stage, const std::string& defines,
                                 std::vector<uint32_t>& out, std::vector<std::string>& deps, std::string& error) {
    std::string abs = fs::resolve("shaders/" + path);
    auto text = fs::readText(abs);
    if (!text) {
        error = "missing shader source " + abs;
        return false;
    }
    deps.push_back(abs);

    EShLanguage lang = stage == ShaderStage::Vertex ? EShLangVertex : stage == ShaderStage::Fragment ? EShLangFragment : EShLangCompute;
    glslang::TShader sh(lang);
    const char* src = text->c_str();
    const char* names = path.c_str();
    sh.setStringsWithLengthsAndNames(&src, nullptr, &names, 1);
    std::string preamble = "#extension GL_GOOGLE_include_directive : require\n" + definesToPreamble(defines);
    if (stage == ShaderStage::Vertex) preamble += "#define VERTEX_SHADER 1\n";
    if (stage == ShaderStage::Fragment) preamble += "#define FRAGMENT_SHADER 1\n";
    if (stage == ShaderStage::Compute) preamble += "#define COMPUTE_SHADER 1\n";
    sh.setPreamble(preamble.c_str());
    sh.setEntryPoint("main");
    sh.setEnvInput(glslang::EShSourceGlsl, lang, glslang::EShClientVulkan, 100);
    sh.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    sh.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);

    FileIncluder includer;
    includer.deps = &deps;
    EShMessages msgs = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
    if (!sh.parse(GetDefaultResources(), 450, false, msgs, includer)) {
        error = std::string(path) + ":\n" + sh.getInfoLog();
        return false;
    }
    glslang::TProgram prog;
    prog.addShader(&sh);
    if (!prog.link(msgs)) {
        error = std::string(path) + " (link):\n" + prog.getInfoLog();
        return false;
    }
    glslang::SpvOptions opt;
    opt.generateDebugInfo = false;
    opt.disableOptimizer = true;
    opt.validate = false;
    out.clear();
    glslang::GlslangToSpv(*prog.getIntermediate(lang), out, &opt);
    return !out.empty();
}

SDL_GPUShader* ShaderLibrary::buildShader(const std::string& path, ShaderStage stage, const std::string& defines,
                                          std::vector<std::string>& deps) {
    std::vector<uint32_t> spirv;
    std::string error;
    if (!compileSpirv(path, stage, defines, spirv, deps, error)) {
        lastError_ = error;
        LOG_ERROR("shader compile error: %s", error.c_str());
        return nullptr;
    }
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(spirv.data());
    size_t byteSize = spirv.size() * 4;

    SDL_ShaderCross_GraphicsShaderMetadata* meta = SDL_ShaderCross_ReflectGraphicsSPIRV(bytes, byteSize, 0);
    if (!meta) {
        lastError_ = path + ": reflection failed";
        LOG_ERROR("shader: reflection failed for %s: %s", path.c_str(), SDL_GetError());
        return nullptr;
    }

    SDL_GPUShaderCreateInfo ci{};
    ci.stage = stage == ShaderStage::Vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    ci.num_samplers = meta->resource_info.num_samplers;
    ci.num_storage_textures = meta->resource_info.num_storage_textures;
    ci.num_storage_buffers = meta->resource_info.num_storage_buffers;
    ci.num_uniform_buffers = std::max(meta->resource_info.num_uniform_buffers,
                                      uniformBufferSlots(spirv, stage == ShaderStage::Vertex ? 1u : 3u));
    SDL_free(meta);

    SDL_GPUDevice* dev = gpu().device();
    SDL_GPUShaderFormat formats = gpu().shaderFormats();
    SDL_GPUShader* shader = nullptr;
    std::vector<uint8_t> dxbc;

    if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
        ci.code = bytes;
        ci.code_size = byteSize;
        ci.entrypoint = "main";
        shader = SDL_CreateGPUShader(dev, &ci);
    } else if (formats & SDL_GPU_SHADERFORMAT_DXBC) {
        // DXBC cache keyed by the SPIR-V hash (d3dcompiler is the slow part)
        uint64_t h = fs::hash64(bytes, byteSize);
        h = fs::hash64(&kCacheVersion, sizeof(kCacheVersion), h);
        char name[64];
        snprintf(name, sizeof(name), "shadercache/%016llx.dxbc", (unsigned long long)h);
        std::string cachePath = fs::userPath(name);
        if (auto cached = fs::readBinary(cachePath); cached && cached->size() > 4) {
            dxbc = std::move(*cached);
        } else {
            SDL_ShaderCross_SPIRV_Info info{};
            info.bytecode = bytes;
            info.bytecode_size = byteSize;
            info.entrypoint = "main";
            info.shader_stage = toCrossStage(stage);
            size_t size = 0;
            void* code = SDL_ShaderCross_CompileDXBCFromSPIRV(&info, &size);
            if (!code) {
                lastError_ = path + ": DXBC compile failed: " + SDL_GetError();
                LOG_ERROR("shader: %s", lastError_.c_str());
                return nullptr;
            }
            dxbc.assign(static_cast<uint8_t*>(code), static_cast<uint8_t*>(code) + size);
            SDL_free(code);
            fs::writeBinary(cachePath, dxbc.data(), dxbc.size());
        }
        ci.format = SDL_GPU_SHADERFORMAT_DXBC;
        ci.code = dxbc.data();
        ci.code_size = dxbc.size();
        ci.entrypoint = "main";
        shader = SDL_CreateGPUShader(dev, &ci);
    } else {
        lastError_ = "no supported shader format";
    }
    if (!shader) {
        lastError_ = path + ": SDL_CreateGPUShader failed: " + SDL_GetError();
        LOG_ERROR("shader: %s", lastError_.c_str());
    }
    return shader;
}

bool ShaderLibrary::buildPipeline(GfxPipeline& p) {
    const PipelineDesc& d = p.desc;
    std::vector<std::string> deps;
    SDL_GPUShader* vs = buildShader(d.vertex, ShaderStage::Vertex, d.defines, deps);
    SDL_GPUShader* fs_ = nullptr;
    if (!d.fragment.empty()) fs_ = buildShader(d.fragment, ShaderStage::Fragment, d.defines, deps);
    SDL_GPUDevice* dev = gpu().device();
    if (!vs || (!d.fragment.empty() && !fs_)) {
        if (vs) SDL_ReleaseGPUShader(dev, vs);
        if (fs_) SDL_ReleaseGPUShader(dev, fs_);
        p.dependencies = deps;
        return false;
    }
    if (d.fragment.empty()) {
        // SDL requires a fragment shader; use a minimal depth-only one
        fs_ = buildShader("depth_only.frag", ShaderStage::Fragment, d.defines, deps);
    }

    SDL_GPUGraphicsPipelineCreateInfo ci{};
    ci.vertex_shader = vs;
    ci.fragment_shader = fs_;
    ci.vertex_input_state.vertex_buffer_descriptions = d.layout.buffers.data();
    ci.vertex_input_state.num_vertex_buffers = uint32_t(d.layout.buffers.size());
    ci.vertex_input_state.vertex_attributes = d.layout.attributes.data();
    ci.vertex_input_state.num_vertex_attributes = uint32_t(d.layout.attributes.size());
    ci.primitive_type = d.primitive;
    ci.rasterizer_state.fill_mode = d.fill;
    ci.rasterizer_state.cull_mode = d.cull;
    ci.rasterizer_state.front_face = d.frontFace;
    ci.rasterizer_state.enable_depth_bias = d.depthBias;
    ci.rasterizer_state.depth_bias_constant_factor = d.depthBiasConstant;
    ci.rasterizer_state.depth_bias_slope_factor = d.depthBiasSlope;
    ci.rasterizer_state.enable_depth_clip = d.depthClip;
    ci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    ci.depth_stencil_state.enable_depth_test = d.depthTest;
    ci.depth_stencil_state.enable_depth_write = d.depthWrite;
    ci.depth_stencil_state.compare_op = d.depthCompare;

    std::vector<SDL_GPUColorTargetDescription> colors(d.colorFormats.size());
    for (size_t i = 0; i < colors.size(); ++i) {
        colors[i].format = d.colorFormats[i];
        SDL_GPUColorTargetBlendState& b = colors[i].blend_state;
        b.color_write_mask = d.colorWriteMask;
        b.enable_color_write_mask = d.colorWriteMask != 0xF;
        switch (d.blend) {
            case PipelineDesc::Blend::Opaque: b.enable_blend = false; break;
            case PipelineDesc::Blend::Alpha:
                b.enable_blend = true;
                b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                b.color_blend_op = SDL_GPU_BLENDOP_ADD;
                b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
                break;
            case PipelineDesc::Blend::Premultiplied:
                b.enable_blend = true;
                b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                b.color_blend_op = SDL_GPU_BLENDOP_ADD;
                b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
                break;
            case PipelineDesc::Blend::Additive:
                b.enable_blend = true;
                b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                b.color_blend_op = SDL_GPU_BLENDOP_ADD;
                b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
                break;
        }
    }
    ci.target_info.color_target_descriptions = colors.data();
    ci.target_info.num_color_targets = uint32_t(colors.size());
    ci.target_info.depth_stencil_format = d.depthFormat;
    ci.target_info.has_depth_stencil_target = d.depthFormat != SDL_GPU_TEXTUREFORMAT_INVALID;

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, d.name.c_str());
    ci.props = props;
    SDL_GPUGraphicsPipeline* pipe = SDL_CreateGPUGraphicsPipeline(dev, &ci);
    SDL_DestroyProperties(props);
    SDL_ReleaseGPUShader(dev, vs);
    if (fs_) SDL_ReleaseGPUShader(dev, fs_);

    p.dependencies = deps;
    if (!pipe) {
        lastError_ = d.name + ": pipeline creation failed: " + SDL_GetError();
        LOG_ERROR("shader: %s", lastError_.c_str());
        return false;
    }
    if (p.handle) SDL_ReleaseGPUGraphicsPipeline(dev, p.handle);
    p.handle = pipe;
    return true;
}

bool ShaderLibrary::buildCompute(ComputePipeline& p) {
    std::vector<uint32_t> spirv;
    std::vector<std::string> deps;
    std::string error;
    if (!compileSpirv(p.path, ShaderStage::Compute, p.defines, spirv, deps, error)) {
        p.dependencies = deps;
        lastError_ = error;
        LOG_ERROR("shader compile error: %s", error.c_str());
        return false;
    }
    p.dependencies = deps;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(spirv.data());
    size_t byteSize = spirv.size() * 4;
    SDL_ShaderCross_ComputePipelineMetadata* meta = SDL_ShaderCross_ReflectComputeSPIRV(bytes, byteSize, 0);
    if (!meta) return false;
    SDL_GPUDevice* dev = gpu().device();
    SDL_GPUComputePipeline* pipe = nullptr;
    if (gpu().shaderFormats() & SDL_GPU_SHADERFORMAT_SPIRV) {
        SDL_GPUComputePipelineCreateInfo ci{};
        ci.code = bytes;
        ci.code_size = byteSize;
        ci.entrypoint = "main";
        ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
        ci.num_samplers = meta->num_samplers;
        ci.num_readonly_storage_textures = meta->num_readonly_storage_textures;
        ci.num_readonly_storage_buffers = meta->num_readonly_storage_buffers;
        ci.num_readwrite_storage_textures = meta->num_readwrite_storage_textures;
        ci.num_readwrite_storage_buffers = meta->num_readwrite_storage_buffers;
        ci.num_uniform_buffers = meta->num_uniform_buffers;
        ci.threadcount_x = meta->threadcount_x;
        ci.threadcount_y = meta->threadcount_y;
        ci.threadcount_z = meta->threadcount_z;
        pipe = SDL_CreateGPUComputePipeline(dev, &ci);
    } else {
        SDL_ShaderCross_SPIRV_Info info{};
        info.bytecode = bytes;
        info.bytecode_size = byteSize;
        info.entrypoint = "main";
        info.shader_stage = SDL_SHADERCROSS_SHADERSTAGE_COMPUTE;
        pipe = SDL_ShaderCross_CompileComputePipelineFromSPIRV(dev, &info, meta, 0);
    }
    SDL_free(meta);
    if (!pipe) {
        lastError_ = p.path + ": compute pipeline failed: " + SDL_GetError();
        LOG_ERROR("shader: %s", lastError_.c_str());
        return false;
    }
    if (p.handle) SDL_ReleaseGPUComputePipeline(dev, p.handle);
    p.handle = pipe;
    return true;
}

GfxPipeline* ShaderLibrary::createPipeline(const PipelineDesc& desc) {
    auto p = std::make_unique<GfxPipeline>();
    p->desc = desc;
    Timer t;
    buildPipeline(*p);
    LOG_TRACE("shader: pipeline '%s' built in %.1f ms", desc.name.c_str(), t.milliseconds());
    for (auto& dep : p->dependencies) fileTimes_[dep] = fs::modifiedTime(dep);
    pipelines_.push_back(std::move(p));
    return pipelines_.back().get();
}

ComputePipeline* ShaderLibrary::createCompute(const std::string& path, const std::string& defines) {
    auto p = std::make_unique<ComputePipeline>();
    p->path = path;
    p->defines = defines;
    buildCompute(*p);
    for (auto& dep : p->dependencies) fileTimes_[dep] = fs::modifiedTime(dep);
    computes_.push_back(std::move(p));
    return computes_.back().get();
}

int ShaderLibrary::checkHotReload() {
    if (!hotReload_) return 0;
    double now = Timer::now();
    if (now - lastPoll_ < 0.5) return 0;
    lastPoll_ = now;
    std::set<std::string> changed;
    for (auto& [path, time] : fileTimes_) {
        int64_t t = fs::modifiedTime(path);
        if (t != time) {
            time = t;
            changed.insert(path);
        }
    }
    if (changed.empty()) return 0;
    gpu().waitIdle();
    int count = 0;
    for (auto& p : pipelines_) {
        bool affected = p->dependencies.empty();
        for (auto& d : p->dependencies)
            if (changed.count(d)) affected = true;
        if (affected && buildPipeline(*p)) ++count;
        for (auto& dep : p->dependencies) fileTimes_[dep] = fs::modifiedTime(dep);
    }
    for (auto& c : computes_) {
        bool affected = c->dependencies.empty();
        for (auto& d : c->dependencies)
            if (changed.count(d)) affected = true;
        if (affected && buildCompute(*c)) ++count;
    }
    LOG_INFO("shader: hot reloaded %d pipelines", count);
    return count;
}

int ShaderLibrary::reloadAll() {
    gpu().waitIdle();
    int count = 0;
    for (auto& p : pipelines_)
        if (buildPipeline(*p)) ++count;
    for (auto& c : computes_)
        if (buildCompute(*c)) ++count;
    LOG_INFO("shader: reloaded %d pipelines", count);
    return count;
}

}  // namespace sw
