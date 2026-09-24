// scoot would - immediate mode debug line drawing (physics debug, wheel contacts, grind detection)
#pragma once

#include "core/math.h"

#include <vector>

namespace sw {

struct DebugVertex {
    Vec3 pos;
    Vec4 color;
};

class DebugDraw {
public:
    void line(const Vec3& a, const Vec3& b, const Vec4& color);
    void arrow(const Vec3& from, const Vec3& to, const Vec4& color, float head = 0.15f);
    void box(const AABB& b, const Vec4& color);
    void box(const Mat4& xf, const Vec3& halfExtents, const Vec4& color);
    void sphere(const Vec3& c, float r, const Vec4& color, int segments = 16);
    void circle(const Vec3& c, const Vec3& normal, float r, const Vec4& color, int segments = 24);
    void cross(const Vec3& p, float size, const Vec4& color);
    void axes(const Mat4& xf, float size);
    void clear() { lines_.clear(); }
    const std::vector<DebugVertex>& lines() const { return lines_; }
    bool enabled = false;         // physics / gameplay overlay
    bool depthTested = true;

private:
    std::vector<DebugVertex> lines_;
};

DebugDraw& debugDraw();

}  // namespace sw
