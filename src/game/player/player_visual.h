// scoot would - player visuals: scooter parts (separate transforms for tricks) + rider (skinned glTF
// model driven by the rider animator, with a procedural mannequin fallback) + bail ragdoll mapping
#pragma once

#include "animation/animation.h"
#include "core/math.h"
#include "game/player/rider_blueprint.h"
#include "render/render_scene.h"

#include <memory>
#include <string>
#include <vector>

namespace sw {

class Player;
struct Model;
class RiderAnimator;

struct Customization {
    // rider
    int top = 0;     // 0 t-shirt, 1 hoodie, 2 tank top
    int pants = 0;   // 0 jeans, 1 shorts
    int shoes = 0;
    int helmet = 1;  // 0 none, 1 helmet, 2 cap
    Vec3 skinColor{0.58f, 0.36f, 0.25f};
    Vec3 topColor{0.85f, 0.2f, 0.18f};
    Vec3 pantsColor{0.18f, 0.22f, 0.32f};
    Vec3 shoesColor{0.95f, 0.95f, 0.95f};
    Vec3 helmetColor{0.1f, 0.1f, 0.12f};
    // scooter
    int deck = 0;    // shape variant
    int bars = 0;    // 0 standard, 1 tall, 2 wide
    int wheels = 0;  // core style
    Vec3 deckColor{0.12f, 0.12f, 0.14f};
    Vec3 barsColor{0.85f, 0.85f, 0.88f};
    Vec3 wheelColor{0.95f, 0.95f, 0.95f};
    Vec3 coreColor{0.1f, 0.1f, 0.1f};
    Vec3 gripColor{0.08f, 0.08f, 0.08f};
    Vec3 clampColor{0.75f, 0.1f, 0.12f};
};

bool riderPartVisible(const std::string& meshName, const Customization& c);

class PlayerVisual {
public:
    PlayerVisual();
    ~PlayerVisual();
    void create(RenderScene& rs);
    void destroy();
    void applyCustomization(const Customization& c);
    const Customization& customization() const { return custom_; }
    // per frame, after the player simulation
    void update(float dt, float alpha, Player& player);
    void setVisible(bool v);
    void setRiderVisible(bool v);  // scooter only (photo mode, showroom shots)
    void setGoofy(bool g) { goofy_ = g; }
    bool goofy() const { return goofy_; }
    bool usingModel() const { return model_ != nullptr; }
    const std::vector<Transform>& jointsModel() const { return jointsModel_; }
    Transform riderModelToWorld() const { return riderWorld_; }
    Vec3 headPosition() const;
    RiderAnimator* animator() { return animator_.get(); }

private:
    enum Part { Deck = 0, Grip, Brake, Fork, Bars, Grips, Clamp, TyreF, CoreF, TyreR, CoreR, PartCount };
    void buildScooterMeshes();
    void buildMannequin();
    void updateScooterParts(const Transform& body, const Player& player, bool bailed);
    void updateRider(const Transform& body, Player& player, float dt, bool bailed);

    RenderScene* rs_ = nullptr;
    Customization custom_;
    std::shared_ptr<GpuMesh> parts_[PartCount];
    RenderScene::Handle partHandles_[PartCount];
    std::vector<MaterialPtr> partMats_[PartCount];
    // rider model
    std::shared_ptr<Model> model_;
    std::vector<RenderScene::Handle> riderHandles_;
    std::vector<std::string> riderMeshVariant_;  // mesh names (customization visibility)
    int boneOffset_ = -1;
    std::unique_ptr<RiderAnimator> animator_;
    std::vector<Transform> jointsModel_;
    Transform riderWorld_;
    // mannequin fallback
    std::vector<RenderScene::Handle> mannequin_;
    std::vector<std::pair<int, int>> mannequinBones_;
    float wheelSpin_ = 0.0f;
    float barSteer_ = 0.0f;
    bool visible_ = true;
    bool riderVisible_ = true;
    bool goofy_ = false;
};

}  // namespace sw
