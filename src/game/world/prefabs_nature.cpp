// scoot would - vegetation prefabs: street trees and shrubs built from tapered branch tubes and
// alpha tested leaf cards (foliage atlas from tools/assetgen). Leaf card normals are bent towards
// the crown surface for soft volumetric shading, and a crown occlusion term is stored in the
// magnitude of tangent.w (the shader reads the handedness from its sign).
#include "game/world/prefab_kit.h"

#include "core/math.h"

#include <cmath>

namespace sw::kit {
namespace {

struct TreeRng {
    uint64_t s;
    explicit TreeRng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) {}
    float uni() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return float((s >> 11) & 0xFFFFFF) / float(0x1000000);
    }
    float range(float a, float b) { return a + (b - a) * uni(); }
    Vec3 unit() {
        float z = range(-1.0f, 1.0f), a = range(0.0f, kTwoPi), r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        return Vec3(r * std::cos(a), z, r * std::sin(a));
    }
};

// tube along a polyline with per point radius (parallel transport frames), bark UVs in metres
void taperedTube(MeshBuilder& b, const std::vector<Vec3>& pts, const std::vector<float>& radii, int seg) {
    size_t n = pts.size();
    if (n < 2) return;
    Vec3 t0 = (pts[1] - pts[0]).normalized();
    Vec3 ref = std::fabs(t0.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    Vec3 nrm = cross(t0, ref).normalized();
    float circ = kTwoPi * radii[0];
    std::vector<uint32_t> prev;
    float v = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        Vec3 t = (i == 0 ? pts[1] - pts[0] : (i + 1 == n ? pts[i] - pts[i - 1] : pts[i + 1] - pts[i - 1])).normalized();
        nrm = (nrm - t * dot(nrm, t)).normalized();
        Vec3 bin = cross(t, nrm);
        if (i > 0) v += (pts[i] - pts[i - 1]).length();
        std::vector<uint32_t> ring;
        for (int k = 0; k <= seg; ++k) {
            float a = float(k) / float(seg) * kTwoPi;
            Vec3 d = nrm * std::cos(a) + bin * std::sin(a);
            ring.push_back(b.addVertex(pts[i] + d * radii[i], d, Vec2(float(k) / float(seg) * circ, v)));
        }
        if (i > 0)
            for (int k = 0; k < seg; ++k) b.addQuad(prev[size_t(k)], prev[size_t(k) + 1], ring[size_t(k) + 1], ring[size_t(k)]);
        prev = std::move(ring);
    }
}

struct CardSet {
    std::vector<std::pair<uint32_t, float>> ao;  // vertex -> crown occlusion
};

// one leaf card (twig cluster from the 2 x 2 atlas); stem at the bottom edge near p
void leafCard(MeshBuilder& b, CardSet& cs, TreeRng& r, Vec3 p, Vec3 center, Vec3 radii, float size, float gravity) {
    auto crownNormal = [&](Vec3 q) {
        Vec3 d = (q - center);
        return Vec3(d.x / (radii.x * radii.x), d.y / (radii.y * radii.y), d.z / (radii.z * radii.z)).normalized();
    };
    auto crownDist = [&](Vec3 q) {
        Vec3 d = q - center;
        return std::sqrt(sqr(d.x / radii.x) + sqr(d.y / radii.y) + sqr(d.z / radii.z));
    };
    Vec3 out = crownNormal(p);
    Vec3 n = (r.unit() + out * 1.1f).normalized();
    // the twig grows outwards and a little upwards, drooping with gravity
    Vec3 up0 = (out * 0.6f + Vec3(0, 1.0f - gravity, 0) + r.unit() * 0.4f).normalized();
    Vec3 up = up0 - n * dot(up0, n);
    if (up.lengthSq() < 1e-4f) up = std::fabs(n.y) < 0.9f ? Vec3(0, 1, 0) - n * n.y : Vec3(1, 0, 0);
    up = up.normalized();
    Vec3 rt = cross(up, n).normalized();
    float s = size * r.range(0.8f, 1.2f);
    Vec3 base = p - up * s * 0.12f;
    Vec3 c[4] = {base - rt * s * 0.5f, base + rt * s * 0.5f, base + rt * s * 0.5f + up * s, base - rt * s * 0.5f + up * s};
    int q = int(r.uni() * 3.99f);
    float u0 = float(q % 2) * 0.5f, v0 = float(q / 2) * 0.5f;
    bool flip = r.uni() < 0.5f;
    Vec2 uv[4] = {{u0 + 0.01f, v0 + 0.49f}, {u0 + 0.49f, v0 + 0.49f}, {u0 + 0.49f, v0 + 0.01f}, {u0 + 0.01f, v0 + 0.01f}};
    if (flip)
        for (auto& t : uv) t.x = u0 + 0.5f - (t.x - u0);
    uint32_t id[4];
    for (int k = 0; k < 4; ++k) {
        Vec3 nv = lerp(n, crownNormal(c[k]), 0.72f).normalized();
        id[k] = b.addVertex(c[k], nv, uv[k]);
        float d = clampf(crownDist(c[k]), 0.0f, 1.2f);
        float ao = clampf(0.28f + 0.72f * std::pow(d, 1.6f), 0.28f, 1.0f);
        ao *= lerpf(0.72f, 1.0f, clampf((c[k].y - (center.y - radii.y)) / (2.0f * radii.y), 0.0f, 1.0f));
        cs.ao.push_back({id[k], std::max(ao, 0.2f)});
    }
    b.addQuad(id[0], id[1], id[2], id[3]);
}

Vec3 pathPoint(const std::vector<Vec3>& p, float t, float* radius = nullptr, const std::vector<float>* radii = nullptr) {
    float f = clampf(t, 0.0f, 1.0f) * float(p.size() - 1);
    size_t i = std::min(size_t(f), p.size() - 2);
    float u = f - float(i);
    if (radius && radii) *radius = lerpf((*radii)[i], (*radii)[i + 1], u);
    return lerp(p[i], p[i + 1], u);
}

void applyAo(MeshData& md, const CardSet& cs) {
    for (auto& [i, ao] : cs.ao) md.vertices[i].tangent.w = (md.vertices[i].tangent.w < 0.0f ? -1.0f : 1.0f) * ao;
}

// deciduous street tree: trunk + leader, 5-8 scaffold branches with twigs, leaf cards on the
// outer part of every branch plus a fill layer on the crown shell
void treePrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    float h = P(j, "height", 7.0f), crown = P(j, "crown", 2.6f);
    int seed = Pi(j, "seed", 1);
    TreeRng r(uint64_t(seed) * 7919u + 13u);
    MeshBuilder b("tree");
    b.setMaterial(0);
    float trunkH = h * r.range(0.36f, 0.46f);
    float r0 = 0.08f + h * 0.012f;
    Vec3 lean(r.range(-0.3f, 0.3f), 0, r.range(-0.3f, 0.3f));
    float leaderH = h - crown * 0.45f;
    std::vector<Vec3> tp;
    std::vector<float> tr;
    for (int i = 0; i <= 9; ++i) {
        float t = float(i) / 9.0f;
        Vec3 wob(std::sin(t * 7.0f + float(seed)) * 0.05f, 0, std::cos(t * 5.0f + float(seed) * 0.7f) * 0.05f);
        tp.push_back(Vec3(0, leaderH * t, 0) + lean * (t * t) + wob * t);
        float rad = lerpf(r0, r0 * 0.18f, std::pow(t, 0.9f));
        if (i == 0) rad *= 1.35f;  // root flare
        tr.push_back(rad);
    }
    taperedTube(b, tp, tr, 10);
    Vec3 cc = Vec3(lean.x * 0.9f, trunkH + (h - trunkH) * 0.52f, lean.z * 0.9f);
    Vec3 cr(crown, (h - trunkH) * 0.52f, crown * r.range(0.85f, 1.0f));
    float cardSize = 1.1f * clampf(crown / 2.6f, 0.6f, 1.4f);
    CardSet cs;
    std::vector<Vec3> leafAt;
    int nb = 5 + int(r.uni() * 3.99f);
    float az0 = r.range(0.0f, kTwoPi);
    for (int bi = 0; bi < nb; ++bi) {
        float tb = float(bi) / float(nb - 1);
        float along = lerpf(trunkH / leaderH, 0.9f, tb) + r.range(-0.03f, 0.03f);
        float srad = 0.0f;
        Vec3 s = pathPoint(tp, along, &srad, &tr);
        float az = az0 + float(bi) * 2.39996f + r.range(-0.3f, 0.3f);
        float el = lerpf(0.3f, 1.0f, tb) + r.range(-0.12f, 0.12f);
        Vec3 dir(std::cos(az) * std::cos(el), std::sin(el), std::sin(az) * std::cos(el));
        float len = crown * r.range(0.8f, 1.05f) * (1.15f - 0.45f * tb);
        std::vector<Vec3> bp;
        std::vector<float> br;
        for (int k = 0; k <= 5; ++k) {
            float t = float(k) / 5.0f;
            Vec3 p = s + dir * (len * t) + Vec3(0, len * 0.18f * t * t, 0) + r.unit() * (0.04f * t);
            bp.push_back(p);
            br.push_back(lerpf(std::max(srad * 0.7f, 0.035f), 0.012f, t));
        }
        taperedTube(b, bp, br, 6);
        for (float t : {0.45f, 0.62f, 0.8f, 0.95f, 1.0f}) leafAt.push_back(pathPoint(bp, t));
        // twigs
        for (int w = 0; w < 2; ++w) {
            float tw = w == 0 ? 0.5f : 0.75f;
            Vec3 ws = pathPoint(bp, tw);
            Vec3 wd = (dir + r.unit() * 0.9f + Vec3(0, 0.3f, 0)).normalized();
            float wl = len * r.range(0.35f, 0.5f);
            std::vector<Vec3> wp = {ws, ws + wd * (wl * 0.5f), ws + wd * wl + Vec3(0, wl * 0.1f, 0)};
            std::vector<float> wr = {0.02f, 0.013f, 0.008f};
            taperedTube(b, wp, wr, 5);
            leafAt.push_back(wp[1]);
            leafAt.push_back(wp[2]);
        }
    }
    b.setMaterial(1);
    float density = clampf(sqr(crown / 2.6f), 0.4f, 2.0f);
    for (const Vec3& p : leafAt) {
        int k = 2 + int(r.uni() * 1.99f);
        for (int i = 0; i < k; ++i) leafCard(b, cs, r, p + r.unit() * 0.35f, cc, cr, cardSize, 0.35f);
    }
    int fill = int(110.0f * density);
    for (int i = 0; i < fill; ++i) {
        Vec3 u = r.unit();
        if (u.y < -0.55f) u.y = -u.y * 0.5f;  // fewer cards on the underside
        float rr = std::cbrt(r.range(0.25f, 1.0f)) * 0.95f;
        leafCard(b, cs, r, cc + Vec3(u.x * cr.x, u.y * cr.y, u.z * cr.z) * rr, cc, cr, cardSize, 0.3f);
    }
    MeshData md = b.build();
    applyAo(md, cs);
    out.mesh = std::move(md);
    std::string leaves = M(mat, (seed % 3 == 0) ? "foliage_leaves_dark" : (seed % 3 == 1 ? "foliage_leaves" : "foliage_leaves_warm"));
    if (leaves == "foliage" || leaves == "foliage_dark") leaves = "foliage_leaves";  // old scenes
    out.materials = {"bark", leaves};
    out.meshKey = keyOf("tree", j, leaves);
    out.collider = ColliderComponent::Kind::Box;
    out.boxHalfExtents = Vec3(r0 * 1.2f, trunkH * 0.5f, r0 * 1.2f);
    out.boxCenter = Vec3(0, trunkH * 0.5f, 0);
    out.cullDistance = 450.0f;
    out.surface = "wood";
}

// shrub / hedge: leaf cards in a squashed ellipsoid (size = full extents)
void shrubPrefab(const Json& j, const std::string& mat, PrefabBuild& out) {
    Vec3 size = jvec3(j, "size", Vec3(1.4f, 1.0f, 1.4f));
    int seed = Pi(j, "seed", 1);
    TreeRng r(uint64_t(seed) * 104729u + 7u);
    MeshBuilder b("shrub");
    b.setMaterial(0);
    Vec3 cr = size * 0.5f;
    Vec3 cc(0, cr.y, 0);
    CardSet cs;
    float card = clampf(std::min(size.x, size.z) * 0.45f, 0.35f, 0.8f);
    int n = int(clampf(size.x * size.z * size.y * 28.0f, 16.0f, 400.0f));
    for (int i = 0; i < n; ++i) {
        Vec3 u = r.unit();
        if (u.y < -0.3f) u.y = -u.y * 0.3f;
        float rr = std::cbrt(r.range(0.2f, 1.0f)) * 0.9f;
        Vec3 p = cc + Vec3(u.x * cr.x, u.y * cr.y, u.z * cr.z) * rr;
        leafCard(b, cs, r, p, cc, cr, card, 0.15f);
    }
    MeshData md = b.build();
    applyAo(md, cs);
    out.mesh = std::move(md);
    out.materials = {M(mat, "foliage_leaves_dark")};
    out.meshKey = keyOf("shrub", j, mat);
    out.collider = ColliderComponent::Kind::None;
    out.cullDistance = 220.0f;
}

}  // namespace

void registerNaturePrefabs(PrefabRegistry& r) {
    r.add("tree", "Props", treePrefab, {{"height", 7.0}, {"crown", 2.6}, {"seed", 1}});
    r.add("shrub", "Props", shrubPrefab, {{"size", {1.4, 1.0, 1.4}}, {"seed", 1}});
}

}  // namespace sw::kit
