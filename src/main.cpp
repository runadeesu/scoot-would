// temporary smoke test for toolchain validation
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "core/log.h"
#include "core/filesystem.h"
#include "render/gpu.h"
#include "render/shader.h"
#include "platform/window.h"
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <stb_image_write.h>
#include <vector>
#include <cstring>

using namespace sw;

int main(int argc, char** argv) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
    fs::init();
    Log::init(fs::userPath("logs/smoke.log"));
    LOG_INFO("data root %s", fs::dataRoot().c_str());
    Window win;
    if (!win.create("scoot would", 1280, 720, WindowMode::Windowed)) return 1;
    shaders().init();
    std::string drv = argc > 1 ? argv[1] : "";
    if (!gpu().init(win.handle(), drv, true, false)) return 2;
    PipelineDesc d;
    d.name = "test";
    d.vertex = "test.vert";
    d.fragment = "test.frag";
    d.cull = SDL_GPU_CULLMODE_NONE;
    d.depthTest = false; d.depthWrite = false;
    d.colorFormats = {gpu().swapchainFormat()};
    GfxPipeline* p = shaders().createPipeline(d);
    LOG_INFO("pipeline valid: %d", p->valid());

    ImGui::CreateContext();
    ImGui_ImplSDL3_InitForSDLGPU(win.handle());
    ImGui_ImplSDLGPU3_InitInfo ii{};
    ii.Device = gpu().device();
    ii.ColorTargetFormat = gpu().swapchainFormat();
    ii.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    ImGui_ImplSDLGPU3_Init(&ii);

    GpuTexture off = gpu().createTexture2D(1280, 720, gpu().swapchainFormat(), SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, 1, "off");
    for (int frame = 0; frame < 30; ++frame) {
        SDL_Event e; while (SDL_PollEvent(&e)) ImGui_ImplSDL3_ProcessEvent(&e);
        ImGui_ImplSDLGPU3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
        ImGui::Begin("scoot would"); ImGui::Text("backend: %s", gpu().driverName().c_str()); ImGui::End();
        ImGui::Render();
        SDL_GPUCommandBuffer* cmd = gpu().beginFrame();
        uint32_t w, h;
        SDL_GPUTexture* sc = gpu().acquireSwapchain(cmd, &w, &h);
        SDL_GPUTexture* target = frame == 29 ? off.handle : sc;
        if (target) {
            ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmd);
            SDL_GPUColorTargetInfo ct{}; ct.texture = target; ct.clear_color = {0.1f, 0.12f, 0.15f, 1}; ct.load_op = SDL_GPU_LOADOP_CLEAR; ct.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
            if (p->valid()) {
                SDL_BindGPUGraphicsPipeline(rp, p->handle);
                float off4[4] = {0.1f, 0.0f, 0, 0};
                SDL_PushGPUVertexUniformData(cmd, 0, off4, 16);
                SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
            }
            ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmd, rp);
            SDL_EndGPURenderPass(rp);
        }
        if (frame == 29) {
            SDL_GPUTransferBufferCreateInfo ti{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, 1280*720*4, 0};
            SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(gpu().device(), &ti);
            SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
            SDL_GPUTextureRegion src{}; src.texture = off.handle; src.w = 1280; src.h = 720; src.d = 1;
            SDL_GPUTextureTransferInfo dst{}; dst.transfer_buffer = tb;
            SDL_DownloadFromGPUTexture(cp, &src, &dst);
            SDL_EndGPUCopyPass(cp);
            SDL_GPUFence* f = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
            SDL_WaitForGPUFences(gpu().device(), true, &f, 1);
            SDL_ReleaseGPUFence(gpu().device(), f);
            uint8_t* px = (uint8_t*)SDL_MapGPUTransferBuffer(gpu().device(), tb, false);
            std::vector<uint8_t> img(px, px + 1280*720*4);
            SDL_UnmapGPUTransferBuffer(gpu().device(), tb);
            if (gpu().swapchainFormat() == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM) for (size_t i = 0; i < img.size(); i += 4) std::swap(img[i], img[i+2]);
            stbi_write_png(fs::userPath("smoke.png").c_str(), 1280, 720, 4, img.data(), 1280*4);
            LOG_INFO("wrote %s", fs::userPath("smoke.png").c_str());
        } else gpu().endFrame(cmd);
    }
    gpu().waitIdle();
    ImGui_ImplSDLGPU3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
    shaders().shutdown();
    gpu().release(off);
    gpu().shutdown();
    win.destroy();
    Log::shutdown();
    SDL_Quit();
    return 0;
}
