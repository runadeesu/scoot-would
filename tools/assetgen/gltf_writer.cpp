#include "gltf_writer.h"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace sw::tools {

int GlbWriter::addMaterial(const GMaterial& m) {
    materials_.push_back(m);
    return int(materials_.size() - 1);
}
int GlbWriter::addMesh(const GMesh& m) {
    meshes_.push_back(m);
    return int(meshes_.size() - 1);
}
int GlbWriter::addNode(const GNode& n) {
    nodes_.push_back(n);
    return int(nodes_.size() - 1);
}
void GlbWriter::setSkin(const std::vector<int>& jointNodes, const std::vector<Mat4>& inverseBind) {
    skinJoints_ = jointNodes;
    inverseBind_ = inverseBind;
}
void GlbWriter::addAnimation(const GAnimation& a) { animations_.push_back(a); }

int GlbWriter::addAccessor(const void* data, size_t bytes, int componentType, int count, const char* type, bool vertexAttr,
                           const std::vector<float>* mn, const std::vector<float>* mx) {
    while (bin_.size() % 4) bin_.push_back(0);
    size_t offset = bin_.size();
    bin_.insert(bin_.end(), static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + bytes);
    Json bv = {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", bytes}};
    if (vertexAttr) bv["target"] = 34962;
    bufferViews_.push_back(bv);
    Json acc = {{"bufferView", int(bufferViews_.size() - 1)}, {"componentType", componentType}, {"count", count}, {"type", type}};
    if (mn) acc["min"] = *mn;
    if (mx) acc["max"] = *mx;
    accessors_.push_back(acc);
    return int(accessors_.size() - 1);
}

bool GlbWriter::write(const std::string& path) {
    bin_.clear();
    accessors_ = Json::array();
    bufferViews_ = Json::array();
    Json j;
    j["asset"] = {{"version", "2.0"}, {"generator", "scoot would assetgen"}};
    Json mats = Json::array();
    for (auto& m : materials_) {
        Json jm = {{"name", m.name},
                   {"pbrMetallicRoughness",
                    {{"baseColorFactor", {m.baseColor.x, m.baseColor.y, m.baseColor.z, m.baseColor.w}}, {"metallicFactor", m.metallic}, {"roughnessFactor", m.roughness}}}};
        if (m.tintable) jm["extras"] = {{"tintable", true}};
        mats.push_back(jm);
    }
    j["materials"] = mats;
    Json meshes = Json::array();
    for (auto& m : meshes_) {
        Json prims = Json::array();
        for (auto& p : m.primitives) {
            size_t n = p.vertices.size();
            std::vector<float> pos(n * 3), nrm(n * 3), tan(n * 4), uv(n * 2), wts(n * 4);
            std::vector<uint8_t> jnt(n * 4);
            std::vector<float> mn = {1e9f, 1e9f, 1e9f}, mx = {-1e9f, -1e9f, -1e9f};
            for (size_t i = 0; i < n; ++i) {
                const GVertex& v = p.vertices[i];
                for (int k = 0; k < 3; ++k) {
                    pos[i * 3 + k] = v.pos[k];
                    nrm[i * 3 + k] = v.normal[k];
                    mn[size_t(k)] = std::min(mn[size_t(k)], v.pos[k]);
                    mx[size_t(k)] = std::max(mx[size_t(k)], v.pos[k]);
                }
                for (int k = 0; k < 4; ++k) {
                    tan[i * 4 + k] = v.tangent[k];
                    jnt[i * 4 + k] = v.joints[k];
                    wts[i * 4 + k] = v.weights[k];
                }
                uv[i * 2] = v.uv.x;
                uv[i * 2 + 1] = v.uv.y;
            }
            Json attrs;
            attrs["POSITION"] = addAccessor(pos.data(), pos.size() * 4, 5126, int(n), "VEC3", true, &mn, &mx);
            attrs["NORMAL"] = addAccessor(nrm.data(), nrm.size() * 4, 5126, int(n), "VEC3", true);
            attrs["TANGENT"] = addAccessor(tan.data(), tan.size() * 4, 5126, int(n), "VEC4", true);
            attrs["TEXCOORD_0"] = addAccessor(uv.data(), uv.size() * 4, 5126, int(n), "VEC2", true);
            if (m.skinned) {
                attrs["JOINTS_0"] = addAccessor(jnt.data(), jnt.size(), 5121, int(n), "VEC4", true);
                attrs["WEIGHTS_0"] = addAccessor(wts.data(), wts.size() * 4, 5126, int(n), "VEC4", true);
            }
            int idx = addAccessor(p.indices.data(), p.indices.size() * 4, 5125, int(p.indices.size()), "SCALAR", false);
            bufferViews_[size_t(accessors_[size_t(idx)]["bufferView"].get<int>())]["target"] = 34963;
            prims.push_back({{"attributes", attrs}, {"indices", idx}, {"material", p.material}, {"mode", 4}});
        }
        meshes.push_back({{"name", m.name}, {"primitives", prims}});
    }
    j["meshes"] = meshes;
    Json nodes = Json::array();
    std::vector<std::vector<int>> children(nodes_.size());
    std::vector<int> roots;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].parent >= 0)
            children[size_t(nodes_[i].parent)].push_back(int(i));
        else
            roots.push_back(int(i));
    }
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const GNode& n = nodes_[i];
        Json jn = {{"name", n.name}};
        if (n.translation.lengthSq() > 0) jn["translation"] = {n.translation.x, n.translation.y, n.translation.z};
        if (n.rotation.w != 1.0f) jn["rotation"] = {n.rotation.x, n.rotation.y, n.rotation.z, n.rotation.w};
        if (n.mesh >= 0) jn["mesh"] = n.mesh;
        if (n.skinned) jn["skin"] = 0;
        if (!children[i].empty()) jn["children"] = children[i];
        nodes.push_back(jn);
    }
    j["nodes"] = nodes;
    j["scenes"] = Json::array({{{"nodes", roots}}});
    j["scene"] = 0;
    if (!skinJoints_.empty()) {
        std::vector<float> ib;
        for (auto& m : inverseBind_)
            for (float f : m.m) ib.push_back(f);
        int acc = addAccessor(ib.data(), ib.size() * 4, 5126, int(inverseBind_.size()), "MAT4", false);
        j["skins"] = Json::array({{{"joints", skinJoints_}, {"inverseBindMatrices", acc}, {"skeleton", skinJoints_[0]}}});
    }
    Json anims = Json::array();
    for (auto& a : animations_) {
        Json samplers = Json::array(), channels = Json::array();
        for (auto& c : a.channels) {
            std::vector<float> mn = {c.times.front()}, mx = {c.times.back()};
            int in = addAccessor(c.times.data(), c.times.size() * 4, 5126, int(c.times.size()), "SCALAR", false, &mn, &mx);
            std::vector<float> vals;
            bool rot = c.path == "rotation";
            for (auto& v : c.values) {
                vals.push_back(v.x);
                vals.push_back(v.y);
                vals.push_back(v.z);
                if (rot) vals.push_back(v.w);
            }
            int out = addAccessor(vals.data(), vals.size() * 4, 5126, int(c.values.size()), rot ? "VEC4" : "VEC3", false);
            samplers.push_back({{"input", in}, {"output", out}, {"interpolation", "LINEAR"}});
            channels.push_back({{"sampler", int(samplers.size() - 1)}, {"target", {{"node", c.node}, {"path", c.path}}}});
        }
        anims.push_back({{"name", a.name}, {"samplers", samplers}, {"channels", channels}});
    }
    if (!anims.empty()) j["animations"] = anims;
    while (bin_.size() % 4) bin_.push_back(0);
    j["accessors"] = accessors_;
    j["bufferViews"] = bufferViews_;
    j["buffers"] = Json::array({{{"byteLength", bin_.size()}}});

    std::string js = j.dump();
    while (js.size() % 4) js.push_back(' ');
    uint32_t total = 12 + 8 + uint32_t(js.size()) + 8 + uint32_t(bin_.size());
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    w32(0x46546C67);  // glTF
    w32(2);
    w32(total);
    w32(uint32_t(js.size()));
    w32(0x4E4F534A);  // JSON
    f.write(js.data(), std::streamsize(js.size()));
    w32(uint32_t(bin_.size()));
    w32(0x004E4942);  // BIN
    f.write(reinterpret_cast<const char*>(bin_.data()), std::streamsize(bin_.size()));
    return bool(f);
}

}  // namespace sw::tools
