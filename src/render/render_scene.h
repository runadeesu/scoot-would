// scoot would - render scene: render proxies, lights, environment, spatial grid culling
#pragma once

#include "core/math.h"
#include "render/material.h"
#include "render/mesh.h"

#include <memory>
#include <string>
#include <vector>

namespace sw {

enum RenderLayer : uint32_t {
    LayerWorld = 1u << 0,
    LayerPlayer = 1u << 1,
    LayerEditor = 1u << 2,
    LayerProps = 1u << 3,
};

struct RenderObject {
    std::shared_ptr<GpuMesh> mesh;
    std::vector<MaterialPtr> materials;  // per material slot
    Mat4 world;
    Vec4 tint{1, 1, 1, 0};  // rgb, a = amount (tintable materials always use rgb)
    AABB worldBounds;
    bool castShadows = true;
    bool visible = true;
    bool isStatic = true;
    float lodBias = 1.0f;
    float cullDistance = 0.0f;  // 0 = renderer default
    int boneOffset = -1;        // skinned meshes: offset into RenderScene::bones
    uint32_t layer = LayerWorld;
    uint32_t userId = 0;        // entity id (editor picking)
};

struct LightProxy {
    enum class Type { Point = 0, Spot = 1 } type = Type::Point;
    Vec3 position;
    Vec3 direction{0, -1, 0};
    Vec3 color{1, 1, 1};
    float intensity = 10.0f;
    float radius = 10.0f;
    float innerAngle = 0.4f, outerAngle = 0.6f;  // radians (spot)
    bool enabled = true;
};

struct Environment {
    std::string name = "day";
    std::string hdri;           // relative path to equirect .hdr
    float rotation = 0.0f;      // radians
    float skyIntensity = 1.6f;
    float iblIntensity = 1.25f;
    bool autoSun = true;        // derive the sun direction from the brightest point of the HDRI
    Vec3 sunDirection{0.4f, 0.7f, 0.3f};
    Vec3 sunColor{1.0f, 0.95f, 0.88f};
    float sunIntensity = 3.0f;  // ratio of direct sun to sky illuminance
    float exposure = 1.0f;
    float fogDensity = 0.004f;
    float fogFalloff = 0.02f;
    float fogStart = 30.0f;
    float fogMax = 0.8f;
    Vec3 fogTint{1, 1, 1};
    float bloomThreshold = 1.2f;
    float bloomStrength = 0.06f;
    float contrast = 1.05f;
    float saturation = 1.05f;
    float temperature = 0.0f;
    float vignette = 0.25f;
    bool lampsOn = false;
    float urbanReflection = 0.0f;  // 0..1: buildings hide the low sky in reflections (city maps)
};

// uniform 2D grid over the XZ plane for static objects
class SpatialGrid {
public:
    void build(const AABB& worldBounds, float cellSize);
    void insert(uint32_t id, const AABB& b);
    void remove(uint32_t id, const AABB& b);
    template <typename F>
    void query(const AABB& region, F&& fn) const;
    float cellSize() const { return cellSize_; }
    int cellsX() const { return nx_; }
    int cellsZ() const { return nz_; }
    const AABB& cellBounds(int ix, int iz) const { return cellAabb_[size_t(iz * nx_ + ix)]; }
    const std::vector<uint32_t>& cell(int ix, int iz) const { return cells_[size_t(iz * nx_ + ix)]; }
    const std::vector<uint32_t>& large() const { return large_; }
    bool valid() const { return nx_ > 0; }

private:
    void cellRange(const AABB& b, int& x0, int& z0, int& x1, int& z1) const;
    Vec3 origin_;
    float cellSize_ = 32.0f;
    int nx_ = 0, nz_ = 0;
    std::vector<std::vector<uint32_t>> cells_;
    std::vector<AABB> cellAabb_;
    std::vector<uint32_t> large_;  // objects spanning many cells
};

// std430 instance record shared with shaders (see common.glsl InstanceData)
struct InstanceGpu {
    Mat4 model;
    Vec4 nrm0, nrm1, nrm2;
    Vec4 tint;
    Vec4 misc;
};
static_assert(sizeof(InstanceGpu) == 144, "instance layout");

class RenderScene {
public:
    using Handle = uint32_t;
    static constexpr Handle kInvalid = 0xffffffffu;

    Handle add(const RenderObject& obj);
    void remove(Handle h);
    void setTransform(Handle h, const Mat4& world);
    void setVisible(Handle h, bool visible);
    void setTint(Handle h, const Vec4& tint);
    void setMaterials(Handle h, const std::vector<MaterialPtr>& mats);
    void setMesh(Handle h, std::shared_ptr<GpuMesh> mesh);
    RenderObject* get(Handle h);
    const RenderObject* get(Handle h) const;
    bool valid(Handle h) const { return h < objects_.size() && alive_[h]; }
    size_t objectCount() const { return liveCount_; }
    size_t capacity() const { return objects_.size(); }

    Handle addLight(const LightProxy& l);
    void removeLight(Handle h);
    LightProxy* light(Handle h);
    const std::vector<LightProxy>& lights() const { return lights_; }
    const std::vector<uint8_t>& lightAlive() const { return lightAlive_; }

    // skinning palette (model space * inverse bind), rebuilt every frame by the animation system
    std::vector<Mat4>& bones() { return bones_; }
    int allocateBones(size_t count);  // returns offset (persistent)

    Environment environment;
    uint64_t environmentVersion = 0;  // bump to make the renderer reload IBL

    // rebuild the spatial grid for static objects (call after loading a level)
    void buildGrid(float cellSize = 32.0f);
    const SpatialGrid& grid() const { return grid_; }
    const std::vector<uint32_t>& dynamicObjects() const { return dynamic_; }
    AABB bounds() const;

    // GPU instance mirror
    const std::vector<InstanceGpu>& instances() const { return instances_; }
    bool takeDirtyRange(uint32_t& first, uint32_t& count);
    void markAllDirty();
    void clear();

    // frame counter based dedup during culling
    std::vector<uint32_t>& visitStamp() { return visit_; }

private:
    void writeInstance(Handle h);
    void markDirty(Handle h);

    std::vector<RenderObject> objects_;
    std::vector<uint8_t> alive_;
    std::vector<Handle> free_;
    std::vector<InstanceGpu> instances_;
    std::vector<uint32_t> visit_;
    std::vector<uint32_t> dynamic_;
    size_t liveCount_ = 0;
    uint32_t dirtyMin_ = 0xffffffffu, dirtyMax_ = 0;
    std::vector<LightProxy> lights_;
    std::vector<uint8_t> lightAlive_;
    std::vector<Mat4> bones_;
    SpatialGrid grid_;
};

template <typename F>
void SpatialGrid::query(const AABB& region, F&& fn) const {
    if (!valid()) return;
    int x0, z0, x1, z1;
    cellRange(region, x0, z0, x1, z1);
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) fn(x, z, cells_[size_t(z * nx_ + x)], cellAabb_[size_t(z * nx_ + x)]);
}

}  // namespace sw
