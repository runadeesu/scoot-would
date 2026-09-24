#include "assets/asset_manager.h"
#include "core/filesystem.h"
#include "core/json.h"
#include "core/log.h"

namespace sw {

AssetManager& assets() {
    static AssetManager am;
    return am;
}

AnimationClipPtr Model::clip(const std::string& name) const {
    for (auto& c : clips)
        if (c->name == name) return c;
    return nullptr;
}

int Model::findNode(const std::string& name) const {
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].name == name) return int(i);
    return -1;
}

void AssetManager::init() {
    const uint8_t white[4] = {255, 255, 255, 255};
    const uint8_t normal[4] = {128, 128, 255, 255};
    const uint8_t black[4] = {0, 0, 0, 255};
    white_ = createTextureRGBA8("builtin:white", 1, 1, white, false, false);
    flatNormal_ = createTextureRGBA8("builtin:normal", 1, 1, normal, false, false);
    black_ = createTextureRGBA8("builtin:black", 1, 1, black, false, false);
    default_ = std::make_shared<Material>();
    default_->name = "default";
    default_->baseColorFactor = Vec4(0.6f, 0.6f, 0.6f, 1.0f);
    default_->roughness = 0.7f;
    default_->id = nextMaterialId_++;
}

void AssetManager::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [k, m] : meshes_)
        if (m.gpu) releaseGpuMesh(*m.gpu);
    for (auto& [k, m] : models_)
        for (auto& mm : m->meshes)
            if (mm.gpu) releaseGpuMesh(*mm.gpu);
    meshes_.clear();
    models_.clear();
    materials_.clear();
    textures_.clear();
    sounds_.clear();
    fonts_.clear();
    default_.reset();
    white_.reset();
    flatNormal_.reset();
    black_.reset();
}

TexturePtr AssetManager::texture(const std::string& relPath, bool srgb) {
    if (relPath.empty()) return nullptr;
    std::string key = relPath + (srgb ? "|s" : "|l");
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = textures_.find(key);
        if (it != textures_.end()) return it->second;
    }
    TextureLoadOptions opt;
    opt.srgb = srgb;
    opt.maxSize = maxTextureSize_;
    TexturePtr t = loadTexture(fs::resolve(relPath), opt);
    if (!t) {
        // fallback so a missing file never crashes rendering
        t = srgb ? white_ : flatNormal_;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    textures_[key] = t;
    return t;
}

static AlphaMode parseAlpha(const std::string& s) {
    if (s == "mask") return AlphaMode::Mask;
    if (s == "blend") return AlphaMode::Blend;
    return AlphaMode::Opaque;
}

MaterialPtr AssetManager::material(const std::string& name) {
    if (name.empty()) return default_;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = materials_.find(name);
        if (it != materials_.end()) return it->second;
    }
    auto m = std::make_shared<Material>();
    m->name = name;
    m->sourcePath = fs::resolve("assets/materials/" + name + ".json");
    auto j = loadJsonFile(m->sourcePath);
    if (!j) {
        LOG_WARN("asset: material '%s' not found, using default", name.c_str());
        *m = *default_;
        m->name = name;
    } else {
        const Json& d = *j;
        std::string bc = jget<std::string>(d, "baseColor", "");
        std::string nm = jget<std::string>(d, "normal", "");
        std::string orm = jget<std::string>(d, "orm", "");
        std::string em = jget<std::string>(d, "emissive", "");
        if (!bc.empty()) m->baseColor = texture(bc, true);
        if (!nm.empty()) m->normal = texture(nm, false);
        if (!orm.empty()) m->orm = texture(orm, false);
        if (!em.empty()) m->emissive = texture(em, true);
        if (d.contains("color")) m->baseColorFactor = Vec4(jcolor(d, "color"), 1.0f);
        m->baseColorFactor = jvec4(d, "baseColorFactor", m->baseColorFactor);
        m->metallic = jget<float>(d, "metallic", m->orm ? 1.0f : 0.0f);
        m->roughness = jget<float>(d, "roughness", m->orm ? 1.0f : 0.8f);
        m->normalScale = jget<float>(d, "normalScale", 1.0f);
        m->aoStrength = jget<float>(d, "aoStrength", 1.0f);
        m->uvScale = jget<float>(d, "uvScale", 1.0f);
        m->alphaMode = parseAlpha(jget<std::string>(d, "alpha", "opaque"));
        m->alphaCutoff = jget<float>(d, "alphaCutoff", 0.5f);
        m->doubleSided = jget<bool>(d, "doubleSided", false);
        m->emissiveFactor = jcolor(d, "emissiveColor", Vec3(1, 1, 1));
        m->emissiveStrength = jget<float>(d, "emissiveStrength", 0.0f);
        m->wear = jget<float>(d, "wear", 0.0f);
        m->surface = jget<std::string>(d, "surface", "concrete");
        m->tintable = jget<bool>(d, "tintable", false);
        m->decal = jget<bool>(d, "decal", false);
        std::string shading = jget<std::string>(d, "shading", "standard");
        m->shading = shading == "skin" ? 1 : shading == "cloth" ? 2 : shading == "interior" ? 3 : shading == "foliage" ? 4 : 0;
        m->roomDepth = jget<float>(d, "roomDepth", 3.2f);
        m->interiorLight = jget<float>(d, "interiorLight", 0.6f);
        m->shop = jget<bool>(d, "shop", false);
        m->antiTile = jget<bool>(d, "antiTile", false);
        m->detailScale = jget<float>(d, "detailScale", 0.0f);
        m->detailStrength = jget<float>(d, "detailStrength", 0.5f);
        std::string det = jget<std::string>(d, "detail", "");
        if (!det.empty()) m->detail = texture(det, false);
        if (m->decal && m->alphaMode == AlphaMode::Opaque) m->alphaMode = AlphaMode::Blend;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    m->id = nextMaterialId_++;
    materials_[name] = m;
    return m;
}

MaterialPtr AssetManager::createMaterial(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = materials_.find(name);
    if (it != materials_.end()) return it->second;
    auto m = std::make_shared<Material>();
    m->name = name;
    m->id = nextMaterialId_++;
    materials_[name] = m;
    return m;
}

std::vector<std::string> AssetManager::materialNames() const {
    std::vector<std::string> names;
    for (auto& f : fs::listFiles(fs::resolve("assets/materials"), ".json")) names.push_back(fs::stem(f));
    return names;
}

int AssetManager::reloadMaterials() {
    std::vector<MaterialPtr> list;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [k, m] : materials_)
            if (!m->sourcePath.empty()) list.push_back(m);
    }
    int n = 0;
    for (auto& m : list) {
        std::string name = m->name;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            materials_.erase(name);
        }
        MaterialPtr fresh = material(name);
        uint32_t id = m->id;
        *m = *fresh;  // update in place so existing references see the change
        m->id = id;
        std::lock_guard<std::mutex> lock(mutex_);
        materials_[name] = m;
        ++n;
    }
    return n;
}

std::shared_ptr<GpuMesh> AssetManager::mesh(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = meshes_.find(key);
    return it == meshes_.end() ? nullptr : it->second.gpu;
}

const MeshData* AssetManager::meshData(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = meshes_.find(key);
    return it == meshes_.end() ? nullptr : &it->second.data;
}

std::shared_ptr<GpuMesh> AssetManager::registerMesh(const std::string& key, MeshData& data, bool lods) {
    auto gm = createGpuMesh(data, lods);
    std::lock_guard<std::mutex> lock(mutex_);
    auto& e = meshes_[key];
    if (e.gpu) releaseGpuMesh(*e.gpu);
    e.gpu = gm;
    e.data = data;
    return gm;
}

std::shared_ptr<GpuMesh> AssetManager::getOrCreateMesh(const std::string& key, const std::function<MeshData()>& build, bool lods) {
    if (auto m = mesh(key)) return m;
    MeshData d = build();
    d.name = key;
    return registerMesh(key, d, lods);
}

ModelPtr AssetManager::model(const std::string& relPath) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = models_.find(relPath);
        if (it != models_.end()) return it->second;
    }
    ModelPtr m = loadGltfModel(*this, relPath);
    std::lock_guard<std::mutex> lock(mutex_);
    models_[relPath] = m;
    return m;
}

AssetManager::Stats AssetManager::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats s;
    s.textures = textures_.size();
    s.materials = materials_.size();
    s.meshes = meshes_.size();
    s.models = models_.size();
    s.sounds = sounds_.size();
    s.fonts = fonts_.size();
    for (auto& [k, m] : models_)
        if (m) s.clips += m->clips.size();
    for (auto& [k, t] : textures_) {
        if (!t || !t->gpuTex.handle) continue;
        size_t base = size_t(t->gpuTex.width) * t->gpuTex.height * formatBytesPerPixel(t->gpuTex.format);
        s.textureBytes += base * 4 / 3;
    }
    return s;
}

void AssetManager::releaseUnused() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = textures_.begin(); it != textures_.end();) {
        if (it->second.use_count() == 1)
            it = textures_.erase(it);
        else
            ++it;
    }
}

}  // namespace sw
