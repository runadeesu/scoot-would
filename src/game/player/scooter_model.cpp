// scoot would - detailed pro freestyle scooter geometry.
//
// Real world reference dimensions (typical street / park scooter, retailer specs and sizing guides):
//   deck 50-53 cm long, 11-13.5 cm wide (4.3"-5.3"), ~3.6 cm thick boxed profile with a hollow underside and a
//   slight concave, integrated ~10 cm headtube at 83 degrees (82-84 usual) with headset cups, 24 mm dropouts,
//   110 mm x 24 mm wheels (88A PU on a spoked alloy core, two 608 bearings on an 8 mm axle, socket head
//   axle bolts), threadless fork, IHC double clamp (bar with a slit) or a one piece SCS clamp over the
//   headset, chromoly T-bar (34.9 mm oversized downtube, 31.8 mm crossbar, slight backsweep), 160 mm flanged
//   grips, spring steel flex fender brake. Overall height = bar length + ~10" (IHC) / ~11" (SCS).
#include "game/player/scooter_model.h"

#include "render/mesh_builder.h"

#include <algorithm>
#include <cmath>

namespace sw {

namespace {

// quad whose front face points along `outward` (winding fixed up from the vertex positions)
void quadOut(MeshBuilder& b, uint32_t i0, uint32_t i1, uint32_t i2, uint32_t i3, const Vec3& outward) {
    const auto& v = b.data().vertices;
    // vertices are stored transformed; compare against the transformed outward direction via the
    // stored normals instead (they were created from `outward` already)
    Vec3 fn = cross(v[i1].position - v[i0].position, v[i2].position - v[i0].position);
    Vec3 ref = v[i0].normal + v[i1].normal + v[i2].normal + v[i3].normal;
    if (ref.lengthSq() < 1e-10f) ref = outward;
    if (dot(fn, ref) < 0.0f)
        b.addQuad(i0, i3, i2, i1);
    else
        b.addQuad(i0, i1, i2, i3);
}

void triOut(MeshBuilder& b, uint32_t i0, uint32_t i1, uint32_t i2) {
    const auto& v = b.data().vertices;
    Vec3 fn = cross(v[i1].position - v[i0].position, v[i2].position - v[i0].position);
    Vec3 ref = v[i0].normal + v[i1].normal + v[i2].normal;
    if (dot(fn, ref) < 0.0f)
        b.addTriangle(i0, i2, i1);
    else
        b.addTriangle(i0, i1, i2);
}

// rounded rectangle outline in the XZ plane (CCW seen from +Y), front = -Z
std::vector<Vec2> roundedRect(float x0, float x1, float z0, float z1, float rFront, float rBack, int seg) {
    std::vector<Vec2> o;
    auto corner = [&](Vec2 c, float r, float a0) {
        for (int i = 0; i <= seg; ++i) {
            float a = a0 + float(i) / float(seg) * kHalfPi;
            o.push_back(c + Vec2(std::cos(a), std::sin(a)) * r);
        }
    };
    // (x, z) with z forward negative: go around front-left -> back-left -> back-right -> front-right
    corner(Vec2(x0 + rFront, z0 + rFront), rFront, kPi);                 // front left  (a: 180 -> 270)
    corner(Vec2(x1 - rFront, z0 + rFront), rFront, kPi * 1.5f);          // front right (270 -> 360)
    corner(Vec2(x1 - rBack, z1 - rBack), rBack, 0.0f);                   // back right  (0 -> 90)
    corner(Vec2(x0 + rBack, z1 - rBack), rBack, kHalfPi);                // back left   (90 -> 180)
    return o;
}

// prism from a convex XZ outline with chamfered top/bottom edges and an optional hollow underside
// (rim + inner wall + recessed ceiling), smooth around the outline, hard between the bands
void beveledSlab(MeshBuilder& b, const std::vector<Vec2>& outline, float yTop, float yBot, float chTop, float chBot, float hollowDepth,
                 float rim, float concave = 0.0f) {
    size_t n = outline.size();
    Vec2 cen(0);
    for (auto& p : outline) cen += p;
    cen = cen / float(n);
    std::vector<Vec2> nrm(n);
    for (size_t i = 0; i < n; ++i) {
        Vec2 d = outline[(i + 1) % n] - outline[(i + n - 1) % n];
        Vec2 nn(d.y, -d.x);
        if (dot(nn, outline[i] - cen) < 0.0f) nn = -nn;
        nrm[i] = nn.normalized();
    }
    auto ring = [&](float y, float inset, Vec3 nH, float nY) {
        // nH.x scales the outline normal, nY the vertical normal part
        std::vector<uint32_t> r(n + 1);
        for (size_t i = 0; i <= n; ++i) {
            size_t k = i % n;
            Vec2 p = outline[k] - nrm[k] * inset;
            Vec3 nn = (Vec3(nrm[k].x, 0, nrm[k].y) * nH.x + Vec3(0, nY, 0)).normalized();
            r[i] = b.addVertex(Vec3(p.x, y, p.y), nn, Vec2(float(i) / float(n), y));
        }
        return r;
    };
    auto band = [&](const std::vector<uint32_t>& a, const std::vector<uint32_t>& c) {
        for (size_t i = 0; i < n; ++i) quadOut(b, a[i], a[i + 1], c[i + 1], c[i], Vec3(0));
    };
    // top chamfer, wall, bottom chamfer (each band gets its own vertices -> crisp edges)
    auto tA = ring(yTop, chTop, Vec3(0.7071f), 0.7071f), tB = ring(yTop - chTop, 0.0f, Vec3(0.7071f), 0.7071f);
    band(tA, tB);
    auto wA = ring(yTop - chTop, 0.0f, Vec3(1.0f), 0.0f), wB = ring(yBot + chBot, 0.0f, Vec3(1.0f), 0.0f);
    band(wA, wB);
    auto bA = ring(yBot + chBot, 0.0f, Vec3(0.7071f), -0.7071f), bB = ring(yBot, chBot, Vec3(0.7071f), -0.7071f);
    band(bA, bB);
    // top face
    {
        // concave: the middle of the platform sits a little lower than the edges
        uint32_t c = b.addVertex(Vec3(cen.x, yTop - concave, cen.y), Vec3(0, 1, 0), Vec2(0.5f, 0.5f));
        auto top = ring(yTop, chTop, Vec3(0.0f), 1.0f);
        for (size_t i = 0; i < n; ++i) triOut(b, c, top[i], top[i + 1]);
    }
    if (hollowDepth > 0.0f) {
        auto r0 = ring(yBot, chBot, Vec3(0.0f), -1.0f), r1 = ring(yBot, rim, Vec3(0.0f), -1.0f);
        band(r0, r1);
        auto w0 = ring(yBot, rim, Vec3(-1.0f), 0.0f), w1 = ring(yBot + hollowDepth, rim, Vec3(-1.0f), 0.0f);
        band(w0, w1);
        uint32_t c = b.addVertex(Vec3(cen.x, yBot + hollowDepth, cen.y), Vec3(0, -1, 0), Vec2(0.5f, 0.5f));
        auto ceil = ring(yBot + hollowDepth, rim, Vec3(0.0f), -1.0f);
        for (size_t i = 0; i < n; ++i) triOut(b, c, ceil[i], ceil[i + 1]);
    } else {
        uint32_t c = b.addVertex(Vec3(cen.x, yBot, cen.y), Vec3(0, -1, 0), Vec2(0.5f, 0.5f));
        auto bot = ring(yBot, chBot, Vec3(0.0f), -1.0f);
        for (size_t i = 0; i < n; ++i) triOut(b, c, bot[i], bot[i + 1]);
    }
}

// sweep a rounded rectangle (superellipse) section along a path; `side` is the section's width axis
// (kept perpendicular to the path), sections give half width / half height per path point
void sweep(MeshBuilder& b, const std::vector<Vec3>& path, const std::vector<Vec2>& half, float sq, Vec3 sideHint, int seg, bool capA, bool capB) {
    size_t n = path.size();
    if (n < 2) return;
    std::vector<std::vector<Vec3>> P(n, std::vector<Vec3>(size_t(seg)));
    std::vector<Vec3> T(n);
    for (size_t i = 0; i < n; ++i) {
        Vec3 t = (path[std::min(i + 1, n - 1)] - path[i == 0 ? 0 : i - 1]).normalized();
        T[i] = t;
        Vec3 s = sideHint - t * dot(sideHint, t);
        if (s.lengthSq() < 1e-8f) s = anyPerpendicular(t);
        s = s.normalized();
        Vec3 u = cross(t, s).normalized();
        for (int k = 0; k < seg; ++k) {
            float th = float(k) / float(seg) * kTwoPi;
            float cs = std::cos(th), sn = std::sin(th), e = 2.0f / sq;
            float ex = (cs < 0 ? -1.0f : 1.0f) * std::pow(std::fabs(cs), e) * half[i].x;
            float ey = (sn < 0 ? -1.0f : 1.0f) * std::pow(std::fabs(sn), e) * half[i].y;
            P[i][size_t(k)] = path[i] + s * ex + u * ey;
        }
    }
    auto normalAt = [&](size_t i, int k) {
        const Vec3& a = P[i][size_t((k + 1) % seg)];
        const Vec3& c = P[i][size_t((k + seg - 1) % seg)];
        Vec3 nn = cross(a - c, T[i]);
        if (dot(nn, P[i][size_t(k)] - path[i]) < 0.0f) nn = -nn;
        return nn.normalized();
    };
    std::vector<std::vector<uint32_t>> id(n, std::vector<uint32_t>(size_t(seg) + 1));
    float v = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) v += (path[i] - path[i - 1]).length();
        for (int k = 0; k <= seg; ++k) id[i][size_t(k)] = b.addVertex(P[i][size_t(k % seg)], normalAt(i, k % seg), Vec2(float(k) / float(seg), v));
    }
    for (size_t i = 0; i + 1 < n; ++i)
        for (int k = 0; k < seg; ++k) quadOut(b, id[i][size_t(k)], id[i][size_t(k) + 1], id[i + 1][size_t(k) + 1], id[i + 1][size_t(k)], Vec3(0));
    auto cap = [&](size_t i, float dir) {
        Vec3 nn = T[i] * dir;
        uint32_t c = b.addVertex(path[i], nn, Vec2(0.5f, 0.5f));
        std::vector<uint32_t> r;
        for (int k = 0; k <= seg; ++k) r.push_back(b.addVertex(P[i][size_t(k % seg)], nn, Vec2(0, 0)));
        for (int k = 0; k < seg; ++k) triOut(b, c, r[size_t(k)], r[size_t(k) + 1]);
    };
    if (capA) cap(0, -1.0f);
    if (capB) cap(n - 1, 1.0f);
}

// flat plate: 2D (z, y) profile (any simple polygon) extruded between x0 and x1
void plateZY(MeshBuilder& b, std::vector<Vec2> prof, float x0, float x1) {
    // make the profile CCW in (z, y)
    float area = 0.0f;
    for (size_t i = 0; i < prof.size(); ++i) {
        const Vec2& a = prof[i];
        const Vec2& c = prof[(i + 1) % prof.size()];
        area += a.x * c.y - c.x * a.y;
    }
    if (area < 0.0f) std::reverse(prof.begin(), prof.end());
    size_t n = prof.size();
    for (size_t i = 0; i < n; ++i) {
        Vec2 a = prof[i], c = prof[(i + 1) % n];
        Vec2 d = c - a;
        Vec3 nn = Vec3(0, -d.x, d.y).normalized();  // outward for a CCW (z, y) polygon
        uint32_t i0 = b.addVertex(Vec3(x0, a.y, a.x), nn, Vec2(a.x, x0)), i1 = b.addVertex(Vec3(x0, c.y, c.x), nn, Vec2(c.x, x0));
        uint32_t i2 = b.addVertex(Vec3(x1, c.y, c.x), nn, Vec2(c.x, x1)), i3 = b.addVertex(Vec3(x1, a.y, a.x), nn, Vec2(a.x, x1));
        quadOut(b, i0, i1, i2, i3, nn);
    }
    std::vector<Vec3> capHi, capLo;
    for (auto& p : prof) capLo.push_back(Vec3(x0, p.y, p.x));
    for (auto it = prof.rbegin(); it != prof.rend(); ++it) capHi.push_back(Vec3(x1, it->y, it->x));
    b.polygon(capLo, Vec3(-1, 0, 0));
    b.polygon(capHi, Vec3(1, 0, 0));
}

// hex head / nut along X
void hexX(MeshBuilder& b, Vec3 c, float r, float len) {
    b.setTransform(Mat4::translation(c) * Mat4::rotation(Quat::angleAxis(-kHalfPi, Vec3(0, 0, 1))));
    b.cylinder(Vec3(0, -len * 0.5f, 0), r, len, 6, true);
    b.resetTransform();
}

// socket head cap screw along X: round head with a chamfer, dark hex socket in the outer face
void socketX(MeshBuilder& b, Vec3 c, float r, float len, float outward, int headSlot, int recessSlot) {
    Mat4 m = Mat4::translation(c) * Mat4::rotation(Quat::angleAxis(-kHalfPi, Vec3(0, 0, 1)));  // local Y -> world X
    b.setTransform(m);
    b.setMaterial(headSlot);
    float o = outward > 0.0f ? 1.0f : -1.0f;
    // lathe profile along local Y (outward = +Y after flipping for the -X side)
    std::vector<Vec2> prof = {{0.0f, -o * len * 0.5f}, {r, -o * len * 0.5f}, {r, o * (len * 0.5f - r * 0.2f)}, {r * 0.8f, o * len * 0.5f},
                              {0.0f, o * len * 0.5f}};
    if (o < 0.0f) std::reverse(prof.begin(), prof.end());
    b.lathe(prof, 18, false);
    b.setMaterial(recessSlot);
    std::vector<Vec3> hex;
    for (int i = 0; i < 6; ++i) {
        float a = float(i) / 6.0f * kTwoPi;
        hex.push_back(Vec3(std::cos(a) * r * 0.45f, o * (len * 0.5f + 0.00015f), std::sin(a) * r * 0.45f));
    }
    b.polygon(hex, Vec3(0, o, 0));
    b.resetTransform();
}

}  // namespace

float scooterBarHeight(const ScooterDims& d, int bars) { return bars == 1 ? d.barHeight + 0.05f : d.barHeight; }
float scooterBarWidth(const ScooterDims& d, int bars) { return bars == 2 ? 0.61f : d.barWidth; }

ScooterMeshSet buildScooterModel(const ScooterDims& d, const ScooterModelOptions& opt) {
    ScooterMeshSet out;
    const Vec3 S = d.steerAxis();
    const Vec3 fa = d.frontAxle(), ra = d.rearAxle();
    const Vec3 X(1, 0, 0);
    auto at = [&](float s) { return fa + S * s; };
    Quat steerRot = Quat::fromTo(Vec3(0, 1, 0), S);
    Mat4 steerM = Mat4::translation(fa) * Mat4::rotation(steerRot);  // local Y = steer axis, origin front axle

    float deckW = opt.deck == 1 ? 0.135f : opt.deck == 2 ? 0.115f : 0.122f;
    float noseR = opt.deck == 2 ? 0.045f : 0.03f;
    float yTop = d.deckTop, thick = 0.036f, yBot = yTop - thick;
    float zF = -0.215f, zB = 0.205f;
    float hwDrop0 = 0.0165f, hwDrop1 = 0.0225f;  // dropout inner / outer half spacing

    // ---- deck: platform, neck, gusset, headtube + headset, dropouts, hardware -----------------
    {
        MeshBuilder b("scooter_deck");
        b.setMaterial(0);
        beveledSlab(b, roundedRect(-deckW * 0.5f, deckW * 0.5f, zF, zB, noseR, 0.022f, 6), yTop, yBot, 0.0035f, 0.003f, 0.02f, 0.0065f, 0.0012f);
        // centre rib inside the hollow underside
        b.box(Vec3(0, yBot + 0.012f, (zF + zB) * 0.5f), Vec3(0.004f, 0.02f, (zB - zF) - 0.05f));
        // neck: boxed tube rising from the deck nose into the headtube
        {
            std::vector<Vec3> path = {Vec3(0, yTop - 0.018f, zF + 0.03f), Vec3(0, yTop - 0.014f, zF - 0.004f), Vec3(0, yTop + 0.012f, zF - 0.028f),
                                      at(0.118f) + S * 0.0f};
            std::vector<Vec2> half = {{0.034f, 0.017f}, {0.032f, 0.018f}, {0.028f, 0.02f}, {0.023f, 0.021f}};
            sweep(b, path, half, 3.2f, X, 20, false, false);
        }
        // gusset under the neck
        plateZY(b, {{zF + 0.05f, yBot + 0.002f}, {zF - 0.006f, yBot + 0.002f}, {at(0.1f).z - 0.004f, at(0.1f).y}, {zF + 0.01f, yTop - 0.01f}}, -0.0035f,
                0.0035f);
        // headtube
        b.setTransform(steerM);
        b.lathe({{0.0245f, 0.106f}, {0.0245f, 0.2f}}, 28, true);
        b.setMaterial(2);  // headset cups + top cap
        b.lathe({{0.017f, 0.095f}, {0.0277f, 0.095f}, {0.0277f, 0.106f}, {0.0245f, 0.107f}}, 28, false);
        b.lathe({{0.0245f, 0.2f}, {0.0277f, 0.201f}, {0.0277f, 0.2115f}, {0.0205f, 0.212f}, {0.0205f, 0.2175f}, {0.0f, 0.2175f}}, 28, false);
        b.resetTransform();
        b.setMaterial(0);
        // dropouts: side plates from the tail down around the rear axle
        std::vector<Vec2> drop = {{0.15f, yTop - 0.004f}, {0.15f, yBot + 0.004f}};
        for (int i = 0; i <= 12; ++i) {
            float a = (-105.0f + float(i) / 12.0f * 200.0f) * kDeg2Rad;
            drop.push_back(Vec2(ra.z + std::cos(a) * 0.0155f, ra.y + std::sin(a) * 0.0155f));
        }
        drop.push_back({ra.z - 0.03f, yTop - 0.004f});
        for (float s : {-1.0f, 1.0f}) plateZY(b, drop, s < 0 ? -hwDrop1 : hwDrop0, s < 0 ? -hwDrop0 : hwDrop1);
        // hardware: rear axle bolt head + nut, spacers, brake bolts
        b.setMaterial(1);
        socketX(b, ra + Vec3(hwDrop1 + 0.003f, 0, 0), 0.0082f, 0.006f, 1.0f, 1, 2);
        b.setMaterial(1);
        hexX(b, ra - Vec3(hwDrop1 + 0.0028f, 0, 0), 0.0088f, 0.0056f);
        for (float s : {-1.0f, 1.0f}) hexX(b, ra + Vec3(s * 0.0145f, 0, 0), 0.0058f, 0.004f);
        for (float z : {0.178f, 0.198f}) b.cylinder(Vec3(0, yTop + 0.0028f, z), 0.0042f, 0.0012f, 12, true);
        MeshData md = b.build();
        out.deck = md;
    }
    // ---- griptape (stops where the brake starts) -------------------------------------------------
    {
        MeshBuilder b("scooter_grip");
        b.setUvScale(1.0f);
        auto o = roundedRect(-deckW * 0.5f + 0.004f, deckW * 0.5f - 0.004f, zF + 0.004f, 0.162f, std::max(noseR - 0.004f, 0.004f), 0.003f, 6);
        std::vector<Vec3> pts;
        for (auto it = o.rbegin(); it != o.rend(); ++it) pts.push_back(Vec3(it->x, yTop + 0.0007f, it->y));
        b.polygon(pts, Vec3(0, 1, 0));
        // logo cut out of the griptape near the tail (hexagon + triangle outlines): the deck shows through
        b.setMaterial(1);
        const float lz = 0.108f, ly = yTop + 0.0009f;
        auto ringPoly = [&](int sides, float rOut, float rIn, float rot) {
            for (int i = 0; i < sides; ++i) {
                float a0 = rot + float(i) / float(sides) * kTwoPi, a1 = rot + float(i + 1) / float(sides) * kTwoPi;
                Vec3 o0(std::cos(a0) * rOut, ly, lz + std::sin(a0) * rOut), o1(std::cos(a1) * rOut, ly, lz + std::sin(a1) * rOut);
                Vec3 i0(std::cos(a0) * rIn, ly, lz + std::sin(a0) * rIn), i1(std::cos(a1) * rIn, ly, lz + std::sin(a1) * rIn);
                b.polygon({o0, o1, i1, i0}, Vec3(0, 1, 0));
            }
        };
        float logoR = std::min(0.031f, deckW * 0.26f);
        ringPoly(6, logoR, logoR * 0.8f, 0.0f);
        ringPoly(3, logoR * 0.5f, logoR * 0.32f, kHalfPi);  // triangle pointing to the tail
        out.grip = b.build();
    }
    // ---- brake: spring steel fender over the rear wheel ----------------------------------------
    {
        MeshBuilder b("scooter_brake");
        float R = d.wheelRadius + 0.0065f;
        std::vector<Vec3> path = {Vec3(0, yTop + 0.0012f, 0.166f), Vec3(0, yTop + 0.0016f, 0.21f)};
        float a0 = std::acos(clampf((yTop + 0.0022f - ra.y) / R, -1.0f, 1.0f));
        for (int i = 0; i <= 10; ++i) {
            float a = a0 - float(i) / 10.0f * (a0 + 55.0f * kDeg2Rad);  // from the front, over the top, down the back
            path.push_back(ra + Vec3(0, std::cos(a) * R, -std::sin(a) * R));
        }
        Vec3 e = path.back(), e2 = path[path.size() - 2];
        path.push_back(e + (e - e2).normalized() * 0.006f + Vec3(0, -0.002f, 0.002f));
        std::vector<Vec2> half(path.size(), Vec2(0.026f, 0.0011f));
        half[0] = Vec2(0.024f, 0.0011f);
        sweep(b, path, half, 6.0f, X, 16, true, true);
        out.brake = b.build();
    }
    // ---- fork: crown, blades, dropouts + axle hardware ------------------------------------------
    {
        MeshBuilder b("scooter_fork");
        b.setMaterial(0);
        Vec3 crown = at(0.085f);
        b.box(crown, Vec3(0.066f, 0.016f, 0.034f), steerRot);
        for (float s : {-1.0f, 1.0f}) {
            b.setTransform(Mat4::translation(crown + X * (s * 0.033f)) * Mat4::rotation(steerRot));
            b.cylinder(Vec3(0, -0.008f, 0), 0.017f, 0.016f, 16, true);
            b.resetTransform();
            std::vector<Vec3> leg;
            std::vector<Vec2> half;
            for (int i = 0; i <= 6; ++i) {
                float t = float(i) / 6.0f;
                float x = lerpf(0.032f, 0.0215f, t);
                leg.push_back(at(0.08f * (1.0f - t)) + X * (s * x) + Vec3(0, 0, -0.004f * std::sin(t * kPi)));
                half.push_back(Vec2(0.0052f, lerpf(0.0125f, 0.0095f, t)));
            }
            sweep(b, leg, half, 3.0f, X, 14, false, false);
            // dropout boss around the axle
            b.setTransform(Mat4::translation(fa + X * (s * 0.0215f)) * Mat4::rotation(Quat::angleAxis(-kHalfPi, Vec3(0, 0, 1))));
            b.cylinder(Vec3(0, -0.0052f, 0), 0.0115f, 0.0104f, 18, true);
            b.resetTransform();
        }
        b.setMaterial(1);
        socketX(b, fa + Vec3(0.0215f + 0.0082f, 0, 0), 0.0082f, 0.006f, 1.0f, 1, 2);
        b.setMaterial(1);
        hexX(b, fa - Vec3(0.0215f + 0.008f, 0, 0), 0.0088f, 0.0056f);
        for (float s : {-1.0f, 1.0f}) hexX(b, fa + Vec3(s * 0.0142f, 0, 0), 0.0058f, 0.0036f);
        // steerer visible between the headset top cap and the clamp
        b.setTransform(steerM);
        b.lathe({{0.0142f, 0.2175f}, {0.0142f, 0.223f}}, 20, true);
        b.resetTransform();
        out.fork = b.build();
    }
    // ---- bars: downtube, backswept crossbar, weld blend, gussets, bar ends ----------------------
    float barH = scooterBarHeight(d, opt.bars);
    float barW = scooterBarWidth(d, opt.bars);
    out.barHeight = barH;
    out.barWidth = barW;
    float sTop = (d.deckTop + barH) / S.y;
    Vec3 top = at(sTop);
    auto crossPt = [&](float x) {
        float t = std::fabs(x) / (barW * 0.5f);
        return top + Vec3(x, 0.008f * std::pow(t, 1.8f), 0.022f * std::pow(t, 1.6f));
    };
    {
        MeshBuilder b("scooter_bars");
        b.setMaterial(0);
        b.tube({at(opt.clamp == 1 ? 0.25f : 0.2235f), top}, 0.01745f, 20, true);
        std::vector<Vec3> cross;
        for (int i = 0; i <= 12; ++i) cross.push_back(crossPt(-barW * 0.5f + barW * float(i) / 12.0f));
        b.tube(cross, 0.0159f, 20, true);
        b.sphere(top, 0.0192f, 10, 16);  // weld / bend blend at the T
        for (float s : {-1.0f, 1.0f}) b.tube({crossPt(s * 0.105f) + Vec3(0, -0.004f, 0), top - S * 0.105f}, 0.0074f, 10, true);
        // bar end plugs
        b.setMaterial(1);
        for (float s : {-1.0f, 1.0f}) {
            Vec3 e = crossPt(s * barW * 0.5f);
            Vec3 dir = (crossPt(s * barW * 0.5f) - crossPt(s * barW * 0.45f)).normalized();
            b.tube({e - dir * 0.002f, e + dir * 0.0045f}, 0.0163f, 20, true);
        }
        out.bars = b.build();
    }
    // ---- grips: flanged, ribbed rubber (160 mm) -------------------------------------------------
    {
        MeshBuilder b("scooter_grips");
        std::vector<Vec2> prof = {{0.0f, 0.0f}, {0.0158f, 0.0f}, {0.0205f, 0.0015f}, {0.0222f, 0.0055f}};
        for (float a = 0.011f; a < 0.128f; a += 0.0045f) {
            prof.push_back({0.0221f, a});
            prof.push_back({0.0211f, a + 0.00225f});
        }
        prof.insert(prof.end(), {{0.0221f, 0.13f}, {0.0232f, 0.1335f}, {0.0288f, 0.1365f}, {0.0288f, 0.1415f}, {0.0205f, 0.1445f}, {0.0172f, 0.158f}});
        for (float s : {-1.0f, 1.0f}) {
            Vec3 e = crossPt(s * barW * 0.5f) + (crossPt(s * barW * 0.5f) - crossPt(s * barW * 0.45f)).normalized() * 0.0045f;
            Vec3 inward = (crossPt(s * barW * 0.32f) - crossPt(s * barW * 0.5f)).normalized();
            b.setTransform(Mat4::translation(e) * Mat4::rotation(Quat::fromTo(Vec3(0, 1, 0), inward)));
            b.lathe(prof, 20, true);
            b.resetTransform();
        }
        out.grips = b.build();
    }
    // ---- clamp: IHC double clamp around the slit bar, or a one piece SCS clamp over the headset -------
    {
        MeshBuilder b("scooter_clamp");
        b.setMaterial(0);
        Vec3 back = (Vec3(0, 0, 1) - S * dot(Vec3(0, 0, 1), S)).normalized();  // slit + bolts face the rider
        Quat along = Quat::fromTo(Vec3(0, 1, 0), S);
        float s0, s1;
        std::vector<float> bolts;
        if (opt.clamp == 1) {
            // SCS: sits on the headset top cap, the fork steerer clamps in the lower half, the bar (no slit) in
            // the upper half; a bolt through the middle wall pulls the fork up
            s0 = 0.2125f;
            s1 = 0.297f;
            bolts = {0.2255f, 0.2445f, 0.265f, 0.2835f};
        } else {
            s0 = 0.2215f;
            s1 = 0.276f;
            bolts = {0.235f, 0.2625f};
        }
        b.setTransform(steerM);
        b.lathe({{0.0178f, s0}, {0.0262f, s0}, {0.0268f, s0 + 0.0025f}, {0.0268f, s1 - 0.0025f}, {0.0262f, s1}, {0.0178f, s1}}, 32, false);
        if (opt.clamp == 1) {
            // relief groove between the fork and bar halves, compression bolt head on top of the middle wall
            b.lathe({{0.0269f, 0.2535f}, {0.0262f, 0.2545f}, {0.0262f, 0.2575f}, {0.0269f, 0.2585f}}, 32, false);
        }
        b.resetTransform();
        float mid = (s0 + s1) * 0.5f, len = s1 - s0 - 0.004f;
        for (float sgn : {-1.0f, 1.0f}) b.box(at(mid) + back * 0.029f + X * (sgn * 0.0068f), Vec3(0.0105f, len, 0.02f), along);
        b.setMaterial(1);
        for (float h : bolts) {
            Vec3 c = at(h) + back * 0.031f;
            // socket head cap screw on one side, the threaded end on the other
            b.setTransform(Mat4::translation(c + X * 0.0142f) * Mat4::rotation(Quat::angleAxis(-kHalfPi, Vec3(0, 0, 1))));
            b.cylinder(Vec3(0, -0.0022f, 0), 0.0045f, 0.0045f, 14, true);
            b.resetTransform();
            hexX(b, c - X * 0.0138f, 0.0044f, 0.0036f);
        }
        out.clamp = b.build();
    }
    // ---- wheel: PU tyre + alloy core (axis X, centred at the origin) ----------------------------
    {
        Mat4 rot = Mat4::rotation(Quat::angleAxis(-kHalfPi, Vec3(0, 0, 1)));  // lathe Y -> X
        float r = d.wheelRadius;
        MeshBuilder t("scooter_tyre");
        t.setTransform(rot);
        std::vector<Vec2> tyre = {{0.0412f, -0.0118f}};
        for (int i = 0; i <= 14; ++i) {
            float a = (-72.0f + float(i) / 14.0f * 144.0f) * kDeg2Rad;
            tyre.push_back({r - 0.0135f + 0.0135f * std::cos(a), 0.0128f * std::sin(a)});
        }
        tyre.push_back({0.0412f, 0.0118f});
        t.lathe(tyre, 40, true);
        t.resetTransform();
        out.tyre = t.build();

        MeshBuilder c("scooter_core");
        c.setTransform(rot);
        c.setMaterial(0);
        c.lathe({{0.0356f, -0.0113f}, {0.0414f, -0.0119f}}, 36, false);
        c.lathe({{0.0414f, 0.0119f}, {0.0356f, 0.0113f}}, 36, false);
        c.lathe({{0.0356f, -0.0113f}, {0.0356f, 0.0113f}}, 36, true, true);
        c.lathe({{0.0165f, -0.0126f}, {0.0165f, 0.0126f}}, 24, true);
        c.lathe({{0.0106f, -0.0126f}, {0.0165f, -0.0126f}}, 24, false);
        c.lathe({{0.0165f, 0.0126f}, {0.0106f, 0.0126f}}, 24, false);
        int spokes = opt.wheels == 1 ? 12 : opt.wheels == 2 ? 0 : 6;
        if (spokes == 0) {
            c.lathe({{0.0165f, -0.0045f}, {0.0356f, -0.0052f}}, 36, false);
            c.lathe({{0.0356f, 0.0052f}, {0.0165f, 0.0045f}}, 36, false);
            // lightening dimples
            for (int i = 0; i < 5; ++i) {
                float a = float(i) / 5.0f * kTwoPi;
                for (float s : {-1.0f, 1.0f}) c.cylinder(Vec3(std::cos(a) * 0.026f, s * 0.0048f - (s > 0 ? 0.0f : 0.0006f), -std::sin(a) * 0.026f), 0.0052f, 0.0006f, 14, true);
            }
        } else {
            float w = spokes == 12 ? 0.0042f : 0.0078f;
            for (int i = 0; i < spokes; ++i) {
                float a = float(i) / float(spokes) * kTwoPi;
                Quat q = Quat::angleAxis(a, Vec3(0, 1, 0));
                c.box(q * Vec3(0.026f, 0, 0), Vec3(0.021f, spokes == 12 ? 0.0085f : 0.0105f, w), q);
            }
        }
        c.setMaterial(1);  // bearing shields in a shallow recess
        c.lathe({{0.0106f, -0.0126f}, {0.0106f, -0.0119f}}, 24, false);
        c.lathe({{0.0046f, -0.0119f}, {0.0106f, -0.0119f}}, 24, false);
        c.lathe({{0.0106f, 0.0119f}, {0.0106f, 0.0126f}}, 24, false);
        c.lathe({{0.0106f, 0.0119f}, {0.0046f, 0.0119f}}, 24, false);
        c.setMaterial(2);  // inner race / axle
        c.lathe({{0.0f, -0.0122f}, {0.0046f, -0.0122f}}, 16, false);
        c.lathe({{0.0046f, 0.0122f}, {0.0f, 0.0122f}}, 16, false);
        c.resetTransform();
        out.core = c.build();
    }
    return out;
}

}  // namespace sw
