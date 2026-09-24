// scoot would - procedural mesh construction (level kit, props, rider/scooter parts)
#pragma once

#include "render/mesh.h"

#include <vector>

namespace sw {

class MeshBuilder {
public:
    explicit MeshBuilder(std::string name = "mesh") { data_.name = std::move(name); }

    // all geometry added after this call uses the given material slot
    void setMaterial(int slot);
    // UV scale for world-space projected UVs (texture repeats per meter * scale)
    void setUvScale(float s) { uvScale_ = s; }
    void setTransform(const Mat4& m) { xf_ = m; nxf_ = m.affineInverse().transposed(); }
    void resetTransform() { setTransform(Mat4::identity()); }

    uint32_t addVertex(const Vec3& p, const Vec3& n, const Vec2& uv);
    void addTriangle(uint32_t a, uint32_t b, uint32_t c);
    void addQuad(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
    // planar quad with box projected UVs, points counter clockwise seen from the front
    void quad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d);
    // convex/concave planar polygon (ear clipping), CCW seen from normal side
    void polygon(const std::vector<Vec3>& pts, const Vec3& normal);

    void box(const Vec3& center, const Vec3& size);
    void boxFaces(const Vec3& center, const Vec3& size, bool top, bool bottom, bool sides);
    // oriented box
    void box(const Vec3& center, const Vec3& size, const Quat& rot);
    // extrude a side profile (points in local XY, CCW) along Z from z0 to z1
    void extrude(const std::vector<Vec2>& profile, float z0, float z1, bool caps = true, bool smooth = false);
    // lathe: profile points (radius, y) revolved around Y
    // inside = surface faces the axis (bowls)
    void lathe(const std::vector<Vec2>& profile, int segments, bool smooth = true, bool inside = false, float arcStart = 0.0f,
               float arcEnd = kTwoPi);
    void cylinder(const Vec3& base, float radius, float height, int segments, bool caps = true);
    // tube along a polyline (rails, pipes, bars)
    void tube(const std::vector<Vec3>& path, float radius, int segments, bool caps = true);
    void squareTube(const std::vector<Vec3>& path, float halfWidth, float halfHeight, bool caps = true);
    void sphere(const Vec3& center, float radius, int rings, int segments);
    void capsule(const Vec3& a, const Vec3& b, float radius, int rings, int segments);
    // subdivided ground plane (XZ) centered at center
    void grid(const Vec3& center, float sizeX, float sizeZ, int divX, int divZ);

    MeshData& data() { return data_; }
    MeshData build();  // computes bounds / tangents

private:
    Vec2 projectUv(const Vec3& worldP, const Vec3& n) const;
    MeshData data_;
    int material_ = 0;
    float uvScale_ = 1.0f;
    Mat4 xf_;
    Mat4 nxf_;
    std::vector<size_t> submeshStarts_;
};

// profiles for skatepark obstacles (side profile in XY, x = length, y = height)
namespace profiles {
std::vector<Vec2> quarterPipe(float height, float radius, float deckLength, int segments);
std::vector<Vec2> kicker(float length, float height, int segments);
std::vector<Vec2> bank(float length, float height);
std::vector<Vec2> funbox(float rampLength, float topLength, float height);
std::vector<Vec2> stairs(int steps, float stepRise, float stepRun, float landingTop, float landingBottom);
std::vector<Vec2> spine(float height, float radius, int segments);
}  // namespace profiles

}  // namespace sw
