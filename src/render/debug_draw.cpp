#include "render/debug_draw.h"

namespace sw {

DebugDraw& debugDraw() {
    static DebugDraw d;
    return d;
}

void DebugDraw::line(const Vec3& a, const Vec3& b, const Vec4& color) {
    if (lines_.size() > 400000) return;
    lines_.push_back({a, color});
    lines_.push_back({b, color});
}

void DebugDraw::arrow(const Vec3& from, const Vec3& to, const Vec4& color, float head) {
    line(from, to, color);
    Vec3 d = to - from;
    float len = d.length();
    if (len < 1e-4f) return;
    Vec3 dir = d / len;
    Vec3 p = anyPerpendicular(dir);
    Vec3 q = sw::cross(dir, p);
    float h = std::min(head, len * 0.4f);
    line(to, to - dir * h + p * h * 0.5f, color);
    line(to, to - dir * h - p * h * 0.5f, color);
    line(to, to - dir * h + q * h * 0.5f, color);
    line(to, to - dir * h - q * h * 0.5f, color);
}

void DebugDraw::box(const AABB& b, const Vec4& color) {
    Vec3 c[8];
    for (int i = 0; i < 8; ++i) c[i] = Vec3(i & 1 ? b.max.x : b.min.x, i & 2 ? b.max.y : b.min.y, i & 4 ? b.max.z : b.min.z);
    const int e[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (auto& k : e) line(c[k[0]], c[k[1]], color);
}

void DebugDraw::box(const Mat4& xf, const Vec3& h, const Vec4& color) {
    Vec3 c[8];
    for (int i = 0; i < 8; ++i) c[i] = xf.transformPoint(Vec3(i & 1 ? h.x : -h.x, i & 2 ? h.y : -h.y, i & 4 ? h.z : -h.z));
    const int e[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (auto& k : e) line(c[k[0]], c[k[1]], color);
}

void DebugDraw::circle(const Vec3& c, const Vec3& normal, float r, const Vec4& color, int segments) {
    Vec3 u = anyPerpendicular(normal.normalized());
    Vec3 v = sw::cross(normal.normalized(), u);
    Vec3 prev = c + u * r;
    for (int i = 1; i <= segments; ++i) {
        float a = float(i) / float(segments) * kTwoPi;
        Vec3 p = c + (u * std::cos(a) + v * std::sin(a)) * r;
        line(prev, p, color);
        prev = p;
    }
}

void DebugDraw::sphere(const Vec3& c, float r, const Vec4& color, int segments) {
    circle(c, Vec3(1, 0, 0), r, color, segments);
    circle(c, Vec3(0, 1, 0), r, color, segments);
    circle(c, Vec3(0, 0, 1), r, color, segments);
}

void DebugDraw::cross(const Vec3& p, float s, const Vec4& color) {
    line(p - Vec3(s, 0, 0), p + Vec3(s, 0, 0), color);
    line(p - Vec3(0, s, 0), p + Vec3(0, s, 0), color);
    line(p - Vec3(0, 0, s), p + Vec3(0, 0, s), color);
}

void DebugDraw::axes(const Mat4& xf, float s) {
    Vec3 o = xf.translationPart();
    line(o, o + xf.transformDir(Vec3(s, 0, 0)), Vec4(1, 0.2f, 0.2f, 1));
    line(o, o + xf.transformDir(Vec3(0, s, 0)), Vec4(0.2f, 1, 0.2f, 1));
    line(o, o + xf.transformDir(Vec3(0, 0, s)), Vec4(0.2f, 0.4f, 1, 1));
}

}  // namespace sw
