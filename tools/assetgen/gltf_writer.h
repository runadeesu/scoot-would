// scoot would asset tools - minimal glTF 2.0 binary (.glb) writer
#pragma once

#include "core/json.h"
#include "core/math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sw::tools {

struct GVertex {
    Vec3 pos, normal;
    Vec4 tangent{1, 0, 0, 1};
    Vec2 uv;
    uint8_t joints[4] = {0, 0, 0, 0};
    float weights[4] = {1, 0, 0, 0};
};

struct GPrimitive {
    std::vector<GVertex> vertices;
    std::vector<uint32_t> indices;
    int material = 0;
};

struct GMesh {
    std::string name;
    std::vector<GPrimitive> primitives;
    bool skinned = false;
};

struct GMaterial {
    std::string name;
    Vec4 baseColor{1, 1, 1, 1};
    float metallic = 0.0f, roughness = 0.8f;
    bool tintable = false;
};

struct GNode {
    std::string name;
    int parent = -1;
    Vec3 translation;
    Quat rotation;
    int mesh = -1;
    bool skinned = false;
};

struct GChannel {
    int node = -1;
    std::string path;  // translation | rotation
    std::vector<float> times;
    std::vector<Vec4> values;
};

struct GAnimation {
    std::string name;
    std::vector<GChannel> channels;
};

class GlbWriter {
public:
    int addMaterial(const GMaterial& m);
    int addMesh(const GMesh& m);
    int addNode(const GNode& n);
    void setSkin(const std::vector<int>& jointNodes, const std::vector<Mat4>& inverseBind);
    void addAnimation(const GAnimation& a);
    bool write(const std::string& path);

private:
    int addAccessor(const void* data, size_t bytes, int componentType, int count, const char* type, bool vertexAttr,
                    const std::vector<float>* mn = nullptr, const std::vector<float>* mx = nullptr);
    std::vector<GMaterial> materials_;
    std::vector<GMesh> meshes_;
    std::vector<GNode> nodes_;
    std::vector<int> skinJoints_;
    std::vector<Mat4> inverseBind_;
    std::vector<GAnimation> animations_;
    // build state
    std::vector<uint8_t> bin_;
    Json accessors_, bufferViews_;
};

}  // namespace sw::tools
