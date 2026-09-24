// scoot would - developer tools (Dear ImGui): debug overlay + profiler (F1), developer console
// (` or F4), scene editor (F2: hierarchy, inspector, move / rotate / scale gizmo, add, duplicate,
// delete, save / load with backup). Not part of the player facing UI.
#pragma once

#include "core/math.h"
#include "render/renderer.h"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

namespace sw {

class Game;
struct Entity;

class DebugUI {
public:
    explicit DebugUI(Game& g) : game_(g) {}
    bool init();
    void shutdown();
    bool processEvent(const SDL_Event& e);  // true = consumed
    void update(float dt);                  // builds the ImGui frame
    void callbacks(RenderCallbacks& cb);
    bool wantsKeyboard() const;
    bool wantsMouse() const;

    // console
    void execute(const std::string& line);
    void print(const std::string& text);

    // editor
    void setEditor(bool on);
    bool editorActive() const { return editor_; }
    void updateEditorCamera(float dt);
    RenderView editorView(float aspect) const;

private:
    void drawOverlay();
    void drawConsole();
    void drawEditor();
    void drawHierarchyNode(Entity& e);
    void drawInspector(Entity& e);
    void pick(float mx, float my);
    void saveScene();

    Game& game_;
    bool ready_ = false;
    bool overlay_ = false, console_ = false, editor_ = false, profiler_ = false;
    bool focusConsoleInput_ = false;
    char input_[256] = {};
    std::vector<std::string> history_;
    int historyPos_ = -1;
    std::vector<std::string> output_;
    // editor state
    uint32_t selected_ = 0;
    int gizmoOp_ = 0;   // 0 translate, 1 rotate, 2 scale
    bool gizmoLocal_ = false;
    Vec3 camPos_{0, 8, 20};
    float camYaw_ = 0.0f, camPitch_ = -0.3f;
    char filter_[64] = {};
    char addPrefab_[64] = {};
    std::string status_;
    float statusTime_ = 0.0f;
    bool frameStarted_ = false;
};

}  // namespace sw
