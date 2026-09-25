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
    // default outfit: street scooter rider (grey hoodie, black baggy pants, white sneakers, cap)
    int top = 1;     // 0 t-shirt, 1 hoodie, 2 tank top
    int pants = 0;   // 0 baggy pants, 1 shorts
    int shoes = 0;   // 0 low sneakers, 1 high tops
    int helmet = 2;  // 0 none, 1 helmet, 2 cap
    Vec3 skinColor{0.58f, 0.36f, 0.25f};
    Vec3 topColor{0.35f, 0.36f, 0.38f};
    Vec3 pantsColor{0.03f, 0.03f, 0.035f};
    Vec3 shoesColor{0.9f, 0.9f, 0.88f};
    Vec3 helmetColor{0.08f, 0.22f, 0.7f};
    // scooter
    int deck = 0;    // shape variant
    int bars = 0;    // 0 standard, 1 tall, 2 wide
    int wheels = 0;  // core style
    int clamp = 0;   // 0 IHC double clamp, 1 SCS
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
    void setBindPose(bool b) { bindPose_ = b; }
    // showroom: the scooter stands alone at `where` (no rider, no physics pose)
    void setDisplay(bool on, const Transform& where = Transform());
    bool display() const { return display_; }
    bool goofy() const { return goofy_; }
    bool usingModel() const { return model_ != nullptr; }
    const std::vector<Transform>& jointsModel() const { return jointsModel_; }
    Transform riderModelToWorld() const { return riderWorld_; }
    Vec3 headPosition() const;
    // first person: point between the eyes (world); the POV camera point (eye height, behind the bars like a
    // helmet / chest camera); hiding the head and torso from the camera (the shadow keeps the whole body)
    Vec3 eyePosition() const;
    Vec3 povCameraPosition() const;
    void setFirstPerson(bool fp) { firstPerson_ = fp; }
    RiderAnimator* animator() { return animator_.get(); }

private:
    enum Part { Deck = 0, Grip, Brake, Fork, Bars, Grips, Clamp, TyreF, CoreF, TyreR, CoreR, PartCount };
    void buildScooterMeshes();
    void buildMannequin();
    void updateScooterParts(const Transform& body, const Player& player, bool bailed);
    void updateRider(const Transform& body, Player& player, float dt, bool bailed);

    RenderScene* rs_ = nullptr;
    Customization custom_;
    float barHeight_ = 0.79f, barWidth_ = 0.56f;  // of the current bars (the rider's hands follow them)
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
    bool bindPose_ = false;
    bool display_ = false;
    bool firstPerson_ = false;
    std::vector<uint8_t> povHidden_;  // joints hidden from the first person camera (spine, chest, neck, head, face)
    int headJoint_ = -1, eyeJoint_[2] = {-1, -1}, pelvisJoint_ = -1;
    std::vector<RenderScene::Handle> shadowProxies_;  // full body, shadow only, while in first person
    int shadowBoneOffset_ = -1;
    Vec3 barCenterModel_{0, 0.8f, -0.18f};  // bar centre in rider model space
    Transform displayXf_;
};

}  // namespace sw
