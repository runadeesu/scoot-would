#include "render/mesh_builder.h"


namespace sw {

void MeshBuilder::setMaterial(int slot) { material_ = slot; }

Vec2 MeshBuilder::projectUv(const Vec3& p, const Vec3& n) const {
    Vec3 an = vabs(n);
    if (an.y >= an.x && an.y >= an.z) return Vec2(p.x, p.z) * uvScale_;
    if (an.x >= an.z) return Vec2(p.z * signf(n.x), -p.y) * uvScale_;
    return Vec2(-p.x * signf(n.z), -p.y) * uvScale_;
}

uint32_t MeshBuilder::addVertex(const Vec3& p, const Vec3& n, const Vec2& uv) {
    Vertex v;
    v.position = xf_.transformPoint(p);
    v.normal = nxf_.transformDir(n).normalized();
    v.uv = uv;
    data_.vertices.push_back(v);
    return uint32_t(data_.vertices.size() - 1);
}

void MeshBuilder::addTriangle(uint32_t a, uint32_t b, uint32_t c) {
    // keep a per-material list in the submesh vector itself: submeshes are rebuilt in build()
    if (data_.submeshes.empty() || data_.submeshes.back().material != material_ ||
        data_.submeshes.back().firstIndex + data_.submeshes.back().indexCount != data_.indices.size()) {
        data_.submeshes.push_back({uint32_t(data_.indices.size()), 0, material_});
    }
    data_.indices.push_back(a);
    data_.indices.push_back(b);
    data_.indices.push_back(c);
    data_.submeshes.back().indexCount += 3;
}

void MeshBuilder::addQuad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    addTriangle(a, b, c);
    addTriangle(a, c, d);
}

void MeshBuilder::quad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
    Vec3 n = cross(b - a, c - a).normalized();
    if (n.lengthSq() < 0.5f) n = cross(c - a, d - a).normalized();
    Vec3 wn = nxf_.transformDir(n).normalized();
    uint32_t ia = addVertex(a, n, projectUv(xf_.transformPoint(a), wn));
    uint32_t ib = addVertex(b, n, projectUv(xf_.transformPoint(b), wn));
    uint32_t ic = addVertex(c, n, projectUv(xf_.transformPoint(c), wn));
    uint32_t id = addVertex(d, n, projectUv(xf_.transformPoint(d), wn));
    addQuad(ia, ib, ic, id);
}

void MeshBuilder::polygon(const std::vector<Vec3>& pts, const Vec3& normal) {
    size_t n = pts.size();
    if (n < 3) return;
    Vec3 nn = normal.normalized();
    Vec3 wn = nxf_.transformDir(nn).normalized();
    // 2D projection basis
    Vec3 u = anyPerpendicular(nn);
    Vec3 v = cross(nn, u);
    std::vector<Vec2> p2(n);
    std::vector<uint32_t> vid(n);
    for (size_t i = 0; i < n; ++i) {
        p2[i] = Vec2(dot(pts[i], u), dot(pts[i], v));
        vid[i] = addVertex(pts[i], nn, projectUv(xf_.transformPoint(pts[i]), wn));
    }
    // ensure CCW in the (u,v) basis
    float area = 0;
    for (size_t i = 0; i < n; ++i) {
        const Vec2 &a = p2[i], &b = p2[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
    }
    std::vector<size_t> idx(n);
    for (size_t i = 0; i < n; ++i) idx[i] = area >= 0 ? i : n - 1 - i;
    auto cross2 = [](const Vec2& a, const Vec2& b, const Vec2& c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
    auto inside = [&](const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c) {
        return cross2(a, b, p) >= -1e-7f && cross2(b, c, p) >= -1e-7f && cross2(c, a, p) >= -1e-7f;
    };
    int guard = 0;
    while (idx.size() > 3 && guard++ < 10000) {
        bool clipped = false;
        for (size_t i = 0; i < idx.size(); ++i) {
            size_t i0 = idx[(i + idx.size() - 1) % idx.size()], i1 = idx[i], i2 = idx[(i + 1) % idx.size()];
            if (cross2(p2[i0], p2[i1], p2[i2]) <= 1e-9f) continue;  // reflex
            bool ear = true;
            for (size_t k : idx) {
                if (k == i0 || k == i1 || k == i2) continue;
                if (inside(p2[k], p2[i0], p2[i1], p2[i2])) {
                    ear = false;
                    break;
                }
            }
            if (!ear) continue;
            if (area >= 0)
                addTriangle(vid[i0], vid[i1], vid[i2]);
            else
                addTriangle(vid[i0], vid[i2], vid[i1]);
            idx.erase(idx.begin() + long(i));
            clipped = true;
            break;
        }
        if (!clipped) break;  // degenerate polygon
    }
    if (idx.size() == 3) {
        if (area >= 0)
            addTriangle(vid[idx[0]], vid[idx[1]], vid[idx[2]]);
        else
            addTriangle(vid[idx[0]], vid[idx[2]], vid[idx[1]]);
    }
}

void MeshBuilder::box(const Vec3& c, const Vec3& s) { boxFaces(c, s, true, true, true); }

void MeshBuilder::boxFaces(const Vec3& c, const Vec3& s, bool top, bool bottom, bool sides) {
    Vec3 h = s * 0.5f;
    Vec3 p[8] = {c + Vec3(-h.x, -h.y, -h.z), c + Vec3(h.x, -h.y, -h.z), c + Vec3(h.x, h.y, -h.z), c + Vec3(-h.x, h.y, -h.z),
                 c + Vec3(-h.x, -h.y, h.z),  c + Vec3(h.x, -h.y, h.z),  c + Vec3(h.x, h.y, h.z),  c + Vec3(-h.x, h.y, h.z)};
    if (sides) {
        quad(p[4], p[5], p[6], p[7]);  // +z
        quad(p[1], p[0], p[3], p[2]);  // -z
        quad(p[5], p[1], p[2], p[6]);  // +x
        quad(p[0], p[4], p[7], p[3]);  // -x
    }
    if (top) quad(p[7], p[6], p[2], p[3]);     // +y
    if (bottom) quad(p[0], p[1], p[5], p[4]);  // -y
}

void MeshBuilder::box(const Vec3& center, const Vec3& size, const Quat& rot) {
    Mat4 saved = xf_;
    setTransform(xf_ * Mat4::trs(center, rot, Vec3(1)));
    box(Vec3(0), size);
    setTransform(saved);
}

void MeshBuilder::extrude(const std::vector<Vec2>& prof, float z0, float z1, bool caps, bool smooth) {
    size_t n = prof.size();
    if (n < 3) return;
    // walls with arc-length UVs, smooth normals across gentle bends
    std::vector<Vec2> edgeN(n);
    std::vector<float> edgeLen(n);
    for (size_t i = 0; i < n; ++i) {
        Vec2 a = prof[i], b = prof[(i + 1) % n];
        Vec2 d = b - a;
        edgeLen[i] = d.length();
        edgeN[i] = Vec2(d.y, -d.x).normalized();
    }
    float arc = 0;
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        if (edgeLen[i] < 1e-6f) continue;
        Vec2 n0 = edgeN[i], n1 = edgeN[i];
        if (smooth) {
            size_t prev = (i + n - 1) % n, next = j;
            if (dot(edgeN[prev], edgeN[i]) > 0.82f) n0 = (edgeN[prev] + edgeN[i]).normalized();
            if (dot(edgeN[next], edgeN[i]) > 0.82f) n1 = (edgeN[next] + edgeN[i]).normalized();
        }
        Vec3 N0(n0.x, n0.y, 0), N1(n1.x, n1.y, 0);
        float u0 = arc * uvScale_, u1 = (arc + edgeLen[i]) * uvScale_;
        uint32_t a = addVertex(Vec3(prof[i].x, prof[i].y, z1), N0, Vec2(z1 * uvScale_, u0));
        uint32_t b = addVertex(Vec3(prof[i].x, prof[i].y, z0), N0, Vec2(z0 * uvScale_, u0));
        uint32_t c = addVertex(Vec3(prof[j].x, prof[j].y, z0), N1, Vec2(z0 * uvScale_, u1));
        uint32_t d = addVertex(Vec3(prof[j].x, prof[j].y, z1), N1, Vec2(z1 * uvScale_, u1));
        addQuad(a, b, c, d);
        arc += edgeLen[i];
    }
    if (caps) {
        std::vector<Vec3> front, back;
        for (size_t i = 0; i < n; ++i) front.push_back(Vec3(prof[i].x, prof[i].y, z1));
        for (size_t i = 0; i < n; ++i) back.push_back(Vec3(prof[n - 1 - i].x, prof[n - 1 - i].y, z0));
        polygon(front, Vec3(0, 0, 1));
        polygon(back, Vec3(0, 0, -1));
    }
}

void MeshBuilder::lathe(const std::vector<Vec2>& prof, int segments, bool smooth, bool inside, float arcStart, float arcEnd) {
    size_t n = prof.size();
    if (n < 2) return;
    std::vector<float> arcs(n, 0);
    for (size_t i = 1; i < n; ++i) arcs[i] = arcs[i - 1] + (prof[i] - prof[i - 1]).length();
    float span = arcEnd - arcStart;
    for (size_t i = 0; i + 1 < n; ++i) {
        Vec2 a = prof[i], b = prof[i + 1];
        Vec2 d = b - a;
        if (d.lengthSq() < 1e-12f) continue;
        // (d.y, -d.x) faces away from the axis for a profile that goes up / inwards
        auto segN = [&](size_t k) {
            Vec2 dd = prof[k + 1] - prof[k];
            return dd.lengthSq() < 1e-12f ? Vec2(0, 0) : Vec2(dd.y, -dd.x).normalized();
        };
        Vec2 self = segN(i);
        Vec2 en = self, enB = self;
        if (smooth) {
            if (i > 0 && dot(segN(i - 1), self) > 0.8f) en = (segN(i - 1) + self).normalized();
            if (i + 2 < n && dot(segN(i + 1), self) > 0.8f) enB = (segN(i + 1) + self).normalized();
        }
        float sgn = inside ? -1.0f : 1.0f;
        for (int s = 0; s < segments; ++s) {
            float t0 = arcStart + float(s) / float(segments) * span, t1 = arcStart + float(s + 1) / float(segments) * span;
            Vec3 dir0(std::cos(t0), 0, -std::sin(t0)), dir1(std::cos(t1), 0, -std::sin(t1));
            Vec3 p00 = dir0 * a.x + Vec3(0, a.y, 0), p01 = dir1 * a.x + Vec3(0, a.y, 0);
            Vec3 p10 = dir0 * b.x + Vec3(0, b.y, 0), p11 = dir1 * b.x + Vec3(0, b.y, 0);
            Vec3 na0, na1, nb0, nb1;
            if (smooth) {
                na0 = (dir0 * en.x + Vec3(0, en.y, 0)).normalized() * sgn;
                na1 = (dir1 * en.x + Vec3(0, en.y, 0)).normalized() * sgn;
                nb0 = (dir0 * enB.x + Vec3(0, enB.y, 0)).normalized() * sgn;
                nb1 = (dir1 * enB.x + Vec3(0, enB.y, 0)).normalized() * sgn;
            } else {
                Vec3 fn = cross(p01 - p00, p10 - p00);
                if (fn.lengthSq() < 1e-12f) fn = cross(p11 - p10, p00 - p10) * -1.0f;
                fn = fn.normalized() * sgn;
                na0 = na1 = nb0 = nb1 = fn;
            }
            float circ = std::max(a.x, b.x) * span;
            float u0 = float(s) / float(segments) * circ * uvScale_, u1 = float(s + 1) / float(segments) * circ * uvScale_;
            uint32_t i00 = addVertex(p00, na0, Vec2(u0, arcs[i] * uvScale_));
            uint32_t i01 = addVertex(p01, na1, Vec2(u1, arcs[i] * uvScale_));
            uint32_t i11 = addVertex(p11, nb1, Vec2(u1, arcs[i + 1] * uvScale_));
            uint32_t i10 = addVertex(p10, nb0, Vec2(u0, arcs[i + 1] * uvScale_));
            if (inside)
                addQuad(i00, i10, i11, i01);
            else
                addQuad(i00, i01, i11, i10);
        }
    }
}

void MeshBuilder::cylinder(const Vec3& base, float radius, float height, int segments, bool caps) {
    Mat4 saved = xf_;
    setTransform(xf_ * Mat4::translation(base));
    lathe({{radius, 0}, {radius, height}}, segments, true);
    if (caps) {
        std::vector<Vec3> top, bottom;
        for (int s = 0; s < segments; ++s) {
            float t = float(s) / float(segments) * kTwoPi;
            top.push_back(Vec3(std::cos(t) * radius, height, -std::sin(t) * radius));
        }
        for (int s = segments - 1; s >= 0; --s) {
            float t = float(s) / float(segments) * kTwoPi;
            bottom.push_back(Vec3(std::cos(t) * radius, 0, -std::sin(t) * radius));
        }
        polygon(top, Vec3(0, 1, 0));
        polygon(bottom, Vec3(0, -1, 0));
    }
    setTransform(saved);
}

void MeshBuilder::tube(const std::vector<Vec3>& path, float radius, int segments, bool caps) {
    size_t n = path.size();
    if (n < 2) return;
    // parallel transport frames
    std::vector<Vec3> T(n), N(n), B(n);
    for (size_t i = 0; i < n; ++i) {
        Vec3 t;
        if (i == 0) t = path[1] - path[0];
        else if (i == n - 1) t = path[n - 1] - path[n - 2];
        else t = (path[i + 1] - path[i]).normalized() + (path[i] - path[i - 1]).normalized();
        T[i] = t.normalized();
    }
    N[0] = anyPerpendicular(T[0]);
    for (size_t i = 1; i < n; ++i) {
        Vec3 nn = projectOnPlane(N[i - 1], T[i]);
        N[i] = nn.lengthSq() > 1e-8f ? nn.normalized() : anyPerpendicular(T[i]);
    }
    for (size_t i = 0; i < n; ++i) B[i] = cross(T[i], N[i]);
    float along = 0;
    std::vector<uint32_t> prevRing;
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) along += distance(path[i], path[i - 1]);
        // widen the ring at bends so the tube keeps its thickness
        float scale = 1.0f;
        if (i > 0 && i < n - 1) {
            float c = dot((path[i] - path[i - 1]).normalized(), T[i]);
            scale = 1.0f / std::max(0.5f, c);
        }
        std::vector<uint32_t> ring;
        for (int s = 0; s <= segments; ++s) {
            float a = float(s) / float(segments) * kTwoPi;
            Vec3 dir = N[i] * std::cos(a) + B[i] * std::sin(a);
            Vec3 p = path[i] + dir * radius;
            if (scale != 1.0f) {
                // scale only the component along the bend direction
                Vec3 off = dir * radius;
                Vec3 bendDir = projectOnPlane((path[i + 1] - path[i]).normalized() - (path[i] - path[i - 1]).normalized(), T[i]);
                if (bendDir.lengthSq() > 1e-8f) {
                    bendDir = bendDir.normalized();
                    off += bendDir * dot(off, bendDir) * (scale - 1.0f);
                }
                p = path[i] + off;
            }
            ring.push_back(addVertex(p, dir, Vec2(float(s) / float(segments) * kTwoPi * radius * uvScale_ * 4.0f, along * uvScale_)));
        }
        if (!prevRing.empty())
            for (int s = 0; s < segments; ++s) addQuad(prevRing[s], ring[s], ring[s + 1], prevRing[s + 1]);
        prevRing = ring;
    }
    if (caps) {
        for (int end = 0; end < 2; ++end) {
            size_t i = end == 0 ? 0 : n - 1;
            Vec3 nrm = end == 0 ? -T[0] : T[n - 1];
            std::vector<Vec3> pts;
            for (int s = 0; s < segments; ++s) {
                float a = float(end == 0 ? segments - s : s) / float(segments) * kTwoPi;
                pts.push_back(path[i] + (N[i] * std::cos(a) + B[i] * std::sin(a)) * radius);
            }
            polygon(pts, nrm);
        }
    }
}

void MeshBuilder::squareTube(const std::vector<Vec3>& path, float hw, float hh, bool caps) {
    size_t n = path.size();
    if (n < 2) return;
    for (size_t i = 0; i + 1 < n; ++i) {
        Vec3 a = path[i], b = path[i + 1];
        Vec3 t = (b - a).normalized();
        Vec3 side = cross(t, Vec3(0, 1, 0));
        if (side.lengthSq() < 1e-6f) side = Vec3(1, 0, 0);
        side = side.normalized();
        Vec3 up = cross(side, t).normalized();
        Vec3 s = side * hw, u = up * hh;
        quad(a - s + u, a + s + u, b + s + u, b - s + u);  // top
        quad(a + s - u, a - s - u, b - s - u, b + s - u);  // bottom
        quad(a + s + u, a + s - u, b + s - u, b + s + u);  // side +
        quad(a - s - u, a - s + u, b - s + u, b - s - u);  // side -
        if (caps && i == 0) quad(a - s - u, a + s - u, a + s + u, a - s + u);
        if (caps && i + 2 == n) quad(b + s - u, b - s - u, b - s + u, b + s + u);
    }
}

void MeshBuilder::sphere(const Vec3& c, float r, int rings, int segments) {
    std::vector<uint32_t> prev;
    for (int i = 0; i <= rings; ++i) {
        float v = float(i) / float(rings);
        float phi = v * kPi;
        std::vector<uint32_t> ring;
        for (int s = 0; s <= segments; ++s) {
            float u = float(s) / float(segments);
            float th = u * kTwoPi;
            Vec3 n(std::sin(phi) * std::cos(th), std::cos(phi), -std::sin(phi) * std::sin(th));
            ring.push_back(addVertex(c + n * r, n, Vec2(u * kTwoPi * r * uvScale_, v * kPi * r * uvScale_)));
        }
        if (!prev.empty())
            for (int s = 0; s < segments; ++s) addQuad(prev[s], ring[s], ring[s + 1], prev[s + 1]);
        prev = ring;
    }
}

void MeshBuilder::capsule(const Vec3& a, const Vec3& b, float r, int rings, int segments) {
    Vec3 axis = b - a;
    float len = axis.length();
    Vec3 dir = len > 1e-6f ? axis / len : Vec3(0, 1, 0);
    Quat q = Quat::fromTo(Vec3(0, 1, 0), dir);
    Mat4 saved = xf_;
    setTransform(xf_ * Mat4::trs(a, q, Vec3(1)));
    // profile: bottom hemisphere, cylinder, top hemisphere
    std::vector<Vec2> prof;
    int hr = std::max(2, rings / 2);
    for (int i = 0; i <= hr; ++i) {
        float t = -kHalfPi + float(i) / float(hr) * kHalfPi;
        prof.push_back(Vec2(std::cos(t) * r, std::sin(t) * r));
    }
    for (int i = 0; i <= hr; ++i) {
        float t = float(i) / float(hr) * kHalfPi;
        prof.push_back(Vec2(std::cos(t) * r, len + std::sin(t) * r));
    }
    prof.front().x = 0.0f;
    prof.back().x = 0.0f;
    lathe(prof, segments, true);
    setTransform(saved);
}

void MeshBuilder::grid(const Vec3& c, float sx, float sz, int dx, int dz) {
    std::vector<uint32_t> ids((dx + 1) * (dz + 1));
    for (int j = 0; j <= dz; ++j)
        for (int i = 0; i <= dx; ++i) {
            Vec3 p = c + Vec3(-sx * 0.5f + sx * float(i) / float(dx), 0, -sz * 0.5f + sz * float(j) / float(dz));
            Vec3 wp = xf_.transformPoint(p);
            ids[j * (dx + 1) + i] = addVertex(p, Vec3(0, 1, 0), Vec2(wp.x, wp.z) * uvScale_);
        }
    for (int j = 0; j < dz; ++j)
        for (int i = 0; i < dx; ++i) {
            uint32_t a = ids[j * (dx + 1) + i], b = ids[j * (dx + 1) + i + 1];
            uint32_t cc = ids[(j + 1) * (dx + 1) + i + 1], d = ids[(j + 1) * (dx + 1) + i];
            addQuad(a, d, cc, b);
        }
}

MeshData MeshBuilder::build() {
    MeshData out = std::move(data_);
    out.computeBounds();
    out.computeTangents();
    data_ = MeshData();
    data_.name = out.name;
    return out;
}

// ---------------------------------------------------------------------------
namespace profiles {

std::vector<Vec2> quarterPipe(float height, float radius, float deck, int segments) {
    // x: from the start of the transition (0) into the ramp, y up. CCW outline.
    float vert = std::max(0.0f, height - radius);
    float curveH = height - vert;
    float thetaMax = std::acos(clampf(1.0f - curveH / radius, -1.0f, 1.0f));
    float xTop = radius * std::sin(thetaMax);
    float xEnd = xTop + deck;
    std::vector<Vec2> p;
    p.push_back({0, 0});
    p.push_back({xEnd, 0});
    p.push_back({xEnd, height});
    p.push_back({xTop, height});
    if (vert > 0.0f) p.push_back({xTop, curveH});
    for (int i = segments - 1; i >= 1; --i) {
        float th = thetaMax * float(i) / float(segments);
        p.push_back({radius * std::sin(th), radius * (1.0f - std::cos(th))});
    }
    return p;
}

std::vector<Vec2> kicker(float length, float height, int segments) {
    float r = (length * length + height * height) / (2.0f * height);
    float thetaMax = std::asin(clampf(length / r, 0.0f, 1.0f));
    std::vector<Vec2> p;
    p.push_back({0, 0});
    p.push_back({length, 0});
    p.push_back({length, height});
    for (int i = segments - 1; i >= 1; --i) {
        float th = thetaMax * float(i) / float(segments);
        p.push_back({r * std::sin(th), r * (1.0f - std::cos(th))});
    }
    return p;
}

std::vector<Vec2> bank(float length, float height) { return {{0, 0}, {length, 0}, {length, height}}; }

std::vector<Vec2> funbox(float rampLength, float topLength, float height) {
    return {{0, 0}, {rampLength * 2 + topLength, 0}, {rampLength + topLength, height}, {rampLength, height}};
}

std::vector<Vec2> stairs(int steps, float rise, float run, float landingTop, float /*landingBottom*/) {
    float H = float(steps) * rise;
    float xLast = landingTop + float(steps - 1) * run;
    std::vector<Vec2> p;
    p.push_back({0, 0});
    p.push_back({xLast, 0});
    for (int k = steps; k >= 1; --k) {
        float x = landingTop + float(k - 1) * run;
        float yTop = H - float(k - 1) * rise;
        p.push_back({x, yTop});
        if (k > 1) p.push_back({x - run, yTop});
    }
    p.push_back({0, H});
    // remove duplicates
    std::vector<Vec2> out;
    for (auto& v : p)
        if (out.empty() || (out.back() - v).lengthSq() > 1e-8f) out.push_back(v);
    return out;
}

std::vector<Vec2> spine(float height, float radius, int segments) {
    float thetaMax = std::acos(clampf(1.0f - height / radius, -1.0f, 1.0f));
    float xTop = radius * std::sin(thetaMax);
    std::vector<Vec2> p;
    p.push_back({0, 0});
    p.push_back({xTop * 2.0f, 0});
    for (int i = 1; i < segments; ++i) {
        float th = thetaMax * float(i) / float(segments);
        p.push_back({xTop * 2.0f - radius * std::sin(th), radius * (1.0f - std::cos(th))});
    }
    p.push_back({xTop, height});
    for (int i = segments - 1; i >= 1; --i) {
        float th = thetaMax * float(i) / float(segments);
        p.push_back({radius * std::sin(th), radius * (1.0f - std::cos(th))});
    }
    return p;
}

}  // namespace profiles
}  // namespace sw
