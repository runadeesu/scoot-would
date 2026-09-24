// scoot would - level kit helpers shared by the prefab generators (internal header)
#pragma once

#include "render/mesh_builder.h"
#include "scene/scene.h"

#include <string>
#include <vector>

namespace sw::kit {

inline float P(const Json& j, const char* k, float def) { return jget<float>(j, k, def); }
inline int Pi(const Json& j, const char* k, int def) { return jget<int>(j, k, def); }
inline bool Pb(const Json& j, const char* k, bool def) { return jget<bool>(j, k, def); }
inline std::string Ps(const Json& j, const char* k, const std::string& def) { return jget<std::string>(j, k, def); }
inline std::string M(const std::string& mat, const std::string& def) { return mat.empty() ? def : mat; }

inline std::string keyOf(const std::string& prefab, const Json& params, const std::string& material) {
    return prefab + "|" + params.dump() + "|" + material;
}

inline std::vector<Vec2> shifted(std::vector<Vec2> p, float dx) {
    for (auto& v : p) v.x += dx;
    return p;
}

inline float profileLength(const std::vector<Vec2>& p) {
    float mn = 1e9f, mx = -1e9f;
    for (auto& v : p) {
        mn = std::min(mn, v.x);
        mx = std::max(mx, v.x);
    }
    return mx - mn;
}

inline void addRail(PrefabBuild& out, std::vector<Vec3> pts, RailType type, float radius = 0.025f) {
    RailComponent r;
    r.points = std::move(pts);
    r.type = type;
    r.radius = radius;
    out.rails.push_back(r);
}

// round rail tube with posts
inline void railGeometry(MeshBuilder& b, const std::vector<Vec3>& path, float radius, int postsEvery, bool square) {
    if (square)
        b.squareTube(path, radius, radius, true);
    else
        b.tube(path, radius, 12, true);
    // posts down to the ground from every Nth point (and the ends)
    for (size_t i = 0; i < path.size(); ++i) {
        bool end = i == 0 || i + 1 == path.size();
        if (!end && (postsEvery <= 0 || i % size_t(postsEvery) != 0)) continue;
        Vec3 p = path[i];
        float h = p.y;
        if (h < 0.08f) continue;
        Vec3 inward = end ? (i == 0 ? (path[1] - path[0]) : (path[i - 1] - path[i])).normalized() * 0.08f : Vec3(0);
        inward.y = 0;
        Vec3 base = Vec3(p.x + inward.x, 0.0f, p.z + inward.z);
        b.cylinder(base, radius * 0.9f, h - radius * 0.5f, 10, false);
        // base plate
        b.cylinder(base, radius * 2.6f, 0.012f, 12, true);
    }
}


// street detail kit (prefabs_street.cpp)
void registerStreetPrefabs(PrefabRegistry& r);
// trees + shrubs (prefabs_nature.cpp)
void registerNaturePrefabs(PrefabRegistry& r);

}  // namespace sw::kit
