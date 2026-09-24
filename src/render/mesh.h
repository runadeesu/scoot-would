// scoot would - meshes (CPU data + GPU buffers + LODs)
#pragma once

#include "core/math.h"
#include "render/gpu.h"

#include <memory>
#include <string>
#include <vector>

namespace sw {

struct Vertex {
    Vec3 position;
    Vec3 normal{0, 1, 0};
    Vec4 tangent{1, 0, 0, 1};
    Vec2 uv;
};
static_assert(sizeof(Vertex) == 48, "vertex layout");

struct SkinVertex {
    uint8_t joints[4] = {0, 0, 0, 0};
    float weights[4] = {1, 0, 0, 0};
};
static_assert(sizeof(SkinVertex) == 20, "skin vertex layout");

struct SubMesh {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int material = 0;  // slot index into the owner's material list
};

struct MeshLod {
    std::vector<SubMesh> submeshes;  // same material slots as LOD0
    float screenSize = 0.0f;         // switch to this LOD when projected size (fraction of screen height) is below
};

// CPU side mesh data
struct MeshData {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<SkinVertex> skin;  // empty = static
    std::vector<uint32_t> indices;
    std::vector<SubMesh> submeshes;
    std::vector<std::string> materialNames;  // per slot (optional)
    AABB bounds;

    void computeBounds();
    void computeNormals();   // smooth normals from triangles
    void computeTangents();  // MikkTSpace-like per triangle accumulation
    void append(const MeshData& other, const Mat4& transform, int materialOffset = 0);
    uint32_t triangleCount() const { return uint32_t(indices.size() / 3); }
};

struct GpuMesh {
    std::string name;
    uint32_t id = 0;  // unique, used for draw sorting
    GpuBuffer vertexBuffer;
    GpuBuffer indexBuffer;
    GpuBuffer skinBuffer;
    std::vector<MeshLod> lods;  // lods[0] = full detail
    AABB bounds;
    float radius = 0.0f;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    bool skinned() const { return skinBuffer.handle != nullptr; }
    uint32_t materialSlots() const { return lods.empty() ? 0 : uint32_t(lods[0].submeshes.size()); }
};

struct LodSettings {
    int levels = 3;  // total including LOD0 (max 4)
    float ratios[3] = {0.5f, 0.22f, 0.08f};
    float screenSizes[3] = {0.12f, 0.05f, 0.015f};
};

// upload mesh to GPU, optionally generating LODs with meshoptimizer
std::shared_ptr<GpuMesh> createGpuMesh(MeshData& data, bool generateLods, const LodSettings& lod = {});
void releaseGpuMesh(GpuMesh& mesh);
// optimize vertex cache / overdraw order in place (keeps submesh ranges)
void optimizeMesh(MeshData& data);

}  // namespace sw
