// scoot would - scooter shop interior, the backdrop of the scooter customization screen: room, counter
// with the display scooter, merchandise built from the real part meshes, CC0 scanned props and
// interior lighting (a photographed shop as image based light + overhead key light + lamps)
#pragma once

#include "render/render_scene.h"

#include <vector>

namespace sw {

class ShopScene {
public:
    // builds the room the first time it is needed (a map load clears the render scene: call reset())
    void ensureBuilt(RenderScene& rs);
    void reset();
    void setActive(bool on);
    bool active() const { return active_; }
    Vec3 origin() const { return origin_; }
    // world transform of the displayed scooter (body space: origin between the axles at axle height)
    Transform displayTransform() const;
    // interior lighting preset
    Environment environment() const;
    // the camera stays inside the room
    Vec3 clampToRoom(const Vec3& worldPos) const;

private:
    RenderScene::Handle add(std::shared_ptr<GpuMesh> mesh, const std::vector<MaterialPtr>& mats, const Mat4& local, bool shadows = true,
                            const Vec4& tint = Vec4(1, 1, 1, 0));
    void addModel(const char* name, const Vec3& pos, float yawDeg, float scale = 1.0f, const char* include = "", const char* exclude = "");
    void addLight(const Vec3& localPos, const Vec3& color, float intensity, float radius);
    void buildRoom();
    void buildCounter();
    void buildMerchandise();

    RenderScene* rs_ = nullptr;
    std::vector<RenderScene::Handle> objects_, lights_;
    bool built_ = false, active_ = false;
    Vec3 origin_{3000.0f, -200.0f, 3000.0f};  // far outside every map: nothing else is in view
};

}  // namespace sw
