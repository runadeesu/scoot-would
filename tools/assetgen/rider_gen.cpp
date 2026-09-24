// scoot would asset tools - procedural rider: skinned body, clothing variants and animation clips
//
// The rider is built on the shared skeleton blueprint (game/player/rider_blueprint.h), so the model,
// the animation system and the ragdoll always agree on joint names and rest positions. Every piece of
// clothing uses the same skin weight function as the body part underneath it, which keeps layered
// garments from poking through while the rider moves. Replace assets/models/rider.glb with any glTF
// that uses the same joint names to swap the character.
#include "assetgen.h"
#include "gltf_writer.h"

#include "game/player/rider_blueprint.h"
#include "render/mesh.h"

#include <cmath>
#include <functional>
#include <map>
#include <unordered_map>

namespace sw::tools {
namespace {

constexpr float D = kDeg2Rad;

// ---------------------------------------------------------------------------------------------
// geometry

struct Geo {
    std::vector<Vec3> p, n;
    std::vector<Vec2> uv;
    std::vector<uint32_t> idx;

    uint32_t vert(const Vec3& pos, const Vec2& t) {
        p.push_back(pos);
        n.push_back(Vec3(0));
        uv.push_back(t);
        return uint32_t(p.size() - 1);
    }
    // triangle whose face normal points along `outward`
    void tri(uint32_t a, uint32_t b, uint32_t c, const Vec3& outward) {
        Vec3 fn = cross(p[b] - p[a], p[c] - p[a]);
        if (dot(fn, outward) < 0.0f) std::swap(b, c);
        idx.push_back(a);
        idx.push_back(b);
        idx.push_back(c);
    }
    void quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d, const Vec3& outward) {
        tri(a, b, c, outward);
        tri(a, c, d, outward);
    }
};

struct Ring {
    Vec3 c;
    float rx, rz;
    float sq = 2.0f;  // superellipse exponent (2 = ellipse, larger = boxier)
};

Vec2 superEllipse(float th, float rx, float rz, float sq) {
    float cs = std::cos(th), sn = std::sin(th);
    float e = 2.0f / sq;
    float x = (cs < 0 ? -1.0f : 1.0f) * std::pow(std::fabs(cs), e) * rx;
    float y = (sn < 0 ? -1.0f : 1.0f) * std::pow(std::fabs(sn), e) * rz;
    return {x, y};
}

// tube through a list of cross sections (frames by parallel transport, `refSide` sets the rx axis)
void loft(Geo& g, const std::vector<Ring>& rings, int seg, bool capA, bool capB, Vec3 refSide = Vec3(1, 0, 0)) {
    size_t n = rings.size();
    std::vector<Vec3> side(n), bin(n), ax(n);
    Vec3 prev = refSide;
    for (size_t i = 0; i < n; ++i) {
        Vec3 a = (rings[std::min(i + 1, n - 1)].c - rings[i == 0 ? 0 : i - 1].c).normalized();
        Vec3 s = prev - a * dot(prev, a);
        if (s.lengthSq() < 1e-6f) s = anyPerpendicular(a);
        s = s.normalized();
        prev = s;
        ax[i] = a;
        side[i] = s;
        bin[i] = cross(s, a).normalized();
    }
    std::vector<uint32_t> base(n);
    float v = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) v += (rings[i].c - rings[i - 1].c).length();
        base[i] = uint32_t(g.p.size());
        float perim = kPi * (rings[i].rx + rings[i].rz);
        for (int k = 0; k <= seg; ++k) {
            float th = float(k) / float(seg) * kTwoPi;
            Vec2 e = superEllipse(th, rings[i].rx, rings[i].rz, rings[i].sq);
            g.vert(rings[i].c + side[i] * e.x + bin[i] * e.y, Vec2(float(k) / float(seg) * perim, v));
        }
    }
    for (size_t i = 0; i + 1 < n; ++i)
        for (int k = 0; k < seg; ++k) {
            uint32_t a = base[i] + uint32_t(k), b = a + 1, c = base[i + 1] + uint32_t(k) + 1, d = base[i + 1] + uint32_t(k);
            Vec3 mid = (g.p[a] + g.p[b] + g.p[c] + g.p[d]) * 0.25f;
            Vec3 ctr = (rings[i].c + rings[i + 1].c) * 0.5f;
            g.quad(a, b, c, d, mid - ctr);
        }
    auto cap = [&](size_t i, Vec3 outward) {
        uint32_t c = g.vert(rings[i].c, Vec2(0, 0));
        for (int k = 0; k < seg; ++k) g.tri(c, base[i] + uint32_t(k), base[i] + uint32_t(k) + 1, outward);
    };
    if (capA) cap(0, -ax[0]);
    if (capB) cap(n - 1, ax[n - 1]);
}

// ellipsoid (optionally only the part within `phiMax` of its local +Y pole)
void ellipsoid(Geo& g, const Vec3& c, const Vec3& r, const Quat& rot, int rings, int seg, float phiMax = kPi) {
    std::vector<uint32_t> prev;
    for (int i = 0; i <= rings; ++i) {
        float phi = float(i) / float(rings) * phiMax;
        std::vector<uint32_t> ring;
        for (int s = 0; s <= seg; ++s) {
            float th = float(s) / float(seg) * kTwoPi;
            Vec3 d(std::sin(phi) * std::cos(th), std::cos(phi), -std::sin(phi) * std::sin(th));
            Vec3 p = c + rot * Vec3(d.x * r.x, d.y * r.y, d.z * r.z);
            ring.push_back(g.vert(p, Vec2(float(s) / float(seg) * kTwoPi * r.x, phi * r.y)));
        }
        if (!prev.empty())
            for (int s = 0; s < seg; ++s) {
                Vec3 mid = (g.p[prev[size_t(s)]] + g.p[ring[size_t(s) + 1]]) * 0.5f;
                g.quad(prev[size_t(s)], ring[size_t(s)], ring[size_t(s) + 1], prev[size_t(s) + 1], mid - c);
            }
        prev = ring;
    }
}

// thick shell cap (helmets): outer + inner surface joined by a rim
void shell(Geo& g, const Vec3& c, const Vec3& r, float thickness, const Quat& rot, float phiMax, int rings, int seg) {
    auto layer = [&](const Vec3& rr, bool inside) {
        std::vector<std::vector<uint32_t>> all;
        for (int i = 0; i <= rings; ++i) {
            float phi = float(i) / float(rings) * phiMax;
            std::vector<uint32_t> ring;
            for (int s = 0; s <= seg; ++s) {
                float th = float(s) / float(seg) * kTwoPi;
                Vec3 d(std::sin(phi) * std::cos(th), std::cos(phi), -std::sin(phi) * std::sin(th));
                ring.push_back(g.vert(c + rot * Vec3(d.x * rr.x, d.y * rr.y, d.z * rr.z), Vec2(float(s) / float(seg), phi)));
            }
            if (!all.empty())
                for (int s = 0; s < seg; ++s) {
                    const auto& pr = all.back();
                    Vec3 mid = (g.p[pr[size_t(s)]] + g.p[ring[size_t(s) + 1]]) * 0.5f;
                    Vec3 out = mid - c;
                    g.quad(pr[size_t(s)], ring[size_t(s)], ring[size_t(s) + 1], pr[size_t(s) + 1], inside ? -out : out);
                }
            all.push_back(ring);
        }
        return all.back();
    };
    auto outer = layer(r, false);
    auto inner = layer(r - Vec3(thickness), true);
    Vec3 down = rot * Vec3(0, -1, 0);
    for (int s = 0; s < seg; ++s) g.quad(outer[size_t(s)], inner[size_t(s)], inner[size_t(s) + 1], outer[size_t(s) + 1], down);
}

// constant radius tube along a path (straps, laces)
void tube(Geo& g, const std::vector<Vec3>& path, float r, int seg) {
    std::vector<Ring> rings;
    for (auto& p : path) rings.push_back({p, r, r});
    loft(g, rings, seg, true, true);
}

// ---------------------------------------------------------------------------------------------
// skin weights

struct Influence {
    int j[4] = {0, 0, 0, 0};
    float w[4] = {1, 0, 0, 0};
};
using WeightFn = std::function<Influence(const Vec3&)>;

// bones along a polyline; weights blend smoothly within `blend` metres of each interior joint
struct Chain {
    std::vector<Vec3> pts;
    std::vector<int> bones;    // pts.size() - 1
    std::vector<float> blend;  // interior joints: pts.size() - 2

    Influence operator()(const Vec3& p) const {
        std::vector<float> L(pts.size(), 0.0f);
        for (size_t k = 1; k < pts.size(); ++k) L[k] = L[k - 1] + (pts[k] - pts[k - 1]).length();
        float best = 1e9f, s = 0.0f;
        size_t seg = 0;
        for (size_t k = 0; k + 1 < pts.size(); ++k) {
            Vec3 a = pts[k], b = pts[k + 1];
            Vec3 ab = b - a;
            float t = saturate(dot(p - a, ab) / std::max(dot(ab, ab), 1e-8f));
            float d = (p - (a + ab * t)).length();
            if (d < best) {
                best = d;
                s = L[k] + t * ab.length();
                seg = k;
            }
        }
        Influence in;
        in.j[0] = bones[seg];
        for (size_t i = 1; i + 1 < pts.size(); ++i) {
            float r = blend[i - 1];
            float ds = s - L[i];
            if (std::fabs(ds) < r) {
                float wn = smoothstep(-1.0f, 1.0f, ds / r);
                in.j[0] = bones[i - 1];
                in.w[0] = 1.0f - wn;
                in.j[1] = bones[i];
                in.w[1] = wn;
                return in;
            }
        }
        return in;
    }
};

const RiderJointDef* J() { return riderJoints(); }
Vec3 rest(int j) { return J()[j].restWorld; }

Chain spineChain() {
    return {{Vec3(0, 0.6f, 0), rest(RJ_Pelvis), rest(RJ_Spine), rest(RJ_Chest), rest(RJ_Neck), rest(RJ_Head), Vec3(0, 2.0f, -0.01f)},
            {RJ_Pelvis, RJ_Pelvis, RJ_Spine, RJ_Chest, RJ_Neck, RJ_Head},
            {0.01f, 0.07f, 0.09f, 0.04f, 0.03f}};
}
Vec3 handDir(bool right) {
    int e = right ? RJ_LowerArmR : RJ_LowerArmL, w = right ? RJ_HandR : RJ_HandL;
    return (rest(w) - rest(e)).normalized();
}
Chain armChain(bool right) {
    float sx = right ? 1.0f : -1.0f;
    int u = right ? RJ_UpperArmR : RJ_UpperArmL, l = right ? RJ_LowerArmR : RJ_LowerArmL, h = right ? RJ_HandR : RJ_HandL;
    return {{Vec3(sx * 0.05f, 1.42f, 0), rest(u), rest(l), rest(h), rest(h) + handDir(right) * 0.22f},
            {RJ_Chest, u, l, h},
            {0.05f, 0.05f, 0.025f}};
}
Chain legChain(bool right) {
    float sx = right ? 1.0f : -1.0f;
    int t = right ? RJ_ThighR : RJ_ThighL, s = right ? RJ_ShinR : RJ_ShinL, f = right ? RJ_FootR : RJ_FootL;
    return {{Vec3(sx * 0.1f, 1.15f, 0), rest(t), rest(s), rest(f), Vec3(sx * 0.1f, 0.02f, -0.17f)},
            {RJ_Pelvis, t, s, f},
            {0.07f, 0.06f, 0.035f}};
}
// both legs + pelvis for the hip block of the pants: the crotch centre stays with the pelvis
Influence hipWeights(const Vec3& p) {
    Influence in = legChain(p.x > 0.0f)(p);
    float k = saturate(std::fabs(p.x) / 0.08f);
    float pel = 0.0f;
    for (int i = 0; i < 4; ++i) {
        if (in.j[i] == RJ_Pelvis) continue;
        pel += in.w[i] * (1.0f - k);
        in.w[i] *= k;
    }
    for (int i = 0; i < 4; ++i)
        if (in.w[i] == 0.0f || in.j[i] == RJ_Pelvis) {
            in.j[i] = RJ_Pelvis;
            in.w[i] += pel;
            break;
        }
    return in;
}
// shoes: rigid on the foot, the collar blends into the shin
Influence shoeWeights(const Vec3& p) {
    bool right = p.x > 0.0f;
    Influence in;
    float ws = smoothstep(0.11f, 0.17f, p.y);
    in.j[0] = right ? RJ_FootR : RJ_FootL;
    in.w[0] = 1.0f - ws;
    in.j[1] = right ? RJ_ShinR : RJ_ShinL;
    in.w[1] = ws;
    return in;
}

// ---------------------------------------------------------------------------------------------
// model assembly

enum Mat { MSkin = 0, MTop, MPants, MShoes, MHelmet, MHair, MEye, MSole, MStrap, MCount };

struct RiderBuilder {
    std::map<std::string, GMesh> meshes;
    std::vector<std::string> order;

    void emit(const std::string& mesh, int material, Geo& g, const WeightFn& wf) {
        // smooth normals: accumulate face normals, then weld across coincident positions
        for (auto& nn : g.n) nn = Vec3(0);
        for (size_t i = 0; i + 2 < g.idx.size(); i += 3) {
            uint32_t a = g.idx[i], b = g.idx[i + 1], c = g.idx[i + 2];
            Vec3 fn = cross(g.p[b] - g.p[a], g.p[c] - g.p[a]);
            g.n[a] += fn;
            g.n[b] += fn;
            g.n[c] += fn;
        }
        std::unordered_map<uint64_t, Vec3> weld;
        auto key = [](const Vec3& p) {
            auto q = [](float f) { return uint64_t(int64_t(std::lround(f * 20000.0f)) & 0x1FFFFF); };
            return q(p.x) | (q(p.y) << 21) | (q(p.z) << 42);
        };
        for (size_t i = 0; i < g.p.size(); ++i) weld[key(g.p[i])] += g.n[i];
        MeshData md;
        md.vertices.resize(g.p.size());
        for (size_t i = 0; i < g.p.size(); ++i) {
            Vec3 nn = weld[key(g.p[i])].normalized();
            md.vertices[i].position = g.p[i];
            md.vertices[i].normal = nn.lengthSq() > 0.5f ? nn : Vec3(0, 1, 0);
            md.vertices[i].uv = g.uv[i];
        }
        md.indices = g.idx;
        md.computeTangents();

        if (!meshes.count(mesh)) {
            order.push_back(mesh);
            meshes[mesh].name = mesh;
            meshes[mesh].skinned = true;
        }
        GMesh& gm = meshes[mesh];
        GPrimitive* prim = nullptr;
        for (auto& pr : gm.primitives)
            if (pr.material == material) prim = &pr;
        if (!prim) {
            gm.primitives.push_back(GPrimitive());
            prim = &gm.primitives.back();
            prim->material = material;
        }
        uint32_t base = uint32_t(prim->vertices.size());
        for (auto& v : md.vertices) {
            GVertex gv;
            gv.pos = v.position;
            gv.normal = v.normal;
            gv.tangent = v.tangent;
            gv.uv = v.uv;
            Influence in = wf(v.position);
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += in.w[k];
            for (int k = 0; k < 4; ++k) {
                gv.joints[k] = uint8_t(in.j[k]);
                gv.weights[k] = sum > 0 ? in.w[k] / sum : (k == 0 ? 1.0f : 0.0f);
            }
            prim->vertices.push_back(gv);
        }
        for (uint32_t i : md.indices) prim->indices.push_back(base + i);
        g = Geo();
    }
};

// torso cross sections: y, half width, half depth, z offset (front = -Z)
struct TorsoSpec {
    float y, rx, rz, z, sq;
};
std::vector<Ring> torsoRings(const std::vector<TorsoSpec>& spec, float grow) {
    std::vector<Ring> r;
    for (auto& s : spec) r.push_back({Vec3(0, s.y, s.z), s.rx + grow, s.rz + grow, s.sq});
    return r;
}

void buildBody(RiderBuilder& rb) {
    Chain spine = spineChain();
    Geo g;
    // --- head: skull, jaw, nose, ears; neck
    ellipsoid(g, Vec3(0, 1.705f, -0.004f), Vec3(0.088f, 0.11f, 0.102f), Quat(), 16, 24);
    ellipsoid(g, Vec3(0, 1.648f, -0.036f), Vec3(0.068f, 0.058f, 0.066f), Quat(), 10, 20);
    ellipsoid(g, Vec3(0, 1.688f, -0.104f), Vec3(0.015f, 0.026f, 0.02f), Quat::angleAxis(-0.25f, Vec3(1, 0, 0)), 6, 10);
    for (float sx : {-1.0f, 1.0f})
        ellipsoid(g, Vec3(sx * 0.087f, 1.694f, 0.006f), Vec3(0.013f, 0.03f, 0.02f), Quat::angleAxis(sx * 0.2f, Vec3(0, 1, 0)), 6, 10);
    loft(g, {{Vec3(0, 1.44f, 0.0f), 0.062f, 0.058f}, {Vec3(0, 1.53f, 0.006f), 0.054f, 0.052f}, {Vec3(0, 1.63f, 0.0f), 0.05f, 0.05f}}, 16, true, true);
    rb.emit("body", MSkin, g, spine);
    // --- hands: flat mitts along the forearm with a thumb
    for (bool right : {false, true}) {
        float sx = right ? 1.0f : -1.0f;
        Vec3 w = rest(right ? RJ_HandR : RJ_HandL);
        Vec3 d = handDir(right);
        Vec3 fwd = (Vec3(0, 0, -1) - d * dot(Vec3(0, 0, -1), d)).normalized();
        Vec3 lat = cross(d, fwd).normalized();
        std::vector<Ring> hand = {{w - d * 0.02f, 0.024f, 0.03f},
                                  {w + d * 0.02f, 0.027f, 0.04f},
                                  {w + d * 0.07f, 0.025f, 0.045f, 2.6f},
                                  {w + d * 0.1f, 0.022f, 0.045f, 2.6f},
                                  {w + d * 0.14f, 0.018f, 0.04f, 2.4f},
                                  {w + d * 0.175f, 0.012f, 0.03f}};
        loft(g, hand, 14, true, true, lat);
        Vec3 t0 = w + d * 0.035f + fwd * 0.03f, t1 = w + d * 0.075f + fwd * 0.055f - lat * sx * 0.0f, t2 = w + d * 0.11f + fwd * 0.058f;
        loft(g, {{t0, 0.013f, 0.013f}, {t1, 0.012f, 0.012f}, {t2, 0.01f, 0.01f}}, 8, true, true);
        rb.emit("body", MSkin, g, armChain(right));
    }
    // eyes
    for (float sx : {-1.0f, 1.0f}) ellipsoid(g, Vec3(sx * 0.032f, 1.716f, -0.093f), Vec3(0.012f), Quat(), 6, 10);
    rb.emit("body", MEye, g, spine);

    // --- bare arms (visible unless the top has long sleeves)
    for (bool right : {false, true}) {
        float sx = right ? 1.0f : -1.0f;
        int u = right ? RJ_UpperArmR : RJ_UpperArmL, l = right ? RJ_LowerArmR : RJ_LowerArmL, h = right ? RJ_HandR : RJ_HandL;
        Vec3 S = rest(u), E = rest(l), W = rest(h);
        std::vector<Ring> arm = {{Vec3(sx * 0.15f, 1.47f, 0.0f), 0.045f, 0.045f},
                                 {S + Vec3(-sx * 0.005f, 0.01f, 0), 0.056f, 0.054f},
                                 {lerp(S, E, 0.35f), 0.052f, 0.05f},
                                 {lerp(S, E, 0.75f), 0.045f, 0.044f},
                                 {E, 0.041f, 0.041f},
                                 {lerp(E, W, 0.3f), 0.043f, 0.039f},
                                 {lerp(E, W, 0.75f), 0.035f, 0.03f},
                                 {W, 0.03f, 0.026f}};
        loft(g, arm, 16, true, true);
        rb.emit("arms_skin", MSkin, g, armChain(right));
    }
    // --- bare lower legs (shorts)
    for (bool right : {false, true}) {
        float sx = right ? 1.0f : -1.0f;
        std::vector<Ring> leg = {{Vec3(sx * 0.1f, 0.68f, -0.01f), 0.068f, 0.07f},
                                 {Vec3(sx * 0.1f, 0.58f, -0.022f), 0.058f, 0.06f},
                                 {Vec3(sx * 0.1f, 0.52f, -0.028f), 0.054f, 0.056f},
                                 {Vec3(sx * 0.1f, 0.4f, -0.006f), 0.054f, 0.062f},
                                 {Vec3(sx * 0.1f, 0.26f, 0.008f), 0.045f, 0.048f},
                                 {Vec3(sx * 0.1f, 0.12f, 0.018f), 0.036f, 0.038f}};
        loft(g, leg, 16, true, true);
        rb.emit("legs_skin", MSkin, g, legChain(right));
    }
    // --- hair (hidden under the helmet)
    ellipsoid(g, Vec3(0, 1.716f, 0.004f), Vec3(0.093f, 0.104f, 0.108f), Quat::angleAxis(0.42f, Vec3(1, 0, 0)), 12, 24, 1.62f);
    rb.emit("hair", MHair, g, spine);
}

const std::vector<TorsoSpec>& shirtSpec() {
    static const std::vector<TorsoSpec> s = {
        {0.95f, 0.176f, 0.124f, 0.0f, 2.2f},  {1.0f, 0.172f, 0.12f, 0.0f, 2.2f},    {1.08f, 0.16f, 0.112f, -0.002f, 2.2f},
        {1.18f, 0.156f, 0.112f, -0.006f, 2.3f}, {1.28f, 0.168f, 0.122f, -0.012f, 2.4f}, {1.37f, 0.182f, 0.124f, -0.01f, 2.6f},
        {1.44f, 0.186f, 0.11f, -0.002f, 2.8f}, {1.49f, 0.15f, 0.09f, 0.0f, 2.4f},     {1.515f, 0.075f, 0.07f, 0.004f, 2.0f}};
    return s;
}

void sleeve(Geo& g, bool right, float length, float grow) {
    float sx = right ? 1.0f : -1.0f;
    int u = right ? RJ_UpperArmR : RJ_UpperArmL, l = right ? RJ_LowerArmR : RJ_LowerArmL, h = right ? RJ_HandR : RJ_HandL;
    Vec3 S = rest(u), E = rest(l), W = rest(h);
    std::vector<Ring> r = {{Vec3(sx * 0.14f, 1.475f, 0.0f), 0.055f + grow, 0.055f + grow}, {S + Vec3(-sx * 0.005f, 0.012f, 0), 0.066f + grow, 0.064f + grow}};
    if (length <= 0.5f) {
        r.push_back({lerp(S, E, length), 0.062f + grow, 0.06f + grow});
    } else {
        r.push_back({lerp(S, E, 0.5f), 0.062f + grow, 0.06f + grow});
        r.push_back({E, 0.056f + grow, 0.055f + grow});
        r.push_back({lerp(E, W, 0.6f), 0.05f + grow, 0.047f + grow});
        r.push_back({W + (W - E).normalized() * 0.005f, 0.044f + grow, 0.04f + grow});
    }
    loft(g, r, 16, true, true);
}

void buildTops(RiderBuilder& rb) {
    Chain spine = spineChain();
    Geo g;
    // 0: t-shirt
    loft(g, torsoRings(shirtSpec(), 0.0f), 28, true, true);
    rb.emit("top_0", MTop, g, spine);
    for (bool right : {false, true}) {
        sleeve(g, right, 0.45f, 0.0f);
        rb.emit("top_0", MTop, g, armChain(right));
    }
    // 1: hoodie - looser, longer, long sleeves, hood folded on the back, front pocket
    std::vector<TorsoSpec> hs = shirtSpec();
    hs.front().y = 0.9f;
    for (auto& s : hs) {
        s.rx += 0.012f;
        s.rz += 0.014f;
    }
    loft(g, torsoRings(hs, 0.0f), 28, true, true);
    ellipsoid(g, Vec3(0, 1.505f, 0.085f), Vec3(0.12f, 0.07f, 0.075f), Quat::angleAxis(-0.35f, Vec3(1, 0, 0)), 10, 20);
    loft(g, {{Vec3(0, 1.555f, 0.03f), 0.095f, 0.07f, 2.0f}, {Vec3(0, 1.53f, 0.035f), 0.105f, 0.085f, 2.0f}}, 20, true, true);
    // kangaroo pocket
    loft(g, {{Vec3(0, 0.99f, -0.118f), 0.11f, 0.02f, 3.0f}, {Vec3(0, 1.07f, -0.114f), 0.1f, 0.018f, 3.0f}}, 20, true, true);
    rb.emit("top_1", MTop, g, spine);
    for (bool right : {false, true}) {
        sleeve(g, right, 1.0f, 0.004f);
        rb.emit("top_1", MTop, g, armChain(right));
    }
    // drawstrings
    for (float sx : {-1.0f, 1.0f}) tube(g, {Vec3(sx * 0.035f, 1.49f, -0.1f), Vec3(sx * 0.04f, 1.41f, -0.132f), Vec3(sx * 0.042f, 1.34f, -0.138f)}, 0.0045f, 6);
    rb.emit("top_1", MStrap, g, spine);
    // 2: tank top - narrow straps over the shoulders, bare arms
    std::vector<TorsoSpec> ts = shirtSpec();
    ts[6] = {1.44f, 0.152f, 0.108f, -0.002f, 2.4f};
    ts[7] = {1.49f, 0.12f, 0.085f, 0.0f, 2.2f};
    loft(g, torsoRings(ts, -0.002f), 28, true, true);
    rb.emit("top_2", MTop, g, spine);
}

void buildPants(RiderBuilder& rb) {
    Geo g;
    auto hip = [&](float grow) {
        std::vector<Ring> r = {{Vec3(0, 1.06f, 0.004f), 0.152f + grow, 0.108f + grow, 2.3f},
                               {Vec3(0, 0.99f, 0.006f), 0.162f + grow, 0.113f + grow, 2.3f},
                               {Vec3(0, 0.92f, 0.008f), 0.17f + grow, 0.116f + grow, 2.4f},
                               {Vec3(0, 0.86f, 0.006f), 0.16f + grow, 0.11f + grow, 2.4f},
                               {Vec3(0, 0.81f, 0.004f), 0.1f, 0.07f, 2.0f}};
        loft(g, r, 28, true, true);
    };
    // 0: jeans
    hip(0.0f);
    // belt
    loft(g, {{Vec3(0, 1.035f, 0.004f), 0.156f, 0.112f, 2.3f}, {Vec3(0, 1.065f, 0.004f), 0.155f, 0.111f, 2.3f}}, 28, true, true);
    rb.emit("pants_0", MPants, g, hipWeights);
    for (bool right : {false, true}) {
        float sx = right ? 1.0f : -1.0f;
        std::vector<Ring> leg = {{Vec3(sx * 0.09f, 0.97f, 0.004f), 0.09f, 0.1f},
                                 {Vec3(sx * 0.1f, 0.86f, 0.0f), 0.088f, 0.094f},
                                 {Vec3(sx * 0.1f, 0.7f, -0.012f), 0.074f, 0.078f},
                                 {Vec3(sx * 0.1f, 0.56f, -0.024f), 0.066f, 0.068f},
                                 {Vec3(sx * 0.1f, 0.5f, -0.026f), 0.064f, 0.066f},
                                 {Vec3(sx * 0.1f, 0.36f, -0.006f), 0.062f, 0.066f},
                                 {Vec3(sx * 0.1f, 0.2f, 0.01f), 0.058f, 0.062f},
                                 {Vec3(sx * 0.1f, 0.12f, 0.018f), 0.06f, 0.064f}};
        loft(g, leg, 18, true, true);
        rb.emit("pants_0", MPants, g, legChain(right));
    }
    // 1: shorts
    hip(0.004f);
    rb.emit("pants_1", MPants, g, hipWeights);
    for (bool right : {false, true}) {
        float sx = right ? 1.0f : -1.0f;
        std::vector<Ring> leg = {{Vec3(sx * 0.09f, 0.97f, 0.004f), 0.094f, 0.104f},
                                 {Vec3(sx * 0.1f, 0.84f, 0.0f), 0.094f, 0.1f},
                                 {Vec3(sx * 0.1f, 0.68f, -0.01f), 0.088f, 0.09f},
                                 {Vec3(sx * 0.1f, 0.6f, -0.014f), 0.088f, 0.09f}};
        loft(g, leg, 18, true, true);
        rb.emit("pants_1", MPants, g, legChain(right));
    }
}

void buildShoes(RiderBuilder& rb) {
    struct S {
        float z, hw, h;
    };
    const std::vector<S> upper = {{0.1f, 0.028f, 0.06f},  {0.088f, 0.042f, 0.082f}, {0.045f, 0.047f, 0.092f}, {0.0f, 0.048f, 0.088f},
                                  {-0.05f, 0.05f, 0.072f}, {-0.1f, 0.052f, 0.058f},  {-0.14f, 0.048f, 0.046f}, {-0.165f, 0.038f, 0.034f},
                                  {-0.178f, 0.02f, 0.02f}};
    for (int variant = 0; variant < 2; ++variant) {
        std::string name = "shoes_" + std::to_string(variant);
        for (bool right : {false, true}) {
            float sx = right ? 1.0f : -1.0f;
            Geo g;
            std::vector<Ring> r;
            for (auto& s : upper) {
                float bottom = 0.014f;
                r.push_back({Vec3(sx * 0.1f, bottom + s.h * 0.5f, s.z), s.hw, s.h * 0.5f, 2.8f});
            }
            loft(g, r, 20, true, true);
            if (variant == 1) {
                // high top collar around the ankle
                loft(g, {{Vec3(sx * 0.1f, 0.07f, 0.025f), 0.05f, 0.06f, 2.2f}, {Vec3(sx * 0.1f, 0.13f, 0.022f), 0.052f, 0.058f, 2.2f},
                         {Vec3(sx * 0.1f, 0.175f, 0.02f), 0.05f, 0.055f, 2.0f}},
                     18, true, true);
            } else {
                // tongue
                loft(g, {{Vec3(sx * 0.1f, 0.075f, -0.02f), 0.03f, 0.012f, 3.0f}, {Vec3(sx * 0.1f, 0.115f, 0.0f), 0.028f, 0.01f, 3.0f}}, 12, true, true);
            }
            rb.emit(name, MShoes, g, shoeWeights);
            // sole
            std::vector<Ring> sole;
            for (auto& s : upper) sole.push_back({Vec3(sx * 0.1f, 0.012f, s.z + (s.z > 0 ? 0.004f : -0.004f)), s.hw + 0.005f, 0.012f, 3.5f});
            loft(g, sole, 20, true, true);
            rb.emit(name, MSole, g, shoeWeights);
            // laces
            for (int k = 0; k < 4; ++k) {
                float z = -0.085f + float(k) * 0.025f;
                float y = 0.014f + 0.068f + float(k) * 0.006f;
                tube(g, {Vec3(sx * 0.1f - 0.022f, y, z), Vec3(sx * 0.1f, y + 0.004f, z), Vec3(sx * 0.1f + 0.022f, y, z)}, 0.0035f, 5);
            }
            rb.emit(name, MStrap, g, shoeWeights);
        }
    }
}

void buildHeadwear(RiderBuilder& rb) {
    Chain spine = spineChain();
    Geo g;
    // 1: skate helmet (thick shell + chin strap)
    Quat tilt = Quat::angleAxis(0.26f, Vec3(1, 0, 0));
    shell(g, Vec3(0, 1.712f, 0.004f), Vec3(0.118f, 0.128f, 0.132f), 0.02f, tilt, 1.72f, 14, 28);
    rb.emit("helmet_1", MHelmet, g, spine);
    for (float sx : {-1.0f, 1.0f}) tube(g, {Vec3(sx * 0.1f, 1.665f, 0.02f), Vec3(sx * 0.086f, 1.62f, -0.02f), Vec3(sx * 0.05f, 1.59f, -0.06f), Vec3(0, 1.584f, -0.075f)}, 0.005f, 6);
    rb.emit("helmet_1", MStrap, g, spine);
    // 2: cap with a curved brim
    ellipsoid(g, Vec3(0, 1.722f, 0.006f), Vec3(0.097f, 0.1f, 0.11f), Quat::angleAxis(0.22f, Vec3(1, 0, 0)), 10, 24, 1.62f);
    {
        const int n = 16;
        std::vector<uint32_t> top, bot, topO, botO;
        for (int i = 0; i <= n; ++i) {
            float a = (-0.5f + float(i) / float(n)) * 2.3f;  // radians around the front
            Vec3 dir(std::sin(a), 0, -std::cos(a));
            float reach = 0.075f * std::cos(a * 0.62f);
            float yIn = 1.748f, yOut = yIn - 0.028f * std::cos(a * 0.7f) - 0.01f;
            Vec3 inner = Vec3(0, yIn, 0.006f) + Vec3(dir.x * 0.098f, 0, dir.z * 0.11f);
            Vec3 outer = inner + dir * reach + Vec3(0, yOut - yIn, 0);
            top.push_back(g.vert(inner + Vec3(0, 0.004f, 0), Vec2(float(i) * 0.02f, 0)));
            topO.push_back(g.vert(outer + Vec3(0, 0.004f, 0), Vec2(float(i) * 0.02f, reach)));
            bot.push_back(g.vert(inner - Vec3(0, 0.004f, 0), Vec2(float(i) * 0.02f, 0)));
            botO.push_back(g.vert(outer - Vec3(0, 0.004f, 0), Vec2(float(i) * 0.02f, reach)));
        }
        for (int i = 0; i < n; ++i) {
            g.quad(top[size_t(i)], topO[size_t(i)], topO[size_t(i) + 1], top[size_t(i) + 1], Vec3(0, 1, 0));
            g.quad(bot[size_t(i)], botO[size_t(i)], botO[size_t(i) + 1], bot[size_t(i) + 1], Vec3(0, -1, 0));
            Vec3 out = (g.p[topO[size_t(i)]] - Vec3(0, 1.74f, 0));
            out.y = 0;
            g.quad(topO[size_t(i)], botO[size_t(i)], botO[size_t(i) + 1], topO[size_t(i) + 1], out);
        }
        g.quad(top[0], bot[0], botO[0], topO[0], Vec3(-1, 0, 0));
        g.quad(top[n], bot[n], botO[n], topO[n], Vec3(1, 0, 0));
    }
    // button on top
    ellipsoid(g, Vec3(0, 1.82f, 0.02f), Vec3(0.012f, 0.007f, 0.012f), Quat(), 4, 8);
    rb.emit("helmet_2", MHelmet, g, spine);
}

// ---------------------------------------------------------------------------------------------
// animation clips: joint euler angles in degrees (Ry * Rx * Rz), pelvis offset from the rest pose
//   hip flexion +X, knee flexion -X, spine forward bend -X, elbow flexion +X,
//   right arm abduction +Z (left -Z), yaw +Y turns left

struct PoseDef {
    Vec3 e[RJ_Count];
    Vec3 pelvis;
    PoseDef& set(int j, float x, float y = 0.0f, float z = 0.0f) {
        e[j] = Vec3(x, y, z);
        return *this;
    }
    // mirrored pair (left gets -y, -z)
    PoseDef& pair(int jl, int jr, float x, float y = 0.0f, float z = 0.0f) {
        e[jl] = Vec3(x, -y, -z);
        e[jr] = Vec3(x, y, z);
        return *this;
    }
    PoseDef& add(int j, float x, float y = 0.0f, float z = 0.0f) {
        e[j] += Vec3(x, y, z);
        return *this;
    }
    PoseDef& hips(float x, float y, float z) {
        pelvis = Vec3(x, y, z);
        return *this;
    }
};

PoseDef ridePose() {
    // regular stance: left foot forward, hips open towards the back foot, shoulders square to the bars
    PoseDef p;
    p.hips(0, -0.11f, 0.03f)
        .set(RJ_Pelvis, -8, -18, 0)
        .set(RJ_Spine, -14, 8, 0)
        .set(RJ_Chest, -8, 6, 0)
        .set(RJ_Neck, 12, 4, 0)
        .set(RJ_Head, 8)
        .pair(RJ_UpperArmL, RJ_UpperArmR, 34, 0, 10)
        .pair(RJ_LowerArmL, RJ_LowerArmR, 30)
        .pair(RJ_ThighL, RJ_ThighR, 32)
        .pair(RJ_ShinL, RJ_ShinR, -55)
        .pair(RJ_FootL, RJ_FootR, 22);
    return p;
}
PoseDef tuckPose() {
    PoseDef p = ridePose();
    p.hips(0, -0.08f, 0.02f).set(RJ_Spine, -20).set(RJ_Chest, -8).set(RJ_Neck, 16);
    p.pair(RJ_ThighL, RJ_ThighR, 88, 0, 6).pair(RJ_ShinL, RJ_ShinR, -122).pair(RJ_FootL, RJ_FootR, 34);
    return p;
}

struct ClipDef {
    std::string name;
    std::vector<std::pair<float, PoseDef>> keys;
};

std::vector<ClipDef> clipDefs() {
    std::vector<ClipDef> c;
    PoseDef B = ridePose();
    auto hold = [&](const std::string& n, const PoseDef& p) { c.push_back({n, {{0.0f, p}, {1.0f, p}}}); };

    // idle: upright, breathing
    {
        PoseDef a = B;
        a.hips(0, -0.06f, 0.02f).set(RJ_Pelvis, -4, -16, 0).set(RJ_Spine, -8, 6, 0).set(RJ_Chest, -4, 6, 0).set(RJ_Neck, 6, 4, 0).set(RJ_Head, 3);
        PoseDef b = a;
        b.add(RJ_Chest, -2).add(RJ_Neck, 1).add(RJ_Head, 1, 4, 0);
        b.pelvis.y -= 0.006f;
        c.push_back({"idle", {{0.0f, a}, {1.4f, b}, {2.8f, a}}});
    }
    // ride: small weight shifts
    {
        PoseDef a = B, b = B;
        b.add(RJ_Spine, -1, 3, 0).add(RJ_Head, 0, -3, 0);
        b.pelvis.y -= 0.008f;
        c.push_back({"ride", {{0.0f, a}, {0.8f, b}, {1.6f, a}}});
    }
    // push: back (right) foot plants beside the deck and kicks back; front knee bends
    {
        PoseDef k0 = B, k1 = B, k2 = B, k3 = B;
        k1.hips(0, -0.1f, 0.03f).set(RJ_ThighL, 45).set(RJ_ShinL, -75).set(RJ_ThighR, 8, 0, 9).set(RJ_ShinR, -22).set(RJ_FootR, 5);
        k1.add(RJ_Spine, -4);
        k2.hips(0, -0.1f, 0.02f).set(RJ_ThighL, 42).set(RJ_ShinL, -72).set(RJ_ThighR, -28, 0, 7).set(RJ_ShinR, -12).set(RJ_FootR, -25);
        k2.add(RJ_Spine, -6).add(RJ_Chest, -2);
        k3.set(RJ_ThighR, 30).set(RJ_ShinR, -70).set(RJ_FootR, 10);
        c.push_back({"push", {{0.0f, k0}, {0.12f, k1}, {0.25f, k2}, {0.34f, k3}, {0.42f, k0}}});
    }
    // crouch (pop charge, compression)
    {
        PoseDef p = B;
        p.hips(0, -0.25f, 0.06f)
            .set(RJ_Pelvis, -10)
            .set(RJ_Spine, -24)
            .set(RJ_Chest, -12)
            .set(RJ_Neck, 24)
            .set(RJ_Head, 12)
            .pair(RJ_UpperArmL, RJ_UpperArmR, 22, 0, 12)
            .pair(RJ_LowerArmL, RJ_LowerArmR, 72)
            .pair(RJ_ThighL, RJ_ThighR, 72)
            .pair(RJ_ShinL, RJ_ShinR, -112)
            .pair(RJ_FootL, RJ_FootR, 38);
        hold("crouch", p);
    }
    // air: knees up, compact
    {
        PoseDef a = B;
        a.hips(0, -0.12f, 0.03f).set(RJ_Spine, -16).set(RJ_Chest, -8).set(RJ_Neck, 14).pair(RJ_ThighL, RJ_ThighR, 48).pair(RJ_ShinL, RJ_ShinR, -80);
        PoseDef b = a;
        b.add(RJ_Spine, -2).add(RJ_Head, 2);
        c.push_back({"air", {{0.0f, a}, {0.6f, b}, {1.2f, a}}});
    }
    // landings: absorb and recover
    {
        PoseDef d = B;
        d.hips(0, -0.2f, 0.05f).set(RJ_Spine, -24).set(RJ_Chest, -10).set(RJ_Neck, 22).pair(RJ_LowerArmL, RJ_LowerArmR, 60);
        c.push_back({"land", {{0.0f, d}, {0.3f, B}}});
        PoseDef h = d;
        h.hips(0, -0.3f, 0.07f).set(RJ_Spine, -36).set(RJ_Chest, -14).set(RJ_Neck, 32).pair(RJ_LowerArmL, RJ_LowerArmR, 85);
        c.push_back({"hard_land", {{0.0f, h}, {0.2f, h}, {0.55f, B}}});
    }
    // manuals
    {
        PoseDef m = B;
        m.hips(0, -0.04f, 0.07f).set(RJ_Pelvis, 6).set(RJ_Spine, 6).set(RJ_Chest, 2).set(RJ_Neck, -4).set(RJ_Head, -2);
        m.pair(RJ_UpperArmL, RJ_UpperArmR, 52, 0, 8).pair(RJ_LowerArmL, RJ_LowerArmR, 6);
        PoseDef m2 = m;
        m2.add(RJ_Spine, 1, 0, 2).add(RJ_UpperArmL, 0, 0, -4);
        c.push_back({"manual", {{0.0f, m}, {0.7f, m2}, {1.4f, m}}});
        PoseDef n = B;
        n.hips(0, -0.12f, -0.04f).set(RJ_Pelvis, -12).set(RJ_Spine, -26).set(RJ_Chest, -10).set(RJ_Neck, 26).set(RJ_Head, 12);
        n.pair(RJ_UpperArmL, RJ_UpperArmR, 20, 0, 12).pair(RJ_LowerArmL, RJ_LowerArmR, 76);
        PoseDef n2 = n;
        n2.add(RJ_Spine, -1, 0, -2);
        c.push_back({"nose_manual", {{0.0f, n}, {0.7f, n2}, {1.4f, n}}});
    }
    // grinds: low and centred, arms balance
    {
        PoseDef g = B;
        g.hips(0, -0.13f, 0.02f).set(RJ_Spine, -18).set(RJ_Chest, -6).set(RJ_Neck, 14).set(RJ_Head, 6);
        g.pair(RJ_ThighL, RJ_ThighR, 45).pair(RJ_ShinL, RJ_ShinR, -76).pair(RJ_UpperArmL, RJ_UpperArmR, 35, 0, 14).pair(RJ_LowerArmL, RJ_LowerArmR, 42);
        PoseDef g2 = g;
        g2.add(RJ_Spine, 0, 0, 3).add(RJ_Chest, 0, 0, 2);
        PoseDef g3 = g;
        g3.add(RJ_Spine, 0, 0, -3).add(RJ_Chest, 0, 0, -2);
        c.push_back({"grind", {{0.0f, g}, {0.5f, g2}, {1.0f, g}, {1.5f, g3}, {2.0f, g}}});
        PoseDef b = g;
        b.set(RJ_Pelvis, -6, 40, 0).set(RJ_Spine, -14, 22, 0).set(RJ_Chest, -4, 8, 0).set(RJ_Neck, 10, -35, 0).set(RJ_Head, 4, -30, 0);
        PoseDef b2 = b;
        b2.add(RJ_Chest, 0, 0, 3);
        c.push_back({"boardslide", {{0.0f, b}, {0.6f, b2}, {1.2f, b}}});
    }
    // trick poses (sampled as held overlays while the trick runs)
    {
        hold("whip", tuckPose());
        PoseDef hw = tuckPose();
        hw.set(RJ_ThighR, 62, 0, 4).set(RJ_ShinR, -18).set(RJ_FootR, -5);
        hold("heelwhip", hw);
        PoseDef bs = B;
        bs.hips(0, -0.09f, 0.02f).set(RJ_Spine, -14).pair(RJ_UpperArmL, RJ_UpperArmR, 62, 0, 26).pair(RJ_LowerArmL, RJ_LowerArmR, 48);
        hold("barspin", bs);
        PoseDef fw = tuckPose();
        fw.set(RJ_Spine, -32).set(RJ_Chest, -10).set(RJ_Neck, 30).set(RJ_UpperArmR, 14, 0, 10).set(RJ_LowerArmR, 22);
        hold("fingerwhip", fw);
        PoseDef nh = B;
        nh.hips(0, -0.07f, 0.02f).set(RJ_Spine, 2).set(RJ_Chest, 4).set(RJ_Head, -6);
        nh.pair(RJ_UpperArmL, RJ_UpperArmR, 18, 0, 86).pair(RJ_LowerArmL, RJ_LowerArmR, 16);
        hold("no_hand", nh);
        PoseDef oh = B;
        oh.set(RJ_Spine, -8).set(RJ_UpperArmR, 26, 0, 82).set(RJ_LowerArmR, 18);
        hold("one_hand", oh);
        PoseDef cc = tuckPose();
        cc.set(RJ_ThighL, 62, 0, 38).set(RJ_ShinL, -16).set(RJ_FootL, -10).set(RJ_Pelvis, -4, -12, 0);
        hold("can_can", cc);
        PoseDef nf = B;
        nf.hips(0, -0.04f, 0.0f).set(RJ_Spine, -4).pair(RJ_ThighL, RJ_ThighR, -32, 0, 24).pair(RJ_ShinL, RJ_ShinR, -18).pair(RJ_FootL, RJ_FootR, -22);
        nf.pair(RJ_UpperArmL, RJ_UpperArmR, 58, 0, 6).pair(RJ_LowerArmL, RJ_LowerArmR, 10);
        hold("no_footer", nf);
        PoseDef gr = tuckPose();
        gr.set(RJ_Spine, -38).set(RJ_Chest, -14).set(RJ_Neck, 34).set(RJ_UpperArmR, 34, 0, 6).set(RJ_LowerArmR, 12);
        gr.pair(RJ_ThighL, RJ_ThighR, 98, 0, 8).pair(RJ_ShinL, RJ_ShinR, -128);
        hold("grab", gr);
        PoseDef kl = tuckPose();
        kl.pair(RJ_UpperArmL, RJ_UpperArmR, 72, 0, 6).pair(RJ_LowerArmL, RJ_LowerArmR, 8).set(RJ_Spine, -10);
        hold("kickless", kl);
        PoseDef sf = tuckPose();
        sf.hips(0, -0.1f, 0.03f).set(RJ_Spine, -26).pair(RJ_UpperArmL, RJ_UpperArmR, 58, 0, 10).pair(RJ_LowerArmL, RJ_LowerArmR, 30);
        hold("scooter_flip", sf);
    }
    // bail: flailing arms
    {
        PoseDef a;
        a.set(RJ_Spine, 10).pair(RJ_UpperArmL, RJ_UpperArmR, 100, 0, 60).pair(RJ_LowerArmL, RJ_LowerArmR, 30).pair(RJ_ThighL, RJ_ThighR, 30).pair(RJ_ShinL, RJ_ShinR, -40);
        PoseDef b = a;
        b.pair(RJ_UpperArmL, RJ_UpperArmR, 50, 0, 110).pair(RJ_LowerArmL, RJ_LowerArmR, 60).set(RJ_ThighL, 50).set(RJ_ThighR, 10);
        c.push_back({"bail", {{0.0f, a}, {0.35f, b}, {0.7f, a}}});
    }
    return c;
}

Quat eulerDeg(const Vec3& e) { return Quat::euler(e.x * D, e.y * D, e.z * D); }

}  // namespace

bool generateRider(const std::string& outPath) {
    GlbWriter w;
    const GMaterial mats[MCount] = {
        {"rider_skin", {1, 1, 1, 1}, 0.0f, 0.55f, true},   {"rider_top", {1, 1, 1, 1}, 0.0f, 0.88f, true},
        {"rider_pants", {1, 1, 1, 1}, 0.0f, 0.85f, true},  {"rider_shoes", {1, 1, 1, 1}, 0.0f, 0.7f, true},
        {"rider_helmet", {1, 1, 1, 1}, 0.0f, 0.32f, true}, {"rider_hair", {0.16f, 0.1f, 0.06f, 1}, 0.0f, 0.65f, false},
        {"rider_eye", {0.03f, 0.03f, 0.035f, 1}, 0.0f, 0.1f, false}, {"rider_sole", {0.92f, 0.9f, 0.86f, 1}, 0.0f, 0.8f, false},
        {"rider_strap", {0.05f, 0.05f, 0.055f, 1}, 0.0f, 0.7f, false}};
    for (auto& m : mats) w.addMaterial(m);

    // skeleton nodes (rest rotations are identity)
    const RiderJointDef* jd = riderJoints();
    std::vector<int> jointNodes;
    std::vector<Mat4> invBind;
    for (int j = 0; j < RJ_Count; ++j) {
        GNode n;
        n.name = jd[j].name;
        n.parent = jd[j].parent;
        n.translation = jd[j].parent >= 0 ? jd[j].restWorld - jd[jd[j].parent].restWorld : jd[j].restWorld;
        jointNodes.push_back(w.addNode(n));
        invBind.push_back(Mat4::translation(-jd[j].restWorld));
    }
    w.setSkin(jointNodes, invBind);

    RiderBuilder rb;
    buildBody(rb);
    buildTops(rb);
    buildPants(rb);
    buildShoes(rb);
    buildHeadwear(rb);
    size_t tris = 0;
    for (auto& name : rb.order) {
        GMesh& m = rb.meshes[name];
        for (auto& p : m.primitives) tris += p.indices.size() / 3;
        int mi = w.addMesh(m);
        GNode n;
        n.name = name;
        n.mesh = mi;
        n.skinned = true;
        w.addNode(n);
    }

    // clips
    auto defs = clipDefs();
    for (auto& cd : defs) {
        GAnimation a;
        a.name = cd.name;
        for (int j = 0; j < RJ_Count; ++j) {
            GChannel ch;
            ch.node = jointNodes[size_t(j)];
            ch.path = "rotation";
            for (auto& k : cd.keys) {
                Quat q = eulerDeg(k.second.e[j]).normalized();
                ch.times.push_back(k.first);
                ch.values.push_back(Vec4(q.x, q.y, q.z, q.w));
            }
            a.channels.push_back(ch);
        }
        GChannel tr;
        tr.node = jointNodes[RJ_Pelvis];
        tr.path = "translation";
        Vec3 restLocal = jd[RJ_Pelvis].restWorld - jd[RJ_Root].restWorld;
        for (auto& k : cd.keys) {
            Vec3 t = restLocal + k.second.pelvis;
            tr.times.push_back(k.first);
            tr.values.push_back(Vec4(t.x, t.y, t.z, 0));
        }
        a.channels.push_back(tr);
        w.addAnimation(a);
    }
    bool ok = w.write(outPath);
    std::printf("rider: %zu meshes, %zu triangles, %zu clips -> %s (%s)\n", rb.order.size(), tris, defs.size(), outPath.c_str(), ok ? "ok" : "FAILED");
    return ok;
}

}  // namespace sw::tools
