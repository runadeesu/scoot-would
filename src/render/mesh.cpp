#include "render/mesh.h"
#include "core/log.h"

#include <meshoptimizer.h>

#include <atomic>

namespace sw {

void MeshData::computeBounds() {
    bounds = AABB();
    for (const Vertex& v : vertices) bounds.expand(v.position);
    if (!bounds.valid()) bounds = AABB(Vec3(0), Vec3(0));
}

void MeshData::computeNormals() {
    std::vector<Vec3> n(vertices.size(), Vec3(0));
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
        Vec3 fn = cross(vertices[b].position - vertices[a].position, vertices[c].position - vertices[a].position);
        n[a] += fn;
        n[b] += fn;
        n[c] += fn;
    }
    for (size_t i = 0; i < vertices.size(); ++i) {
        Vec3 nn = n[i].normalized();
        vertices[i].normal = nn.lengthSq() > 0.5f ? nn : Vec3(0, 1, 0);
    }
}

void MeshData::computeTangents() {
    std::vector<Vec3> tan(vertices.size(), Vec3(0)), bit(vertices.size(), Vec3(0));
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
        const Vertex &v0 = vertices[i0], &v1 = vertices[i1], &v2 = vertices[i2];
        Vec3 e1 = v1.position - v0.position, e2 = v2.position - v0.position;
        Vec2 d1 = v1.uv - v0.uv, d2 = v2.uv - v0.uv;
        float det = d1.x * d2.y - d2.x * d1.y;
        if (std::fabs(det) < 1e-12f) continue;
        float r = 1.0f / det;
        Vec3 t = (e1 * d2.y - e2 * d1.y) * r;
        Vec3 b = (e2 * d1.x - e1 * d2.x) * r;
        for (uint32_t k : {i0, i1, i2}) {
            tan[k] += t;
            bit[k] += b;
        }
    }
    for (size_t i = 0; i < vertices.size(); ++i) {
        Vec3 n = vertices[i].normal;
        Vec3 t = tan[i];
        t = (t - n * dot(n, t));
        if (t.lengthSq() < 1e-12f) t = anyPerpendicular(n);
        t = t.normalized();
        float w = dot(cross(n, t), bit[i]) < 0.0f ? -1.0f : 1.0f;
        vertices[i].tangent = Vec4(t, w);
    }
}

void MeshData::append(const MeshData& other, const Mat4& transform, int materialOffset) {
    uint32_t base = uint32_t(vertices.size());
    Mat4 nm = transform.affineInverse().transposed();
    for (const Vertex& v : other.vertices) {
        Vertex o = v;
        o.position = transform.transformPoint(v.position);
        o.normal = nm.transformDir(v.normal).normalized();
        Vec3 t = transform.transformDir(v.tangent.xyz()).normalized();
        o.tangent = Vec4(t, v.tangent.w);
        vertices.push_back(o);
    }
    if (!other.skin.empty()) skin.insert(skin.end(), other.skin.begin(), other.skin.end());
    for (const SubMesh& sm : other.submeshes) {
        SubMesh n{uint32_t(indices.size()), sm.indexCount, sm.material + materialOffset};
        for (uint32_t i = 0; i < sm.indexCount; ++i) indices.push_back(other.indices[sm.firstIndex + i] + base);
        // merge with an existing submesh of the same material when contiguous
        if (!submeshes.empty() && submeshes.back().material == n.material &&
            submeshes.back().firstIndex + submeshes.back().indexCount == n.firstIndex)
            submeshes.back().indexCount += n.indexCount;
        else
            submeshes.push_back(n);
    }
    if (other.submeshes.empty() && !other.indices.empty()) {
        SubMesh n{uint32_t(indices.size()), uint32_t(other.indices.size()), materialOffset};
        for (uint32_t idx : other.indices) indices.push_back(idx + base);
        submeshes.push_back(n);
    }
    bounds.expand(other.bounds.transformed(transform));
}

void optimizeMesh(MeshData& data) {
    if (data.indices.empty() || data.vertices.empty()) return;
    for (SubMesh& sm : data.submeshes) {
        meshopt_optimizeVertexCache(data.indices.data() + sm.firstIndex, data.indices.data() + sm.firstIndex, sm.indexCount,
                                    data.vertices.size());
    }
}

static void sortSubmeshesByMaterial(MeshData& data) {
    // group all submeshes of the same material into one contiguous range
    std::vector<SubMesh> sorted = data.submeshes;
    std::stable_sort(sorted.begin(), sorted.end(), [](const SubMesh& a, const SubMesh& b) { return a.material < b.material; });
    std::vector<uint32_t> newIdx;
    newIdx.reserve(data.indices.size());
    std::vector<SubMesh> merged;
    for (const SubMesh& sm : sorted) {
        uint32_t first = uint32_t(newIdx.size());
        newIdx.insert(newIdx.end(), data.indices.begin() + sm.firstIndex, data.indices.begin() + sm.firstIndex + sm.indexCount);
        if (!merged.empty() && merged.back().material == sm.material)
            merged.back().indexCount += sm.indexCount;
        else
            merged.push_back({first, sm.indexCount, sm.material});
    }
    data.indices = std::move(newIdx);
    data.submeshes = std::move(merged);
}

std::shared_ptr<GpuMesh> createGpuMesh(MeshData& data, bool generateLods, const LodSettings& lodSettings) {
    static std::atomic<uint32_t> s_nextId{1};
    auto mesh = std::make_shared<GpuMesh>();
    mesh->name = data.name;
    mesh->id = s_nextId++;
    if (data.submeshes.empty()) data.submeshes.push_back({0, uint32_t(data.indices.size()), 0});
    if (data.vertices.empty() || data.indices.empty()) {
        LOG_WARN("mesh: '%s' is empty", data.name.c_str());
        return mesh;
    }
    sortSubmeshesByMaterial(data);
    if (!data.bounds.valid()) data.computeBounds();

    std::vector<uint32_t> allIndices = data.indices;
    MeshLod lod0;
    lod0.submeshes = data.submeshes;
    mesh->lods.push_back(lod0);

    if (generateLods && data.skin.empty() && data.indices.size() > 300) {
        int levels = std::min(lodSettings.levels, 4);
        for (int l = 1; l < levels; ++l) {
            MeshLod lod;
            lod.screenSize = lodSettings.screenSizes[l - 1];
            float ratio = lodSettings.ratios[l - 1];
            bool any = false;
            for (const SubMesh& sm : data.submeshes) {
                size_t target = size_t(float(sm.indexCount) * ratio) / 3 * 3;
                target = std::max<size_t>(target, 3);
                std::vector<uint32_t> out(sm.indexCount);
                float err = 0.0f;
                size_t n = meshopt_simplify(out.data(), data.indices.data() + sm.firstIndex, sm.indexCount,
                                            &data.vertices[0].position.x, data.vertices.size(), sizeof(Vertex), target,
                                            0.05f, meshopt_SimplifyLockBorder, &err);
                if (n < 3 || n >= sm.indexCount) {
                    // cannot simplify: reuse full detail range
                    lod.submeshes.push_back(sm);
                    continue;
                }
                any = true;
                out.resize(n);
                meshopt_optimizeVertexCache(out.data(), out.data(), n, data.vertices.size());
                SubMesh s{uint32_t(allIndices.size()), uint32_t(n), sm.material};
                allIndices.insert(allIndices.end(), out.begin(), out.end());
                lod.submeshes.push_back(s);
            }
            if (!any) break;
            mesh->lods.push_back(lod);
        }
    }

    mesh->vertexCount = uint32_t(data.vertices.size());
    mesh->indexCount = uint32_t(allIndices.size());
    mesh->bounds = data.bounds;
    mesh->radius = data.bounds.extents().length();
    mesh->vertexBuffer = gpu().createBufferWithData(SDL_GPU_BUFFERUSAGE_VERTEX, data.vertices.data(),
                                                    uint32_t(data.vertices.size() * sizeof(Vertex)), data.name.c_str());
    mesh->indexBuffer = gpu().createBufferWithData(SDL_GPU_BUFFERUSAGE_INDEX, allIndices.data(),
                                                   uint32_t(allIndices.size() * sizeof(uint32_t)), data.name.c_str());
    if (!data.skin.empty())
        mesh->skinBuffer = gpu().createBufferWithData(SDL_GPU_BUFFERUSAGE_VERTEX, data.skin.data(),
                                                      uint32_t(data.skin.size() * sizeof(SkinVertex)), data.name.c_str());
    return mesh;
}

void releaseGpuMesh(GpuMesh& mesh) {
    gpu().release(mesh.vertexBuffer);
    gpu().release(mesh.indexBuffer);
    gpu().release(mesh.skinBuffer);
}

}  // namespace sw
