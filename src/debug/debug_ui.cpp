#include "debug/debug_ui.h"

#include "assets/asset_manager.h"
#include "audio/audio.h"
#include "audio/music.h"
#include "core/engine.h"
#include "core/filesystem.h"
#include "core/log.h"
#include "core/timer.h"
#include "game/game.h"
#include "input/input.h"
#include "physics/physics_world.h"
#include "render/gpu.h"
#include "render/shader.h"
#include "save/save_system.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <cstdio>
#include <sstream>

namespace sw {

bool DebugUI::init() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 6.0f;
    st.FrameRounding = 4.0f;
    st.Colors[ImGuiCol_WindowBg].w = 0.9f;
    if (!ImGui_ImplSDL3_InitForSDLGPU(engine().window().handle())) return false;
    ImGui_ImplSDLGPU3_InitInfo info;
    info.Device = gpu().device();
    info.ColorTargetFormat = renderer().backbufferFormat();
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    if (!ImGui_ImplSDLGPU3_Init(&info)) return false;
    ready_ = true;
    print("scoot would developer console. Type 'help' for commands.");
    return true;
}

void DebugUI::shutdown() {
    if (!ready_) return;
    gpu().waitIdle();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    ready_ = false;
}

bool DebugUI::wantsKeyboard() const { return ready_ && (console_ || editor_) && ImGui::GetIO().WantCaptureKeyboard; }
bool DebugUI::wantsMouse() const { return ready_ && (console_ || editor_ || overlay_) && (ImGui::GetIO().WantCaptureMouse || editor_); }

bool DebugUI::processEvent(const SDL_Event& e) {
    if (!ready_) return false;
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        if (e.key.scancode == SDL_SCANCODE_GRAVE || e.key.scancode == SDL_SCANCODE_F4) {
            console_ = !console_;
            focusConsoleInput_ = console_;
            input().textInputActive = console_;
            if (console_) SDL_StartTextInput(engine().window().handle());
            else SDL_StopTextInput(engine().window().handle());
            return true;
        }
        if (e.key.scancode == SDL_SCANCODE_F1) {
            overlay_ = !overlay_;
            return true;
        }
        if (e.key.scancode == SDL_SCANCODE_F6) {
            profiler_ = !profiler_;
            return true;
        }
    }
    bool active = overlay_ || console_ || editor_ || profiler_;
    ImGui_ImplSDL3_ProcessEvent(&e);
    if (!active) return false;
    ImGuiIO& io = ImGui::GetIO();
    bool kb = e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP || e.type == SDL_EVENT_TEXT_INPUT;
    bool mouse = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP || e.type == SDL_EVENT_MOUSE_WHEEL;
    if (kb && io.WantCaptureKeyboard && (console_ || editor_)) return true;
    if (mouse && io.WantCaptureMouse) return true;
    // editor: click in the viewport selects
    if (editor_ && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && !io.WantCaptureMouse && !ImGuizmo::IsOver()) {
        pick(e.button.x, e.button.y);
        return true;
    }
    return false;
}

void DebugUI::print(const std::string& text) {
    output_.push_back(text);
    if (output_.size() > 400) output_.erase(output_.begin(), output_.begin() + 100);
}

void DebugUI::execute(const std::string& lineIn) {
    std::string line = lineIn;
    while (!line.empty() && line.back() == ' ') line.pop_back();
    if (line.empty()) return;
    print("> " + line);
    history_.push_back(line);
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;
    std::vector<std::string> a;
    for (std::string t; ss >> t;) a.push_back(t);
    auto flag = [&](bool cur) { return a.empty() ? !cur : (a[0] == "1" || a[0] == "on" || a[0] == "true"); };
    Player& p = game_.player();
    if (cmd == "help") {
        print("respawn                 respawn at the last checkpoint (respawn start: at the spawn)");
        print("teleport x y z | label  move the rider");
        print("fps <n|0>               frame limit (0 = unlimited)");
        print("vsync [0|1]             toggle vsync");
        print("debug_physics [0|1]     physics debug drawing (F3)");
        print("reload_scene            reload the current map from disk");
        print("reload_shaders          recompile all shaders (F5)");
        print("reload_materials        reload material json files");
        print("godmode [0|1]           no bails");
        print("timescale <s>           slow motion");
        print("env <day|clear|sunset|overcast|night>");
        print("map <scene.json> | spawn <label> | challenge <id>");
        print("hud [0|1] | camera | screenshot | stats | save | music next | quit");
    } else if (cmd == "respawn") {
        p.respawn(!(a.size() && a[0] == "start"));
    } else if (cmd == "teleport" || cmd == "tp") {
        if (a.size() >= 3) {
            Vec3 pos(std::strtof(a[0].c_str(), nullptr), std::strtof(a[1].c_str(), nullptr), std::strtof(a[2].c_str(), nullptr));
            Quat rot = Quat::angleAxis(std::atan2(-p.scooter.forward().x, -p.scooter.forward().z), Vec3(0, 1, 0));
            p.spawn(pos, rot);
        } else if (a.size() == 1) {
            if (!game_.spawnAt(a[0])) print("no spawn called " + a[0]);
        } else {
            print("usage: teleport x y z | teleport <spawn label>");
        }
    } else if (cmd == "fps") {
        int n = a.empty() ? 0 : std::atoi(a[0].c_str());
        saves().settings().graphics.fpsLimit = n;
        engine().setFpsLimit(n);
        print(n ? "fps limit " + std::to_string(n) : "fps unlimited");
    } else if (cmd == "vsync") {
        bool v = flag(gpu().vsync());
        gpu().setPresentMode(v);
        saves().settings().graphics.vsync = v;
        print(v ? "vsync on" : "vsync off");
    } else if (cmd == "debug_physics") {
        game_.physicsDebug = flag(game_.physicsDebug);
    } else if (cmd == "reload_scene") {
        std::string m = game_.mapPath();
        game_.loadMap(m);
        game_.startFreeRide();
    } else if (cmd == "reload_shaders") {
        shaders().reloadAll();
        print("shaders reloaded");
    } else if (cmd == "reload_materials") {
        print(std::to_string(assets().reloadMaterials()) + " materials reloaded");
    } else if (cmd == "godmode") {
        PlayerSettings s = p.settings;
        s.godMode = flag(s.godMode);
        p.applySettings(s);
        print(s.godMode ? "god mode on" : "god mode off");
    } else if (cmd == "timescale") {
        game_.timeScale = a.empty() ? 1.0f : clampf(std::strtof(a[0].c_str(), nullptr), 0.05f, 4.0f);
        engine().setTimeScale(game_.timeScale);
    } else if (cmd == "env") {
        if (!a.empty()) game_.applyEnvironment(a[0]);
    } else if (cmd == "map") {
        if (!a.empty() && game_.loadMap(a[0])) game_.startFreeRide();
    } else if (cmd == "spawn") {
        if (!a.empty()) game_.spawnAt(a[0]);
    } else if (cmd == "challenge") {
        if (!a.empty() && !game_.startChallenge(a[0])) print("unknown challenge");
        if (a.empty())
            for (auto& d : game_.modes().challenges()) print(d.id + "  " + d.name);
    } else if (cmd == "hud") {
        game_.hudVisible = flag(game_.hudVisible);
    } else if (cmd == "camera") {
        game_.camera().cycleMode();
    } else if (cmd == "screenshot") {
        char name[64];
        std::snprintf(name, sizeof(name), "screenshots/shot_%lld.png", (long long)SDL_GetTicks());
        renderer().requestScreenshot(fs::userPath(name));
        print(std::string("saved ") + fs::userPath(name));
    } else if (cmd == "stats") {
        auto st = assets().stats();
        char b[256];
        std::snprintf(b, sizeof(b), "textures %zu (%.1f MB) materials %zu meshes %zu models %zu sounds %zu entities %zu voices %d", st.textures,
                      double(st.textureBytes) / 1048576.0, st.materials, st.meshes, st.models, st.sounds, game_.scene().entityCount(), audio().activeVoices());
        print(b);
    } else if (cmd == "save") {
        saves().saveData();
        saves().saveSettings();
        print("saved");
    } else if (cmd == "music") {
        music().next();
    } else if (cmd == "quit" || cmd == "exit") {
        game_.requestQuit();
    } else {
        print("unknown command '" + cmd + "' (help)");
    }
}

void DebugUI::setEditor(bool on) {
    editor_ = on;
    if (on) {
        // start the fly camera at the gameplay camera
        camPos_ = game_.camera().position();
        Vec3 f = game_.camera().forward();
        camYaw_ = std::atan2(-f.x, -f.z);
        camPitch_ = std::asin(clampf(f.y, -1.0f, 1.0f));
        game_.scene().clearStaticBatches();
        engine().window().setMouseCaptured(false);
    }
}

void DebugUI::updateEditorCamera(float dt) {
    if (!ready_) return;
    ImGuiIO& io = ImGui::GetIO();
    Input& in = input();
    bool fly = in.mouseDown(SDL_BUTTON_RIGHT) && !io.WantCaptureMouse;
    if (fly) {
        Vec2 d = in.mouseDelta();
        camYaw_ -= d.x * 0.004f;
        camPitch_ = clampf(camPitch_ - d.y * 0.004f, -1.5f, 1.5f);
    }
    if (!io.WantCaptureKeyboard) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        Vec3 fwd(-std::sin(camYaw_) * std::cos(camPitch_), std::sin(camPitch_), -std::cos(camYaw_) * std::cos(camPitch_));
        Vec3 right = cross(fwd, Vec3(0, 1, 0)).normalized();
        float speed = (keys[SDL_SCANCODE_LSHIFT] ? 40.0f : 10.0f) * dt;
        if (fly) {
            if (keys[SDL_SCANCODE_W]) camPos_ += fwd * speed;
            if (keys[SDL_SCANCODE_S]) camPos_ -= fwd * speed;
            if (keys[SDL_SCANCODE_D]) camPos_ += right * speed;
            if (keys[SDL_SCANCODE_A]) camPos_ -= right * speed;
            if (keys[SDL_SCANCODE_E]) camPos_.y += speed;
            if (keys[SDL_SCANCODE_Q]) camPos_.y -= speed;
        } else {
            if (keys[SDL_SCANCODE_W] && !keys[SDL_SCANCODE_LCTRL]) gizmoOp_ = 0;
            if (keys[SDL_SCANCODE_E]) gizmoOp_ = 1;
            if (keys[SDL_SCANCODE_R]) gizmoOp_ = 2;
        }
    }
}

RenderView DebugUI::editorView(float aspect) const {
    Vec3 fwd(-std::sin(camYaw_) * std::cos(camPitch_), std::sin(camPitch_), -std::cos(camYaw_) * std::cos(camPitch_));
    return RenderView::lookAt(camPos_, camPos_ + fwd, Vec3(0, 1, 0), 60.0f * kDeg2Rad, aspect, 0.1f);
}

void DebugUI::pick(float mx, float my) {
    int w, h;
    engine().window().pixelSize(w, h);
    RenderView v = editorView(float(w) / float(std::max(h, 1)));
    float nx = mx / float(w) * 2.0f - 1.0f, ny = 1.0f - my / float(h) * 2.0f;
    float th = std::tan(v.fovY * 0.5f);
    Ray r;
    r.origin = v.position;
    r.dir = (v.forward + v.right * (nx * th * v.aspect) + v.up * (ny * th)).normalized();
    float best = 1e30f;
    uint32_t hit = 0;
    game_.scene().forEach([&](Entity& e) {
        if (!e.meshRenderer || e.meshRenderer->handle == RenderScene::kInvalid) return;
        const RenderObject* o = game_.renderScene().get(e.meshRenderer->handle);
        if (!o || !o->visible) return;
        float t0, t1;
        if (r.intersect(o->worldBounds, t0, t1) && t0 < best) {
            // prefer small objects when the ray starts inside a big one
            float size = (o->worldBounds.max - o->worldBounds.min).length();
            float score = t0 > 0.0f ? t0 : size;
            if (score < best) {
                best = score;
                hit = e.id;
            }
        }
    });
    selected_ = hit;
}

void DebugUI::saveScene() {
    std::string path = fs::resolve(game_.mapPath());
    if (game_.scene().save(path)) {
        status_ = "saved " + game_.mapPath() + " (backup .bak)";
    } else {
        status_ = "save FAILED";
    }
    statusTime_ = 0.0f;
    LOG_INFO("editor: %s", status_.c_str());
}

void DebugUI::update(float dt) {
    if (!ready_) return;
    statusTime_ += dt;
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    frameStarted_ = true;
    if (overlay_) drawOverlay();
    if (profiler_) {
        ImGui::SetNextWindowPos(ImVec2(10, 420), ImGuiCond_FirstUseEver);
        ImGui::Begin("Profiler", &profiler_);
        for (int s = 0; s < int(ProfileSection::Count); ++s)
            ImGui::Text("%-11s %6.2f ms", Profiler::name(ProfileSection(s)), Profiler::average(ProfileSection(s)));
        const auto& hist = Profiler::frameHistory();
        ImGui::PlotLines("frame ms", hist.data(), int(hist.size()), Profiler::frameHistoryOffset(), nullptr, 0.0f, 40.0f, ImVec2(320, 80));
        ImGui::End();
    }
    if (console_) drawConsole();
    if (editor_) drawEditor();
    ImGui::Render();
}

void DebugUI::callbacks(RenderCallbacks& cb) {
    if (!ready_ || !frameStarted_) return;
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd || dd->CmdListsCount == 0) return;
    cb.prepareOverlay = [dd](SDL_GPUCommandBuffer* cmd) { ImGui_ImplSDLGPU3_PrepareDrawData(dd, cmd); };
    cb.drawOverlay = [dd](SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* rp) { ImGui_ImplSDLGPU3_RenderDrawData(dd, cmd, rp); };
}

void DebugUI::drawOverlay() {
    Player& p = game_.player();
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("Debug (F1)", &overlay_, ImGuiWindowFlags_AlwaysAutoResize);
    const auto& rs = Profiler::lastRenderStats();
    ImGui::Text("FPS %.0f   frame %.2f ms", engine().fps(), Profiler::average(ProfileSection::Frame));
    ImGui::Text("physics %.2f ms  gameplay %.2f ms  render %.2f ms  gpu wait %.2f ms", Profiler::average(ProfileSection::Physics),
                Profiler::average(ProfileSection::Gameplay), Profiler::average(ProfileSection::Render), Profiler::average(ProfileSection::GpuWait));
    ImGui::Text("draw calls %u (shadow %u)  triangles %.2f M  instances %u", rs.drawCalls, rs.shadowDrawCalls, double(rs.triangles) / 1e6, rs.instances);
    ImGui::Text("visible %u  culled %u  lights %u", rs.visibleObjects, rs.culledObjects, rs.lights);
    ImGui::Separator();
    Vec3 v = p.velocity(), w = p.scooter.angularVelocity(), n = p.scooter.groundNormal(), pos = p.position();
    const char* states[] = {"Riding", "Air", "Grinding", "Manual", "Bailed"};
    ImGui::Text("state %s%s   surface %s", states[std::clamp(int(p.state()), 0, 4)], p.fakie() ? " (fakie)" : "",
                surfaces().get(p.scooter.groundSurface()).name.c_str());
    ImGui::Text("pos %.2f %.2f %.2f", pos.x, pos.y, pos.z);
    ImGui::Text("speed %.2f m/s (%.1f km/h)", p.speed(), p.speed() * 3.6f);
    ImGui::Text("velocity %.2f %.2f %.2f", v.x, v.y, v.z);
    ImGui::Text("angular velocity %.2f %.2f %.2f", w.x, w.y, w.z);
    ImGui::Text("ground normal %.2f %.2f %.2f  wheels %d%d", n.x, n.y, n.z, p.scooter.frontWheel().contact, p.scooter.rearWheel().contact);
    ImGui::Text("air %.2f s  peak %.2f m  spin %.0f  flip %.0f", p.airTime(), p.maxAirHeight(), p.tricks.spinDegrees(), p.tricks.flipDegrees());
    ImGui::Text("trick %s", p.state() == PlayerState::Air ? p.tricks.currentLabel().c_str() : p.lastTrick().c_str());
    ImGui::Text("combo %d x%.1f  timer %.2f  total %lld", p.combo.comboScore(), p.combo.multiplier(), p.combo.timer(), p.combo.totalScore());
    const LandingInfo& li = p.lastLanding();
    ImGui::Text("last landing %s  angle %.1f  heading %.1f  impact %.1f m/s", landingResultName(li.result), li.rotationError, li.headingError, li.impact);
    if (p.state() == PlayerState::Grinding) ImGui::Text("grind balance %.2f  speed %.2f", p.grind.balance, p.grind.speed);
    if (p.state() == PlayerState::Manual) ImGui::Text("manual balance %.2f", p.manual.balance);
    ImGui::Separator();
    ImGui::Text("audio: %s  voices %d", audio().deviceName().c_str(), audio().activeVoices());
    ImGui::Text("F2 editor  F3 physics debug  F4 console  F5 reload shaders  F6 profiler  F11 screenshot");
    ImGui::End();
}

void DebugUI::drawConsole() {
    int w, h;
    engine().window().pixelSize(w, h);
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(float(w), float(h) * 0.42f), ImGuiCond_Always);
    ImGui::Begin("Console", &console_, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::BeginChild("log", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);
    for (const LogLine& l : Log::recent(80)) {
        ImVec4 c = l.level >= LogLevel::Error ? ImVec4(1, 0.4f, 0.4f, 1) : l.level == LogLevel::Warning ? ImVec4(1, 0.8f, 0.3f, 1) : ImVec4(0.6f, 0.6f, 0.65f, 1);
        ImGui::TextColored(c, "%s", l.text.c_str());
    }
    for (auto& o : output_) ImGui::TextUnformatted(o.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    if (focusConsoleInput_) {
        ImGui::SetKeyboardFocusHere();
        focusConsoleInput_ = false;
    }
    ImGuiInputTextFlags fl = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory;
    auto cbHist = [](ImGuiInputTextCallbackData* d) -> int {
        auto* self = static_cast<DebugUI*>(d->UserData);
        if (self->history_.empty()) return 0;
        if (d->EventKey == ImGuiKey_UpArrow) self->historyPos_ = self->historyPos_ < 0 ? int(self->history_.size()) - 1 : std::max(0, self->historyPos_ - 1);
        else if (d->EventKey == ImGuiKey_DownArrow) self->historyPos_ = std::min(int(self->history_.size()) - 1, self->historyPos_ + 1);
        d->DeleteChars(0, d->BufTextLen);
        if (self->historyPos_ >= 0) d->InsertChars(0, self->history_[size_t(self->historyPos_)].c_str());
        return 0;
    };
    ImGui::PushItemWidth(-1);
    if (ImGui::InputText("##cmd", input_, sizeof(input_), fl, cbHist, this)) {
        execute(input_);
        input_[0] = 0;
        historyPos_ = -1;
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::PopItemWidth();
    ImGui::End();
    if (!console_) {
        input().textInputActive = false;
        SDL_StopTextInput(engine().window().handle());
    }
}

void DebugUI::drawHierarchyNode(Entity& e) {
    std::string label = (e.name.empty() ? e.prefab : e.name) + "##" + std::to_string(e.id);
    if (filter_[0] && label.find(filter_) == std::string::npos && e.children.empty()) return;
    ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_OpenOnArrow | (e.children.empty() ? ImGuiTreeNodeFlags_Leaf : 0) |
                            (selected_ == e.id ? ImGuiTreeNodeFlags_Selected : 0);
    bool open = ImGui::TreeNodeEx(label.c_str(), fl);
    if (ImGui::IsItemClicked()) selected_ = e.id;
    if (open) {
        for (EntityId c : e.children)
            if (Entity* ch = game_.scene().get(c)) drawHierarchyNode(*ch);
        ImGui::TreePop();
    }
}

void DebugUI::drawInspector(Entity& e) {
    Scene& sc = game_.scene();
    char name[128];
    std::snprintf(name, sizeof(name), "%s", e.name.c_str());
    if (ImGui::InputText("name", name, sizeof(name))) e.name = name;
    ImGui::Text("prefab: %s   id %u", e.prefab.c_str(), e.id);
    bool moved = false;
    Vec3 p = e.local.position, eul = quatToEulerDeg(e.local.rotation), s = e.local.scale;
    if (ImGui::DragFloat3("position", &p.x, 0.05f)) { e.local.position = p; moved = true; }
    if (ImGui::DragFloat3("rotation", &eul.x, 0.5f)) { e.local.rotation = Quat::euler(eul.x * kDeg2Rad, eul.y * kDeg2Rad, eul.z * kDeg2Rad); moved = true; }
    if (ImGui::DragFloat3("scale", &s.x, 0.01f, 0.01f, 100.0f)) { e.local.scale = s; moved = true; }
    if (moved) {
        sc.markDirty(e.id);
        sc.updateTransforms();
        sc.syncTransform(e);
    }
    // prefab parameters: numbers, bools, strings and small vectors are editable
    bool rebuild = false;
    if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (auto& [k, v] : e.params.items()) {
            std::string id = k + "##p";
            if (v.is_boolean()) {
                bool b = v.get<bool>();
                if (ImGui::Checkbox(id.c_str(), &b)) { v = b; rebuild = true; }
            } else if (v.is_number_integer()) {
                int i = v.get<int>();
                if (ImGui::DragInt(id.c_str(), &i, 0.1f)) { v = i; rebuild = true; }
            } else if (v.is_number()) {
                float f = v.get<float>();
                if (ImGui::DragFloat(id.c_str(), &f, 0.02f)) { v = f; rebuild = true; }
            } else if (v.is_string()) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%s", v.get<std::string>().c_str());
                if (ImGui::InputText(id.c_str(), buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)) { v = std::string(buf); rebuild = true; }
            } else if (v.is_array() && v.size() >= 2 && v.size() <= 4 && v[0].is_number()) {
                float f[4] = {0, 0, 0, 0};
                for (size_t i = 0; i < v.size(); ++i) f[i] = v[i].get<float>();
                bool ch = v.size() == 2 ? ImGui::DragFloat2(id.c_str(), f, 0.02f) : v.size() == 3 ? ImGui::DragFloat3(id.c_str(), f, 0.02f) : ImGui::DragFloat4(id.c_str(), f, 0.02f);
                if (ch) {
                    for (size_t i = 0; i < v.size(); ++i) v[i] = f[i];
                    rebuild = true;
                }
            } else {
                ImGui::Text("%s: %s", k.c_str(), v.dump().substr(0, 48).c_str());
            }
        }
    }
    char mat[64];
    std::snprintf(mat, sizeof(mat), "%s", e.material.c_str());
    if (ImGui::InputText("material", mat, sizeof(mat), ImGuiInputTextFlags_EnterReturnsTrue)) {
        e.material = mat;
        rebuild = true;
    }
    if (rebuild) {
        sc.rebuild(e);
        sc.revision++;
    }
}

void DebugUI::drawEditor() {
    Scene& sc = game_.scene();
    int w, h;
    engine().window().pixelSize(w, h);
    // toolbar
    ImGui::SetNextWindowPos(ImVec2(float(w) * 0.5f - 330, 8), ImGuiCond_Always);
    ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
    if (ImGui::Button("Save (Ctrl+S)")) saveScene();
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        std::string m = game_.mapPath();
        game_.loadMap(m);
        game_.scene().setEditorMode(true);
        game_.scene().clearStaticBatches();
        selected_ = 0;
    }
    ImGui::SameLine();
    ImGui::RadioButton("Move (W)", &gizmoOp_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Rotate (E)", &gizmoOp_, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Scale (R)", &gizmoOp_, 2);
    ImGui::SameLine();
    ImGui::Checkbox("Local", &gizmoLocal_);
    ImGui::SameLine();
    if (ImGui::Button("Play here (F2)")) {
        Vec3 fwd(-std::sin(camYaw_), 0, -std::cos(camYaw_));
        Vec3 hitPos = camPos_ + fwd * 4.0f;
        RayHit rh;
        if (physics().raycast(camPos_ + fwd * 4.0f + Vec3(0, 50, 0), Vec3(0, -1, 0), 200.0f, rh)) hitPos = rh.point;
        game_.openEditor(false);
        game_.player().spawn(hitPos + Vec3(0, 0.2f, 0), Quat::angleAxis(camYaw_, Vec3(0, 1, 0)));
    }
    if (statusTime_ < 4.0f) ImGui::TextColored(ImVec4(0.5f, 1, 0.5f, 1), "%s", status_.c_str());
    ImGui::TextDisabled("RMB + WASD/QE fly (Shift fast)  click select  Ctrl+D duplicate  Del delete");
    ImGui::End();

    // hierarchy
    ImGui::SetNextWindowPos(ImVec2(8, 90), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, float(h) - 110), ImGuiCond_FirstUseEver);
    ImGui::Begin("Hierarchy");
    ImGui::InputText("filter", filter_, sizeof(filter_));
    ImGui::Text("%zu entities", sc.entityCount());
    if (ImGui::BeginCombo("add", addPrefab_[0] ? addPrefab_ : "prefab...")) {
        auto list = prefabs().list();
        std::sort(list.begin(), list.end(), [](auto& a, auto& b) { return a.second == b.second ? a.first < b.first : a.second < b.second; });
        std::string cat;
        for (auto& [name, category] : list) {
            if (category != cat) {
                ImGui::TextDisabled("%s", category.c_str());
                cat = category;
            }
            if (ImGui::Selectable(name.c_str())) {
                std::snprintf(addPrefab_, sizeof(addPrefab_), "%s", name.c_str());
                Vec3 fwd(-std::sin(camYaw_) * std::cos(camPitch_), std::sin(camPitch_), -std::cos(camYaw_) * std::cos(camPitch_));
                Vec3 at = camPos_ + fwd * 10.0f;
                RayHit rh;
                if (physics().raycast(camPos_, fwd, 60.0f, rh)) at = rh.point;
                Json j = {{"name", name}, {"prefab", name}, {"params", prefabs().defaults(name)}, {"position", {at.x, at.y, at.z}}};
                if (Entity* ne = sc.entityFromJson(j)) {
                    sc.updateTransforms();
                    sc.instantiate(*ne);
                    selected_ = ne->id;
                    sc.revision++;
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::Separator();
    ImGui::BeginChild("tree");
    for (EntityId id : sc.roots())
        if (Entity* e = sc.get(id)) drawHierarchyNode(*e);
    ImGui::EndChild();
    ImGui::End();

    // inspector + gizmo
    Entity* sel = selected_ ? sc.get(selected_) : nullptr;
    ImGui::SetNextWindowPos(ImVec2(float(w) - 408, 90), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 560), ImGuiCond_FirstUseEver);
    ImGui::Begin("Inspector");
    if (sel) {
        drawInspector(*sel);
        ImGui::Separator();
        if (ImGui::Button("Duplicate (Ctrl+D)")) {
            if (Entity* d = sc.duplicate(sel->id)) selected_ = d->id;
            sc.revision++;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete (Del)")) {
            sc.destroy(sel->id);
            selected_ = 0;
            sel = nullptr;
            sc.revision++;
        }
    } else {
        ImGui::TextDisabled("nothing selected");
    }
    ImGui::End();

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_S) && io.KeyCtrl) saveScene();
        if (sel && ImGui::IsKeyPressed(ImGuiKey_D) && io.KeyCtrl) {
            if (Entity* d = sc.duplicate(sel->id)) selected_ = d->id;
            sc.revision++;
        }
        if (sel && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            sc.destroy(sel->id);
            selected_ = 0;
            sel = nullptr;
            sc.revision++;
        }
    }
    if (sel) {
        float aspect = float(w) / float(std::max(h, 1));
        RenderView v = editorView(aspect);
        Mat4 proj = Mat4::perspective(v.fovY, aspect, 0.1f, 2000.0f);
        Mat4 m = sel->world;
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
        ImGuizmo::SetRect(0, 0, float(w), float(h));
        ImGuizmo::OPERATION op = gizmoOp_ == 0 ? ImGuizmo::TRANSLATE : gizmoOp_ == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
        if (ImGuizmo::Manipulate(v.view.m, proj.m, op, gizmoLocal_ ? ImGuizmo::LOCAL : ImGuizmo::WORLD, m.m)) {
            // back to the parent's space
            Mat4 parentWorld = Mat4::identity();
            if (sel->parent)
                if (Entity* par = sc.get(sel->parent)) parentWorld = par->world;
            Mat4 local = parentWorld.inverse() * m;
            float t[3], r[3], s[3];
            ImGuizmo::DecomposeMatrixToComponents(local.m, t, r, s);
            sel->local.position = Vec3(t[0], t[1], t[2]);
            sel->local.rotation = Quat::fromMat(local);
            sel->local.scale = Vec3(s[0], s[1], s[2]);
            sc.markDirty(sel->id);
            sc.updateTransforms();
            sc.syncTransform(*sel);
        }
        // selection bounds
        if (sel->meshRenderer && sel->meshRenderer->handle != RenderScene::kInvalid)
            if (const RenderObject* o = game_.renderScene().get(sel->meshRenderer->handle)) {
                AABB b = o->worldBounds;
                ImDrawList* dl = ImGui::GetBackgroundDrawList();
                Vec3 c[8];
                for (int i = 0; i < 8; ++i) c[i] = Vec3(i & 1 ? b.max.x : b.min.x, i & 2 ? b.max.y : b.min.y, i & 4 ? b.max.z : b.min.z);
                const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                for (auto& e : edges) {
                    Vec4 a = v.viewProj * Vec4(c[e[0]], 1.0f), bb = v.viewProj * Vec4(c[e[1]], 1.0f);
                    if (a.w <= 0.1f || bb.w <= 0.1f) continue;
                    ImVec2 pa((a.x / a.w * 0.5f + 0.5f) * float(w), (0.5f - a.y / a.w * 0.5f) * float(h));
                    ImVec2 pb((bb.x / bb.w * 0.5f + 0.5f) * float(w), (0.5f - bb.y / bb.w * 0.5f) * float(h));
                    dl->AddLine(pa, pb, IM_COL32(255, 200, 30, 200), 1.5f);
                }
            }
    }
}

}  // namespace sw
