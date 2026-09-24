// scoot would - asset manager
// Central cache for every runtime asset type: textures, materials, meshes, glTF
// models (skeletons + animation clips), sounds and fonts. Assets are identified by
// their path relative to the data root; each is loaded at most once. Missing files
// are logged and replaced by fallbacks instead of crashing.
#pragma once

#include "animation/animation.h"
#include "render/material.h"
#include "render/mesh.h"
#include "render/texture.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace sw {

struct ModelNode {
    std::string name;
    int parent = -1;
    Transform local;
    int mesh = -1;   // index into Model::meshes
    int joint = -1;  // index in skeleton if this node is a joint
};

struct ModelMesh {
    std::string name;
    MeshData data;                      // CPU copy (collision, editor picking)
    std::shared_ptr<GpuMesh> gpu;
    std::vector<MaterialPtr> materials;  // per submesh material slot
    int node = -1;
};

struct Model {
    std::string path;
    std::vector<ModelNode> nodes;
    std::vector<ModelMesh> meshes;
    std::vector<MaterialPtr> materials;
    SkeletonPtr skeleton;
    std::vector<AnimationClipPtr> clips;
    AABB bounds;
    AnimationClipPtr clip(const std::string& name) const;
    int findNode(const std::string& name) const;
};
using ModelPtr = std::shared_ptr<Model>;

struct SoundAsset;
using SoundPtr = std::shared_ptr<SoundAsset>;
class Font;
using FontPtr = std::shared_ptr<Font>;

class AssetManager {
public:
    void init();
    void shutdown();

    // textures -----------------------------------------------------------------
    TexturePtr texture(const std::string& relPath, bool srgb = true);
    TexturePtr white() const { return white_; }
    TexturePtr flatNormal() const { return flatNormal_; }
    TexturePtr black() const { return black_; }
    void setMaxTextureSize(int size) { maxTextureSize_ = size; }
    int maxTextureSize() const { return maxTextureSize_; }

    // materials (assets/materials/<name>.json) ------------------------------------
    MaterialPtr material(const std::string& name);
    MaterialPtr createMaterial(const std::string& name);  // runtime material, cached by name
    MaterialPtr defaultMaterial() const { return default_; }
    std::vector<std::string> materialNames() const;
    int reloadMaterials();

    // meshes: procedural meshes registered by key, reused on duplicate requests ----
    std::shared_ptr<GpuMesh> mesh(const std::string& key) const;
    std::shared_ptr<GpuMesh> registerMesh(const std::string& key, MeshData& data, bool lods);
    std::shared_ptr<GpuMesh> getOrCreateMesh(const std::string& key, const std::function<MeshData()>& build, bool lods);
    const MeshData* meshData(const std::string& key) const;

    // glTF 2.0 models (.glb / .gltf) ----------------------------------------------
    ModelPtr model(const std::string& relPath);

    // audio + fonts (implemented in audio/ and ui/) ----------------------------------
    SoundPtr sound(const std::string& relPath);
    FontPtr font(const std::string& relPath, float pixelSize);

    struct Stats {
        size_t textures = 0, materials = 0, meshes = 0, models = 0, sounds = 0, fonts = 0, clips = 0;
        size_t textureBytes = 0;
    };
    Stats stats() const;
    void releaseUnused();

private:
    TexturePtr white_, flatNormal_, black_;
    MaterialPtr default_;
    int maxTextureSize_ = 2048;
    std::unordered_map<std::string, TexturePtr> textures_;
    std::unordered_map<std::string, MaterialPtr> materials_;
    struct MeshEntry {
        std::shared_ptr<GpuMesh> gpu;
        MeshData data;
    };
    std::unordered_map<std::string, MeshEntry> meshes_;
    std::unordered_map<std::string, ModelPtr> models_;
    std::unordered_map<std::string, SoundPtr> sounds_;
    std::map<std::pair<std::string, int>, FontPtr> fonts_;
    uint32_t nextMaterialId_ = 1;
    mutable std::mutex mutex_;
    friend ModelPtr loadGltfModel(AssetManager& am, const std::string& relPath);
};

AssetManager& assets();

ModelPtr loadGltfModel(AssetManager& am, const std::string& relPath);

}  // namespace sw
