// scoot would - lightweight scene graph (entities + components), loaded from / saved to JSON
#pragma once

#include "core/json.h"
#include "core/math.h"
#include "physics/physics_world.h"
#include "render/material.h"
#include "render/mesh.h"
#include "render/render_scene.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sw {

using EntityId = uint32_t;

// --- components -------------------------------------------------------------------
struct MeshRendererComponent {
    std::shared_ptr<GpuMesh> mesh;
    std::vector<MaterialPtr> materials;
    bool castShadows = true;
    bool isStatic = true;
    bool batched = false;  // merged into a static batch (no own render proxy)
    Vec4 tint{1, 1, 1, 0};
    float lodBias = 1.0f;
    float cullDistance = 0.0f;
    RenderScene::Handle handle = RenderScene::kInvalid;
    std::string meshKey;
    std::shared_ptr<MeshData> cpu;  // unique meshes keep their CPU data (static batching)
};

struct LightComponent {
    LightProxy light;  // local space position/direction
    bool nightOnly = true;
    RenderScene::Handle handle = RenderScene::kInvalid;
};

struct ColliderComponent {
    enum class Kind { Mesh, Box, None } kind = Kind::Mesh;
    Vec3 halfExtents{0.5f};
    Vec3 center;
    int surface = 0;
    BodyHandle body = kNoBody;
    std::shared_ptr<MeshData> mesh;  // local space collision mesh
};

struct RigidBodyComponent {
    float mass = 10.0f;
    Vec3 halfExtents{0.2f};
    ShapeKind shape = ShapeKind::Box;
    float radius = 0.2f;
    BodyHandle body = kNoBody;
};

struct AudioSourceComponent {
    std::string sound;
    float volume = 1.0f;
    float minDistance = 2.0f;
    float maxDistance = 40.0f;
    bool loop = true;
    int voice = -1;
};

struct CameraComponent {
    float fov = 60.0f;
};

enum class RailType { Round = 0, Square, Ledge, Coping };

struct RailComponent {
    std::vector<Vec3> points;  // local space polyline
    RailType type = RailType::Round;
    int surface = 0;
    float radius = 0.025f;
};

struct SpawnComponent {
    std::string label;
    bool isDefault = false;
};

struct ZoneComponent {
    std::string area;       // map area name (Street Plaza, Skatepark, ...)
    Vec3 halfExtents{10.0f};
    std::string kind = "area";  // area | gap | line_target
    int score = 0;
};

struct Entity {
    EntityId id = 0;
    std::string name;
    std::string prefab;       // generator name ("" = empty node)
    Json params = Json::object();
    std::string material;     // primary material override
    std::vector<std::string> tags;
    std::string area;
    Transform local;
    EntityId parent = 0;
    std::vector<EntityId> children;
    Mat4 world;
    bool worldDirty = true;
    bool visible = true;
    bool editorOnly = false;

    std::unique_ptr<MeshRendererComponent> meshRenderer;
    std::unique_ptr<LightComponent> light;
    std::unique_ptr<ColliderComponent> collider;
    std::unique_ptr<RigidBodyComponent> rigidBody;
    std::unique_ptr<AudioSourceComponent> audioSource;
    std::unique_ptr<CameraComponent> camera;
    std::vector<RailComponent> rails;
    std::unique_ptr<SpawnComponent> spawn;
    std::unique_ptr<ZoneComponent> zone;

    bool hasTag(const std::string& t) const;
    Vec3 worldPosition() const { return world.translationPart(); }
};

// output of a prefab generator
struct PrefabBuild {
    MeshData mesh;                         // may be empty
    MeshData collisionMesh;                // optional simpler collision geometry (default: mesh)
    std::vector<std::string> materials;    // per material slot
    std::string meshKey;                   // cache key for shared meshes (instancing); "" = unique
    ColliderComponent::Kind collider = ColliderComponent::Kind::Mesh;
    Vec3 boxHalfExtents{0.5f}, boxCenter;
    std::vector<RailComponent> rails;
    std::vector<LightComponent> lights;
    bool castShadows = true;
    bool isStatic = true;
    float cullDistance = 0.0f;
    std::unique_ptr<RigidBodyComponent> rigidBody;
    std::unique_ptr<SpawnComponent> spawn;
    std::unique_ptr<ZoneComponent> zone;
    std::unique_ptr<AudioSourceComponent> audio;
    bool editorHelper = false;  // only visible in the editor
    std::string surface;        // overrides the material surface
};

using PrefabFn = std::function<void(const Json& params, const std::string& material, PrefabBuild& out)>;

class PrefabRegistry {
public:
    void add(const std::string& name, const std::string& category, PrefabFn fn, Json defaults);
    bool build(const std::string& name, const Json& params, const std::string& material, PrefabBuild& out) const;
    const Json& defaults(const std::string& name) const;
    std::vector<std::pair<std::string, std::string>> list() const;  // (name, category)
    bool has(const std::string& name) const { return entries_.count(name) > 0; }

private:
    struct Entry {
        std::string category;
        PrefabFn fn;
        Json defaults;
    };
    std::unordered_map<std::string, Entry> entries_;
};
PrefabRegistry& prefabs();
void registerBuiltinPrefabs();

class Scene {
public:
    explicit Scene(RenderScene* render = nullptr) : render_(render) {}
    ~Scene();

    void setRenderScene(RenderScene* rs) { render_ = rs; }
    Entity* create(const std::string& name, EntityId parent = 0);
    void destroy(EntityId id);
    Entity* get(EntityId id);
    const Entity* get(EntityId id) const;
    Entity* find(const std::string& name);
    void setParent(EntityId child, EntityId parent);
    void markDirty(EntityId id);
    void updateTransforms();
    const std::vector<EntityId>& roots() const { return roots_; }
    size_t count() const { return entities_.size(); }
    template <typename F>
    void forEach(F&& fn) {
        for (auto& [id, e] : entities_) fn(*e);
    }
    std::vector<Entity*> all();

    // (re)generates the entity's components from its prefab + params and registers them
    // with the render scene / physics
    void instantiate(Entity& e);
    void releaseComponents(Entity& e);
    void rebuild(Entity& e) { releaseComponents(e); instantiate(e); }
    void syncTransform(Entity& e);  // after the transform changed (editor)
    Entity* duplicate(EntityId id);

    bool load(const std::string& absPath);
    bool save(const std::string& absPath) const;
    void clear();
    Json entityToJson(const Entity& e) const;
    Entity* entityFromJson(const Json& j, EntityId parentOverride = 0);

    // merge static unique meshes into per-sector / per-material batches (play mode)
    void buildStaticBatches(float sectorSize = 48.0f);
    void clearStaticBatches();
    bool batched() const { return !batches_.empty(); }

    const std::string& name() const { return name_; }
    const Json& environmentJson() const { return environment_; }
    void setEnvironmentJson(const Json& j) { environment_ = j; }
    const Json& metadata() const { return metadata_; }
    Json& metadata() { return metadata_; }
    bool enablePhysics = true;
    bool editorMode = false;  // show helper meshes (spawns, zones)
    uint32_t revision = 0;    // bumped on structural change (rails / zones list rebuilt)

private:
    void updateWorld(Entity& e, const Mat4& parentWorld);
    std::unordered_map<EntityId, std::unique_ptr<Entity>> entities_;
    std::vector<EntityId> roots_;
    EntityId nextId_ = 1;
    RenderScene* render_ = nullptr;
    std::vector<RenderScene::Handle> batches_;
    std::string name_ = "untitled";
    Json environment_ = Json::object();
    Json metadata_ = Json::object();
};

}  // namespace sw
